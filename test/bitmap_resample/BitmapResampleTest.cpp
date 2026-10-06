#include <Bitmap.h>
#include <ImageDitherBand.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace {
void writeLe16(std::vector<uint8_t>& data, const size_t offset, const uint16_t value) {
  data[offset] = static_cast<uint8_t>(value);
  data[offset + 1] = static_cast<uint8_t>(value >> 8);
}

void writeLe32(std::vector<uint8_t>& data, const size_t offset, const uint32_t value) {
  data[offset] = static_cast<uint8_t>(value);
  data[offset + 1] = static_cast<uint8_t>(value >> 8);
  data[offset + 2] = static_cast<uint8_t>(value >> 16);
  data[offset + 3] = static_cast<uint8_t>(value >> 24);
}

std::vector<uint8_t> create24BitBmp(const int width, const int height, const bool topDown = false) {
  const int rowBytes = (width * 24 + 31) / 32 * 4;
  constexpr size_t kPixelOffset = 54;
  std::vector<uint8_t> data(kPixelOffset + static_cast<size_t>(rowBytes) * height, 0);
  data[0] = 'B';
  data[1] = 'M';
  writeLe32(data, 2, static_cast<uint32_t>(data.size()));
  writeLe32(data, 10, kPixelOffset);
  writeLe32(data, 14, 40);
  writeLe32(data, 18, width);
  writeLe32(data, 22, static_cast<uint32_t>(topDown ? -height : height));
  writeLe16(data, 26, 1);
  writeLe16(data, 28, 24);
  writeLe32(data, 34, static_cast<uint32_t>(rowBytes * height));

  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      const uint8_t value = static_cast<uint8_t>((x * 13 + y * 7) & 0xFF);
      const int fileRow = topDown ? y : height - 1 - y;
      const size_t offset = kPixelOffset + static_cast<size_t>(fileRow) * rowBytes + x * 3;
      data[offset] = value;
      data[offset + 1] = value;
      data[offset + 2] = value;
    }
  }
  return data;
}

uint32_t fingerprint(const std::vector<uint8_t>& data) {
  uint32_t hash = 2166136261U;
  for (const uint8_t value : data) {
    hash = (hash ^ value) * 16777619U;
  }
  return hash;
}
}  // namespace

TEST(BitmapResample, DownsamplesBeforeDitheringAndRewindsDeterministically) {
  HalFile file(create24BitBmp(480, 800));
  Bitmap bitmap(file, true);
  ASSERT_EQ(bitmap.parseHeaders(), BmpReaderError::Ok);
  ASSERT_TRUE(bitmap.setDitheredOutputSize(475, 792));
  EXPECT_EQ(bitmap.getWidth(), 475);
  EXPECT_EQ(bitmap.getHeight(), 792);

  std::vector<uint8_t> firstPass;
  std::vector<uint8_t> row((475 + 3) / 4);
  std::vector<uint8_t> sourceRow(bitmap.getRowBytes());
  firstPass.reserve(row.size() * 792);
  for (int y = 0; y < 792; y++) {
    ASSERT_EQ(bitmap.readNextRow(row.data(), sourceRow.data()), BmpReaderError::Ok);
    firstPass.insert(firstPass.end(), row.begin(), row.end());
  }
  EXPECT_NE(fingerprint(firstPass), 0U);

  ASSERT_EQ(bitmap.rewindToData(), BmpReaderError::Ok);
  std::vector<uint8_t> secondPass;
  secondPass.reserve(firstPass.size());
  for (int y = 0; y < 792; y++) {
    ASSERT_EQ(bitmap.readNextRow(row.data(), sourceRow.data()), BmpReaderError::Ok);
    secondPass.insert(secondPass.end(), row.begin(), row.end());
  }
  EXPECT_EQ(secondPass, firstPass);
}

