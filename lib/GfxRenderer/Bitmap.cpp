#include "Bitmap.h"

#include <Logging.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <new>

// ============================================================================
// IMAGE PROCESSING OPTIONS
// ============================================================================
// Dithering is applied when converting high-color BMPs to the display's native
// 2-bit (4-level) grayscale. Images whose palette entries all map to native
// gray levels (0, 85, 170, 255 ±21) are mapped directly without dithering.
// For cover images, dithering is done in JpegToBmpConverter.cpp instead.
constexpr bool USE_ATKINSON = false;  // Use Atkinson dithering instead of Floyd-Steinberg
// ============================================================================

Bitmap::~Bitmap() {
  delete[] errorCurRow;
  delete[] errorNextRow;

  delete atkinsonDitherer;
  delete fsDitherer;
}

uint16_t Bitmap::readLE16(HalFile& f) {
  const int c0 = f.read();
  const int c1 = f.read();
  const auto b0 = static_cast<uint8_t>(c0 < 0 ? 0 : c0);
  const auto b1 = static_cast<uint8_t>(c1 < 0 ? 0 : c1);
  return static_cast<uint16_t>(b0) | (static_cast<uint16_t>(b1) << 8);
}

uint32_t Bitmap::readLE32(HalFile& f) {
  const int c0 = f.read();
  const int c1 = f.read();
  const int c2 = f.read();
  const int c3 = f.read();

  const auto b0 = static_cast<uint8_t>(c0 < 0 ? 0 : c0);
  const auto b1 = static_cast<uint8_t>(c1 < 0 ? 0 : c1);
  const auto b2 = static_cast<uint8_t>(c2 < 0 ? 0 : c2);
  const auto b3 = static_cast<uint8_t>(c3 < 0 ? 0 : c3);

  return static_cast<uint32_t>(b0) | (static_cast<uint32_t>(b1) << 8) | (static_cast<uint32_t>(b2) << 16) |
         (static_cast<uint32_t>(b3) << 24);
}

const char* Bitmap::errorToString(BmpReaderError err) {
  switch (err) {
    case BmpReaderError::Ok:
      return "Ok";
    case BmpReaderError::FileInvalid:
      return "FileInvalid";
    case BmpReaderError::SeekStartFailed:
      return "SeekStartFailed";
    case BmpReaderError::NotBMP:
      return "NotBMP (missing 'BM')";
    case BmpReaderError::DIBTooSmall:
      return "DIBTooSmall (<40 bytes)";
    case BmpReaderError::BadPlanes:
      return "BadPlanes (!= 1)";
    case BmpReaderError::UnsupportedBpp:
      return "UnsupportedBpp (expected 1, 2, 4, 8, 24, or 32)";
    case BmpReaderError::UnsupportedCompression:
      return "UnsupportedCompression (expected BI_RGB or BI_BITFIELDS for 32bpp)";
    case BmpReaderError::BadDimensions:
      return "BadDimensions";
    case BmpReaderError::ImageTooLarge:
      return "ImageTooLarge (max 2048x3072)";
    case BmpReaderError::PaletteTooLarge:
      return "PaletteTooLarge";

    case BmpReaderError::SeekPixelDataFailed:
      return "SeekPixelDataFailed";
    case BmpReaderError::BufferTooSmall:
      return "BufferTooSmall";

    case BmpReaderError::OomRowBuffer:
      return "OomRowBuffer";
    case BmpReaderError::ShortReadRow:
      return "ShortReadRow";
  }
  return "Unknown";
}