TEST(BitmapResample, ReadsPhysicalRowsBeforeRendererOrientation) {
  HalFile bottomUpFile(create24BitBmp(480, 800));
  HalFile topDownFile(create24BitBmp(480, 800, true));
  Bitmap bottomUp(bottomUpFile, true);
  Bitmap topDown(topDownFile, true);
  ASSERT_EQ(bottomUp.parseHeaders(), BmpReaderError::Ok);
  ASSERT_EQ(topDown.parseHeaders(), BmpReaderError::Ok);
  EXPECT_TRUE(bottomUp.isTopDown());
  EXPECT_TRUE(topDown.isTopDown());
  ASSERT_TRUE(bottomUp.setDitheredOutputSize(475, 792));
  ASSERT_TRUE(topDown.setDitheredOutputSize(475, 792));

  std::vector<uint8_t> bottomUpRow((475 + 3) / 4);
  std::vector<uint8_t> topDownRow((475 + 3) / 4);
  std::vector<uint8_t> bottomUpSourceRow(bottomUp.getRowBytes());
  std::vector<uint8_t> topDownSourceRow(topDown.getRowBytes());
  ASSERT_EQ(bottomUp.readNextRow(bottomUpRow.data(), bottomUpSourceRow.data()), BmpReaderError::Ok);
  ASSERT_EQ(topDown.readNextRow(topDownRow.data(), topDownSourceRow.data()), BmpReaderError::Ok);

  // Dither in visual row order so file storage order cannot change the picture.
  EXPECT_EQ(bottomUpRow, topDownRow);
  for (int y = 1; y < 792; ++y) {
    ASSERT_EQ(bottomUp.readNextRow(bottomUpRow.data(), bottomUpSourceRow.data()), BmpReaderError::Ok);
    ASSERT_EQ(topDown.readNextRow(topDownRow.data(), topDownSourceRow.data()), BmpReaderError::Ok);
    EXPECT_EQ(bottomUpRow, topDownRow) << y;
  }
}

TEST(BitmapResample, ResizingKeepsImageQuantizationSeparateFromTextOverlayLevels) {
  auto bytes = create24BitBmp(4, 4);
  std::fill(bytes.begin() + 54, bytes.end(), 85);
  for (bool imageLevels : {false, true}) {
    HalFile file(bytes);
    Bitmap bitmap(file, true, imageLevels);
    ASSERT_EQ(bitmap.parseHeaders(), BmpReaderError::Ok);
    ASSERT_TRUE(bitmap.setDitheredOutputSize(2, 2));
    std::vector<uint8_t> row(1), scratch(bitmap.getRowBytes());
    ASSERT_EQ(bitmap.readNextRow(row.data(), scratch.data()), BmpReaderError::Ok);
    EXPECT_EQ(row[0] >> 6, imageLevels ? 1 : 2);
    ASSERT_EQ(bitmap.rewindToData(), BmpReaderError::Ok);
    const auto expected = row;
    ASSERT_EQ(bitmap.readNextRow(row.data(), scratch.data()), BmpReaderError::Ok);
    EXPECT_EQ(row, expected);
  }
}

TEST(BitmapResample, PaletteStartsAfterFullInfoHeader) {
  constexpr int kWidth = 64;
  constexpr int kHeight = 32;
  for (const uint32_t infoHeaderSize : {40u, 108u, 124u}) {
    const size_t paletteOffset = 14 + infoHeaderSize;
    const size_t pixelOffset = paletteOffset + 256 * 4;
    for (const uint8_t pixelIndex : {uint8_t{0}, uint8_t{255}}) {
      std::vector<uint8_t> data(pixelOffset + kWidth * kHeight, pixelIndex);
      std::fill(data.begin(), data.begin() + paletteOffset, 0);
      data[0] = 'B';
      data[1] = 'M';
      writeLe32(data, 2, static_cast<uint32_t>(data.size()));
      writeLe32(data, 10, static_cast<uint32_t>(pixelOffset));
      writeLe32(data, 14, infoHeaderSize);
      writeLe32(data, 18, kWidth);
      writeLe32(data, 22, kHeight);
      writeLe16(data, 26, 1);
      writeLe16(data, 28, 8);
      writeLe32(data, 34, kWidth * kHeight);
      writeLe32(data, 46, 256);
      if (infoHeaderSize > 40) {
        // Extended-header fields must not be mistaken for palette entries.
        writeLe32(data, 54, 0x00FF0000);
        writeLe32(data, 58, 0x0000FF00);
        writeLe32(data, 62, 0x000000FF);
        writeLe32(data, 70, 0x73524742);  // 'sRGB'
      }
      for (int i = 0; i < 256; i++) {
        const size_t entry = paletteOffset + static_cast<size_t>(i) * 4;
        data[entry] = data[entry + 1] = data[entry + 2] = static_cast<uint8_t>(i);
        data[entry + 3] = 0;
      }

      for (const bool imageLevels : {false, true}) {
        SCOPED_TRACE(::testing::Message() << "header=" << infoHeaderSize << " index=" << static_cast<int>(pixelIndex)
                                          << " imageLevels=" << imageLevels);
        HalFile file(data);
        Bitmap bitmap(file, true, imageLevels);
        ASSERT_EQ(bitmap.parseHeaders(), BmpReaderError::Ok);
        std::vector<uint8_t> row((kWidth + 3) / 4);
        std::vector<uint8_t> sourceRow(bitmap.getRowBytes());
        const uint8_t expectedByte = pixelIndex == 255 ? 0xFF : 0;
        int unexpectedBytes = 0;
        for (int y = 0; y < kHeight; y++) {
          ASSERT_EQ(bitmap.readNextRow(row.data(), sourceRow.data()), BmpReaderError::Ok);
          for (const uint8_t packed : row) unexpectedBytes += packed != expectedByte;
        }
        EXPECT_EQ(unexpectedBytes, 0);
      }
    }
  }
}

TEST(BitmapResample, RejectsTruncatedExtendedPalette) {
  for (const uint32_t infoHeaderSize : {108u, 124u}) {
    SCOPED_TRACE(infoHeaderSize);
    // The last of the 256 palette entries is missing its red and reserved bytes.
    std::vector<uint8_t> data(14 + infoHeaderSize + 256 * 4 - 2, 0);
    data[0] = 'B';
    data[1] = 'M';
    writeLe32(data, 2, static_cast<uint32_t>(data.size()));
    writeLe32(data, 10, 14 + infoHeaderSize + 256 * 4);
    writeLe32(data, 14, infoHeaderSize);
    writeLe32(data, 18, 1);
    writeLe32(data, 22, 1);
    writeLe16(data, 26, 1);
    writeLe16(data, 28, 8);
    writeLe32(data, 46, 256);

    HalFile file(data);
    Bitmap bitmap(file);
    EXPECT_EQ(bitmap.parseHeaders(), BmpReaderError::FileInvalid);
  }
}

TEST(BitmapResample, RejectsPaletteThatOverlapsPixelData) {
  for (const uint32_t infoHeaderSize : {40u, 108u, 124u}) {
    SCOPED_TRACE(infoHeaderSize);
    // The file has enough bytes for 256 reads, but the last entry is actually pixel data.
    const uint32_t pixelOffset = 14 + infoHeaderSize + 255 * 4;
    std::vector<uint8_t> data(pixelOffset + 4, 0);
    std::fill(data.begin() + pixelOffset, data.end(), 255);
    data[0] = 'B';
    data[1] = 'M';
    writeLe32(data, 2, static_cast<uint32_t>(data.size()));
    writeLe32(data, 10, pixelOffset);
    writeLe32(data, 14, infoHeaderSize);
    writeLe32(data, 18, 4);
    writeLe32(data, 22, 1);
    writeLe16(data, 26, 1);
    writeLe16(data, 28, 8);
    writeLe32(data, 46, 256);

    HalFile file(data);
    Bitmap bitmap(file);
    EXPECT_EQ(bitmap.parseHeaders(), BmpReaderError::FileInvalid);
  }
}