BmpReaderError Bitmap::parseHeaders() {
  if (!file) return BmpReaderError::FileInvalid;
  if (!file.seek(0)) return BmpReaderError::SeekStartFailed;

  // --- BMP FILE HEADER ---
  const uint16_t bfType = readLE16(file);
  if (bfType != 0x4D42) return BmpReaderError::NotBMP;

  file.seekCur(8);
  bfOffBits = readLE32(file);

  // --- DIB HEADER ---
  const uint32_t biSize = readLE32(file);
  if (biSize < 40) return BmpReaderError::DIBTooSmall;

  width = static_cast<int32_t>(readLE32(file));
  const auto rawHeight = static_cast<int32_t>(readLE32(file));
  topDown = rawHeight < 0;
  height = topDown ? -rawHeight : rawHeight;

  const uint16_t planes = readLE16(file);
  bpp = readLE16(file);
  const uint32_t comp = readLE32(file);
  const bool validBpp = bpp == 1 || bpp == 2 || bpp == 4 || bpp == 8 || bpp == 24 || bpp == 32;

  if (planes != 1) return BmpReaderError::BadPlanes;
  if (!validBpp) return BmpReaderError::UnsupportedBpp;
  // Allow BI_RGB (0) for all, and BI_BITFIELDS (3) for 32bpp which is common for BGRA masks.
  if (!(comp == 0 || (bpp == 32 && comp == 3))) return BmpReaderError::UnsupportedCompression;

  file.seekCur(12);  // biSizeImage, biXPelsPerMeter, biYPelsPerMeter
  colorsUsed = readLE32(file);
  // BMP spec: colorsUsed==0 means default (2^bpp for paletted formats)
  if (colorsUsed == 0 && bpp <= 8) colorsUsed = 1u << bpp;
  if (colorsUsed > 256u) return BmpReaderError::PaletteTooLarge;
  file.seekCur(4);  // biClrImportant

  if (width <= 0 || height <= 0) return BmpReaderError::BadDimensions;

  outputWidth = width;
  outputHeight = height;

  // Safety limits to prevent memory issues on ESP32
  constexpr int MAX_IMAGE_WIDTH = 2048;
  constexpr int MAX_IMAGE_HEIGHT = 3072;
  if (width > MAX_IMAGE_WIDTH || height > MAX_IMAGE_HEIGHT) {
    return BmpReaderError::ImageTooLarge;
  }

  // Pre-calculate Row Bytes to avoid doing this every row
  rowBytes = (width * bpp + 31) / 32 * 4;

  for (int i = 0; i < 256; i++) paletteLum[i] = static_cast<uint8_t>(i);
  if (colorsUsed > 0) {
    const uint64_t paletteEnd = 14ULL + biSize + 4ULL * colorsUsed;
    if (paletteEnd > bfOffBits) {
      LOG_ERR("BMP", "Palette overlaps pixel data");
      return BmpReaderError::FileInvalid;
    }
    // The palette follows the full DIB header. V4/V5 headers (108/124 bytes, as
    // written by GIMP and ImageMagick) are longer than the 40 bytes parsed above.
    if (!file.seek(14 + biSize)) {
      return BmpReaderError::SeekPixelDataFailed;
    }
    for (uint32_t i = 0; i < colorsUsed; i++) {
      uint8_t rgb[4];
      if (file.read(rgb, 4) != 4) {  // Read B, G, R, Reserved in one go
        LOG_ERR("BMP", "Incomplete palette entry %u", static_cast<unsigned>(i));
        return BmpReaderError::FileInvalid;
      }
      paletteLum[i] = (77u * rgb[2] + 150u * rgb[1] + 29u * rgb[0]) >> 8;
    }
  }

  if (!file.seek(bfOffBits)) {
    return BmpReaderError::SeekPixelDataFailed;
  }

  // Check if palette luminances map cleanly to the display's 4 native gray levels.
  // Native levels are 0, 85, 170, 255 — i.e. values where (lum >> 6) is lossless.
  // If all palette entries are near a native level, we can skip dithering entirely.
  nativePalette = bpp <= 2;  // 1-bit and 2-bit are always native
  if (!nativePalette && colorsUsed > 0) {
    nativePalette = true;
    for (uint32_t i = 0; i < colorsUsed; i++) {
      const uint8_t lum = paletteLum[i];
      const uint8_t level = lum >> 6;            // quantize to 0-3
      const uint8_t reconstructed = level * 85;  // back to 0, 85, 170, 255
      if (lum > reconstructed + 21 || lum + 21 < reconstructed) {
        nativePalette = false;  // luminance is too far from any native level
        break;
      }
    }
  }

  // Reuse the palette storage for final levels. Native indexed images never
  // enter error diffusion, so each palette entry needs adjustment only once.
  if (nativePalette && bpp <= 8) {
    for (auto& value : paletteLum) value = static_cast<uint8_t>(adjustPixel(value) >> 6);
  }

  // Decide pixel processing strategy:
  //  - Native palette → direct mapping, no processing needed
  //  - High-color + dithering enabled → error-diffusion dithering (Atkinson or Floyd-Steinberg)
  //  - High-color + dithering disabled → simple quantization (no error diffusion)
  const bool highColor = !nativePalette;
  if (highColor && dithering) {
    if (USE_ATKINSON) {
      atkinsonDitherer = new (std::nothrow) AtkinsonDitherer(width, imageLevels);
      if (!atkinsonDitherer || !atkinsonDitherer->isValid()) {
        delete atkinsonDitherer;
        atkinsonDitherer = nullptr;
        return BmpReaderError::OomRowBuffer;
      }
    } else {
      fsDitherer = new (std::nothrow) FloydSteinbergDitherer(width, imageLevels);
      if (!fsDitherer || !fsDitherer->isValid()) {
        delete fsDitherer;
        fsDitherer = nullptr;
        LOG_ERR("BMP", "Failed to allocate Floyd-Steinberg ditherer");
        return BmpReaderError::OomRowBuffer;
      }
    }
  }

  return BmpReaderError::Ok;
}