TEST(BitmapResample, NativeIndexedPalettesKeepAllLevelsAndPartialRowBits) {
  constexpr int width = 13;
  constexpr int height = 2;
  for (int bpp : {1, 2, 4, 8}) {
    const int colors = 1 << bpp;
    const int rowBytes = (width * bpp + 31) / 32 * 4;
    const int pixels = 54 + colors * 4;
    std::vector<uint8_t> data(pixels + rowBytes * height, 0);
    data[0] = 'B';
    data[1] = 'M';
    writeLe32(data, 2, data.size());
    writeLe32(data, 10, pixels);
    writeLe32(data, 14, 40);
    writeLe32(data, 18, width);
    writeLe32(data, 22, height);
    writeLe16(data, 26, 1);
    writeLe16(data, 28, bpp);
    writeLe32(data, 46, colors);
    for (int i = 0; i < colors; ++i) {
      const uint8_t gray = bpp == 1 ? (i ? 255 : 0) : (3 - i % 4) * 85;
      data[54 + i * 4] = data[55 + i * 4] = data[56 + i * 4] = gray;
    }
    std::vector<uint8_t> expected((width + 3) / 4, 0);
    for (int x = 0; x < width; ++x) {
      const int index = x % colors;
      const uint8_t level = bpp == 1 ? (index ? 3 : 0) : 3 - index % 4;
      expected[x / 4] |= level << (6 - (x % 4) * 2);
      for (int y = 0; y < height; ++y) data[pixels + y * rowBytes + x * bpp / 8] |= index << (8 - bpp - (x * bpp % 8));
    }
    HalFile file(data);
    Bitmap bitmap(file, true);
    ASSERT_EQ(bitmap.parseHeaders(), BmpReaderError::Ok);
    std::vector<uint8_t> row(expected.size());
    std::vector<uint8_t> scratch(rowBytes);
    for (int pass = 0; pass < 2; ++pass) {
      ASSERT_EQ(bitmap.rewindToData(), BmpReaderError::Ok);
      for (int y = 0; y < height; ++y) {
        ASSERT_EQ(bitmap.readNextRow(row.data(), scratch.data()), BmpReaderError::Ok);
        EXPECT_EQ(row, expected) << "bpp=" << bpp;
      }
    }
  }
}

TEST(BitmapResample, RgbWithNativeColorTableStillQuantizesRgbSamples) {
  auto data = create24BitBmp(13, 2);
  data.insert(data.begin() + 54, {0, 0, 0, 0});
  writeLe32(data, 2, data.size());
  writeLe32(data, 10, 58);
  writeLe32(data, 46, 1);
  HalFile file(data);
  Bitmap bitmap(file, true);
  ASSERT_EQ(bitmap.parseHeaders(), BmpReaderError::Ok);
  std::vector<uint8_t> row(4);
  std::vector<uint8_t> scratch(bitmap.getRowBytes());
  ASSERT_EQ(bitmap.readNextRow(row.data(), scratch.data()), BmpReaderError::Ok);
  for (int x = 0; x < 13; ++x) EXPECT_EQ((row[x / 4] >> (6 - (x % 4) * 2)) & 3, (x * 13 + 7) >> 6);
}