bool Bitmap::setDitheredOutputSize(const int targetWidth, const int targetHeight) {
  if (!dithering || (!atkinsonDitherer && !fsDitherer) || targetWidth <= 0 || targetHeight <= 0 ||
      targetWidth > width || targetHeight > height || (targetWidth == width && targetHeight == height))
    return false;
  // Bitmap owns the replacement helper and its two row buffers (4,240 bytes at X3 width).
  auto* resized = new (std::nothrow) FloydSteinbergDitherer(targetWidth, imageLevels);
  if (!resized || !resized->isValid()) {
    delete resized;
    return false;
  }
  delete atkinsonDitherer;
  atkinsonDitherer = nullptr;
  delete fsDitherer;
  fsDitherer = resized;
  outputWidth = targetWidth;
  outputHeight = targetHeight;
  return true;
}

// packed 2bpp output, 0 = black, 1 = dark gray, 2 = light gray, 3 = white
BmpReaderError Bitmap::readNextRow(uint8_t* data, uint8_t* rowBuffer) const {
  // Note: rowBuffer should be pre-allocated by the caller to size 'rowBytes'
  if (outputRowsRead >= outputHeight) return BmpReaderError::ShortReadRow;

  // Select source rows before dithering. For example, a 480x800 custom
  // wallpaper fitted to an X3 becomes 475x792 here, so error diffusion never
  // has to survive the renderer's later non-integer scale.
  const int sourceY = outputHeight == height
                          ? outputRowsRead
                          : std::min(height - 1, (outputRowsRead * height + height / 2) / outputHeight);
  if (fsDitherer && !topDown) {
    const size_t position = bfOffBits + static_cast<size_t>(height - 1 - sourceY) * rowBytes;
    if (!file.seek(position)) return BmpReaderError::SeekPixelDataFailed;
    if (file.read(rowBuffer, rowBytes) != rowBytes) return BmpReaderError::ShortReadRow;
  } else
    while (sourceRowsRead <= sourceY) {
      if (file.read(rowBuffer, rowBytes) != rowBytes) return BmpReaderError::ShortReadRow;
      sourceRowsRead++;
    }

  std::memset(data, 0, (outputWidth + 3) / 4);
  const int outputY = outputRowsRead;

  // Helper lambda to pack 2bpp color into the output stream
  auto packPixel = [&](const uint8_t lum, const int outputX) {
    uint8_t color;
    if (atkinsonDitherer) {
      color = atkinsonDitherer->processPixel(adjustPixel(lum), outputX);
    } else if (fsDitherer) {
      color = fsDitherer->processPixel(adjustPixel(lum), outputX);
    } else {
      if (nativePalette) {
        // Indexed palettes already hold adjusted levels. RGB samples still
        // need quantization when their header supplied a native color table.
        color = bpp <= 8 ? lum : static_cast<uint8_t>(adjustPixel(lum) >> 6);
      } else {
        // Non-native palette with dithering disabled: simple quantization
        color = quantize(adjustPixel(lum), outputX, outputY);
      }
    }
    data[outputX / 4] |= color << (6 - (outputX % 4) * 2);
  };

  const bool sameWidth = outputWidth == width;
  const bool reverse = fsDitherer && fsDitherer->isReverseRow();
  for (int step = 0; step < outputWidth; step++) {
    const int outputX = reverse ? outputWidth - 1 - step : step;
    const int sourceX = sameWidth ? outputX : std::min(width - 1, (outputX * width + width / 2) / outputWidth);
    uint8_t lum;
    switch (bpp) {
      case 32: {
        const uint8_t* p = rowBuffer + sourceX * 4;
        lum = (77u * p[2] + 150u * p[1] + 29u * p[0]) >> 8;
        break;
      }
      case 24: {
        const uint8_t* p = rowBuffer + sourceX * 3;
        lum = (77u * p[2] + 150u * p[1] + 29u * p[0]) >> 8;
        break;
      }
      case 8:
        lum = paletteLum[rowBuffer[sourceX]];
        break;
      case 4: {
        const uint8_t nibble = (sourceX & 1) ? (rowBuffer[sourceX >> 1] & 0x0F) : (rowBuffer[sourceX >> 1] >> 4);
        lum = paletteLum[nibble];
        break;
      }
      case 2:
        lum = paletteLum[(rowBuffer[sourceX >> 2] >> (6 - ((sourceX & 3) * 2))) & 0x03];
        break;
      case 1: {
        const uint8_t palIndex = (rowBuffer[sourceX >> 3] & (0x80 >> (sourceX & 7))) ? 1 : 0;
        lum = paletteLum[palIndex];
        break;
      }
      default:
        return BmpReaderError::UnsupportedBpp;
    }
    packPixel(lum, outputX);
  }

  if (atkinsonDitherer)
    atkinsonDitherer->nextRow();
  else if (fsDitherer)
    fsDitherer->nextRow();

  outputRowsRead++;

  return BmpReaderError::Ok;
}

BmpReaderError Bitmap::rewindToData() const {
  if (!file.seek(bfOffBits)) {
    return BmpReaderError::SeekPixelDataFailed;
  }

  // Reset dithering when rewinding
  if (fsDitherer) fsDitherer->reset();
  if (atkinsonDitherer) atkinsonDitherer->reset();
  sourceRowsRead = 0;
  outputRowsRead = 0;

  return BmpReaderError::Ok;
}