TEST(BitmapResample, DiffusionKeepsNativeLevelsAndAverageTone) {
  for (const int gray : {0, 85, 170, 255}) {
    FloydSteinbergDitherer dither(64, true);
    ASSERT_TRUE(dither.isValid());
    for (int y = 0; y < 32; ++y) {
      for (int step = 0; step < 64; ++step) {
        const int x = dither.isReverseRow() ? 63 - step : step;
        EXPECT_EQ(dither.processPixel(gray, x), gray / 85);
      }
      dither.nextRow();
    }
  }
  for (const bool mono : {false, true}) {
    FloydSteinbergDitherer dither(64, true, mono);
    ASSERT_TRUE(dither.isValid());
    int sum = 0;
    for (int y = 0; y < 64; ++y) {
      for (int step = 0; step < 64; ++step) {
        const int x = dither.isReverseRow() ? 63 - step : step;
        sum += dither.processPixel(102, x) * (mono ? 255 : 85);
      }
      dither.nextRow();
    }
    EXPECT_NEAR(sum / 4096.0, 102.0, 1.0);
  }
}

TEST(BitmapResample, DecoderTilesMatchCompleteRowDiffusion) {
  constexpr int width = 8, height = 8;
  std::vector<uint8_t> source(width * height), expected(width * height), result(width * height);
  for (int y = 0; y < height; ++y)
    for (int x = 0; x < width; ++x) source[y * width + x] = (x * 31 + y * 19) & 255;
  FloydSteinbergDitherer reference(width, true);
  for (int y = 0; y < height; ++y) {
    for (int step = 0; step < width; ++step) {
      const int x = reference.isReverseRow() ? width - 1 - step : step;
      expected[y * width + x] = reference.processPixel(source[y * width + x], x);
    }
    reference.nextRow();
  }
  ImageDitherBand band;
  ASSERT_TRUE(band.begin(width, height, 2, false));
  auto write = [&](int y, const uint8_t* row, int count) { std::copy(row, row + count, result.begin() + y * width); };
  for (int blockY = 0; blockY < height; blockY += 2) {
    ASSERT_TRUE(band.advanceTo(blockY, write));
    for (int blockX = 0; blockX < width; blockX += 2) {
      for (int y = blockY; y < blockY + 2; ++y)
        for (int x = blockX; x < blockX + 2; ++x) {
          ASSERT_TRUE(band.put(x, y, source[y * width + x]));
        }
    }
  }
  ASSERT_TRUE(band.advanceTo(height, write));
  EXPECT_EQ(result, expected);
}
TEST(BitmapResample, RejectsOversizedAndOutOfBandDecoderRows) {
  ImageDitherBand band;
  EXPECT_FALSE(band.begin(2048, 2048, 16, false));
  ASSERT_TRUE(band.begin(8, 8, 2, false));
  EXPECT_FALSE(band.put(-1, 0, 80));
  EXPECT_FALSE(band.put(8, 0, 80));
  EXPECT_FALSE(band.put(0, 2, 80));
}

TEST(BitmapResample, MatchesSelectedImageStudyDiffusion) {
  // Golden output from x3emu.image_experiment.quantize(..., "diffused").
  // The independent study uses floating-point Floyd-Steinberg with alternating rows.
  constexpr uint8_t expected[64] = {0, 0, 1, 1, 1, 2, 2, 2, 1, 1, 1, 1, 2, 2, 2, 2, 1, 1, 2, 2, 2, 2,
                                    3, 3, 1, 2, 2, 2, 2, 3, 0, 0, 2, 2, 2, 3, 3, 0, 1, 1, 2, 3, 3, 0,
                                    0, 1, 1, 1, 3, 0, 0, 1, 1, 1, 1, 2, 0, 0, 1, 1, 1, 1, 2, 2};
  FloydSteinbergDitherer dither(8, true);
  ASSERT_TRUE(dither.isValid());
  for (int y = 0; y < 8; ++y) {
    for (int step = 0; step < 8; ++step) {
      const int x = dither.isReverseRow() ? 7 - step : step;
      EXPECT_EQ(dither.processPixel((x * 23 + y * 37 + 11) % 256, x), expected[y * 8 + x]) << x << "," << y;
    }
    dither.nextRow();
  }
}
