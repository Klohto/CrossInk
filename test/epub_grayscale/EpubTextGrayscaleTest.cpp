#include <Epub/blocks/TextBlock.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <GlyphBitmap.h>
#include <SdCardFont.h>
#include <TouchReaderPreviewModel.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>

namespace {
// The reference paints logical pixels through the existing scalar renderer.
// Compare whole buffers, including guards around the strip scratch.
template <class Reference, class Candidate>
void compareRaster(GfxRenderer& renderer, HalDisplay& display, int origin, int rows, Reference reference,
                   Candidate candidate) {
  const auto initial = display.bw;
  std::vector<uint8_t> expected(display.stride * rows + 32, 0xA5);
  auto observed = expected;
  if (origin >= 0) renderer.beginStripTarget(expected.data() + 16, origin, rows);
  reference();
  if (origin >= 0)
    renderer.endStripTarget();
  else
    expected = display.bw;
  display.bw = initial;
  if (origin >= 0) renderer.beginStripTarget(observed.data() + 16, origin, rows);
  candidate();
  if (origin >= 0) {
    renderer.endStripTarget();
    ASSERT_EQ(display.bw, initial);
  } else {
    observed = display.bw;
  }
  ASSERT_EQ(observed, expected);
  display.bw = initial;
}

template <class Check>
void rasterPlacements(Check check) {
  constexpr std::array<int, 7> positions{-9, -1, 0, 17, 39, 63, 80};
  for (const auto [width, height] : {std::pair{64, 40}, std::pair{61, 39}}) {
    fakeheap::reset(true);
    HalDisplay display(width, height);
    GfxRenderer renderer(display);
    renderer.begin();
    for (int orientation = 0; orientation < 4; ++orientation) {
      renderer.setOrientation(GfxRenderer::Orientation(orientation));
      for (int x : positions)
        for (int y : positions)
          for (const auto [origin, rows] :
               {std::pair{-1, height}, std::pair{0, 1}, std::pair{13, 7}, std::pair{height - 1, 1}})
            for (int clip = 0; clip < 3; ++clip) {
              SCOPED_TRACE(testing::Message() << width << ':' << height << ':' << orientation << ':' << x << ':' << y
                                              << ':' << origin << ':' << rows << ':' << clip);
              if (clip) renderer.beginTextClip(2, 3, clip == 1 ? 32 : 0, clip == 1 ? 23 : 0);
              check(renderer, display, x, y, origin, rows);
              if (clip) renderer.endTextClip();
            }
    }
  }
}

TEST(GlyphFrameRaster, PackedGlyphMatchesScalarPixelsAcrossOrientationsAndClips) {
  constexpr int width = 11, height = 7;
  std::array<uint8_t, (width * height + 3) / 4> twoBit{};
  std::array<uint8_t, (width * height + 7) / 8> oneBit{};
  for (int i = 0; i < width * height; ++i) {
    twoBit[i / 4] |= uint8_t((i * 7 + i / width) % 4) << (6 - (i % 4) * 2);
    oneBit[i / 8] |= uint8_t((i + i / width) % 2) << (7 - i % 8);
  }
  rasterPlacements([&](GfxRenderer& renderer, HalDisplay& display, int x, int y, int origin, int rows) {
    for (const auto frame : {glyphBitmap::Frame{x, y, 1, 0, 0, 1}, glyphBitmap::Frame{x, y, 0, -1, 1, 0},
                             glyphBitmap::Frame{x, y, -1, 0, 0, -1}, glyphBitmap::Frame{x, y, 0, 1, -1, 0}})
      for (bool depth : {false, true})
        for (auto mode : {GfxRenderer::BW, GfxRenderer::GRAYSCALE_LSB, GfxRenderer::GRAYSCALE_MSB})
          for (bool state : {false, true}) {
            SCOPED_TRACE(testing::Message()
                         << frame.dxX << ':' << frame.dxY << ':' << depth << ':' << mode << ':' << state);
            const auto* bitmap = depth ? twoBit.data() : oneBit.data();
            compareRaster(
                renderer, display, origin, rows,
                [&] {
                  for (int gy = 0; gy < height; ++gy)
                    for (int gx = 0; gx < width; ++gx) {
                      const int i = gy * width + gx;
                      const int value =
                          depth ? (bitmap[i / 4] >> (6 - (i % 4) * 2)) & 3 : (bitmap[i / 8] >> (7 - i % 8)) & 1;
                      const bool ink = !depth || mode == GfxRenderer::BW    ? value != 0
                                       : mode == GfxRenderer::GRAYSCALE_MSB ? value == 1 || value == 2
                                                                            : value == 2;
                      if (ink)
                        renderer.drawPixel(x + gx * frame.dxX + gy * frame.dyX, y + gx * frame.dxY + gy * frame.dyY,
                                           depth && mode != GfxRenderer::BW ? false : state);
                    }
                },
                [&] { renderer.drawGlyphBitmap(bitmap, width, height, frame, depth, mode, state); });
          }
  });
}

TEST(GlyphFrameRaster, PaddedMonoRowsMatchScalarPixelsAcrossOrientationsAndClips) {
  constexpr int width = 11, height = 7, stride = (width + 7) / 8;
  std::array<uint8_t, stride * height> bitmap{};
  for (int i = 0; i < int(bitmap.size()); ++i) bitmap[i] = uint8_t(i * 53 + 7);
  rasterPlacements([&](GfxRenderer& renderer, HalDisplay& display, int x, int y, int origin, int rows) {
    compareRaster(
        renderer, display, origin, rows,
        [&] {
          for (int gy = 0; gy < height; ++gy)
            for (int gx = 0; gx < width; ++gx)
              if (!(bitmap[gy * stride + gx / 8] & (0x80 >> (gx % 8)))) renderer.drawPixel(x + gx, y + gy);
        },
        [&] { renderer.drawMonoBitmap(bitmap.data(), width, height, x, y); });
  });
}

TEST(GlyphFrameRaster, BmpRowsMatchScalarTonesAcrossOrientationsAndClips) {
  constexpr int width = 11, height = 7, stride = 4, offset = 70;
  for (bool topDown : {false, true}) {
    auto data = std::make_shared<HostFileData>();
    data->bytes.resize(offset + stride * height, 0);
    auto put16 = [&](int at, uint16_t value) { memcpy(data->bytes.data() + at, &value, 2); };
    auto put32 = [&](int at, uint32_t value) { memcpy(data->bytes.data() + at, &value, 4); };
    put16(0, 0x4d42);
    put32(2, data->bytes.size());
    put32(10, offset);
    put32(14, 40);
    put32(18, width);
    put32(22, topDown ? -height : height);
    put16(26, 1);
    put16(28, 2);
    put32(34, stride * height);
    put32(46, 4);
    for (int value = 0; value < 4; ++value)
      for (int channel = 0; channel < 3; ++channel) data->bytes[54 + value * 4 + channel] = value * 85;
    for (int y = 0; y < height; ++y)
      for (int x = 0; x < width; ++x)
        data->bytes[offset + (topDown ? y : height - 1 - y) * stride + x / 4] |= uint8_t((x + y) % 4)
                                                                                 << (6 - (x % 4) * 2);
    HalFile file(data);
    Bitmap bitmap(file);
    ASSERT_EQ(bitmap.parseHeaders(), BmpReaderError::Ok);
    rasterPlacements([&](GfxRenderer& renderer, HalDisplay& display, int x, int y, int origin, int rows) {
      for (bool absolute : {false, true})
        for (auto mode : {GfxRenderer::BW, GfxRenderer::GRAYSCALE_LSB, GfxRenderer::GRAYSCALE_MSB}) {
          SCOPED_TRACE(testing::Message() << topDown << ':' << absolute << ':' << mode);
          renderer.setRenderMode(GfxRenderer::BW);
          if (absolute) ASSERT_TRUE(renderer.displayAbsoluteGrayscaleBase());
          renderer.setRenderMode(mode);
          ASSERT_EQ(bitmap.rewindToData(), BmpReaderError::Ok);
          compareRaster(
              renderer, display, origin, rows,
              [&] {
                for (int gy = 0; gy < height; ++gy)
                  for (int gx = 0; gx < width; ++gx) {
                    const int value = (gx + gy) % 4;
                    if (mode == GfxRenderer::BW) {
                      if (value < 3) renderer.drawPixel(x + gx, y + gy);
                    } else if (absolute) {
                      renderer.drawPixel(x + gx, y + gy, mode == GfxRenderer::GRAYSCALE_MSB ? value < 2 : !(value & 1));
                    } else if (value == 1 || (mode == GfxRenderer::GRAYSCALE_MSB && value == 2)) {
                      renderer.drawPixel(x + gx, y + gy, false);
                    }
                  }
              },
              [&] { ASSERT_TRUE(renderer.drawBitmap(bitmap, x, y, 0, 0)); });
        }
      renderer.setRenderMode(GfxRenderer::BW);
    });
    file.close();
  }
}

TEST(GlyphStripBounds, MatchesRotatedCornerReferenceAcrossBandsAndReversedBounds) {
  fakeheap::reset(true);
  HalDisplay display(64, 40);
  GfxRenderer renderer(display);
  renderer.begin();
  std::array<uint8_t, 64 * 40 / 8> scratch{};
  constexpr std::array<int, 9> coordinates{-48, -1, 0, 16, 39, 40, 63, 64, 120};
  for (int orientation = 0; orientation < 4; ++orientation) {
    renderer.setOrientation(GfxRenderer::Orientation(orientation));
    EXPECT_TRUE(renderer.glyphIntersectsStrip(-48, -48, 120, 120));
    EXPECT_TRUE(renderer.glyphIntersectsStrip(120, 120, -48, -48));
    for (const auto [origin, rows] :
         {std::pair{0, 40}, std::pair{0, 1}, std::pair{13, 7}, std::pair{39, 1}, std::pair{20, 20}}) {
      SCOPED_TRACE(testing::Message() << orientation << ':' << origin << ':' << rows);
      renderer.beginStripTarget(scratch.data(), origin, rows);
      auto physicalY = [&](int x, int y) {
        switch (orientation) {
          case 0:
            return 39 - x;
          case 1:
            return 39 - y;
          case 2:
            return x;
          default:
            return y;
        }
      };
      for (int x0 : coordinates)
        for (int y0 : coordinates)
          for (int x1 : coordinates)
            for (int y1 : coordinates) {
              const int a = physicalY(x0, y0);
              const int b = physicalY(x1, y1);
              const bool expected = std::max(a, b) >= origin && std::min(a, b) < origin + rows;
              EXPECT_EQ(renderer.glyphIntersectsStrip(x0, y0, x1, y1), expected);
            }
      renderer.endStripTarget();
    }
  }
}

// Deterministic 2-bit glyphs with negative bearings and descenders. Both the
// built-in and real .cpfont loaders use these bytes, including RTL/CJK/marks.
struct RasterFont {
  std::vector<EpdUnicodeInterval> intervals;
  std::vector<EpdGlyph> glyphs;
  std::vector<uint8_t> pixels;
  EpdFontData data{};
  EpdFont font{&data};
  explicit RasterFont(int size) {
    for (auto [first, last] : {std::pair{32u, 127u},
                               {0xB7u, 0xB7u},
                               {0x300u, 0x36Fu},
                               {0x590u, 0x6FFu},
                               {0x4E00u, 0x4E20u},
                               {0xFB50u, 0xFEFFu},
                               {0xFFFDu, 0xFFFDu}}) {
      intervals.push_back({first, last, static_cast<uint32_t>(glyphs.size())});
      for (unsigned cp = first; cp <= last; ++cp) {
        EpdGlyph g{};
        g.width = size;
        g.height = size + 4;
        g.advanceX = size * 16;
        g.left = -3;
        g.top = size + 1;
        if (cp >= 0x300 && cp <= 0x36f) {
          g.advanceX = 0;
          g.top = size + 5;
        }
        g.dataOffset = pixels.size();
        g.dataLength = (g.width * g.height + 3) / 4;
        glyphs.push_back(g);
        for (int b = 0; b < g.dataLength; ++b) pixels.push_back(b % 2 ? 0x1B : 0xE4);
      }
    }
    data.bitmap = pixels.data();
    data.glyph = glyphs.data();
    data.intervals = intervals.data();
    data.intervalCount = intervals.size();
    data.advanceY = size + 6;
    data.ascender = size + 1;
    data.descender = -3;
    data.is2Bit = true;
  }
  template <class T>
  static void append(std::vector<uint8_t>& bytes, const T& value) {
    auto p = reinterpret_cast<const uint8_t*>(&value);
    bytes.insert(bytes.end(), p, p + sizeof(value));
  }
  std::vector<uint8_t> file() const {
    std::vector<uint8_t> bytes(64);
    memcpy(bytes.data(), "CPFONT\0\0", 8);
    auto put16 = [&](int o, uint16_t v) { memcpy(bytes.data() + o, &v, 2); };
    auto put32 = [&](int o, uint32_t v) { memcpy(bytes.data() + o, &v, 4); };
    put16(8, CPFONT_VERSION);
    put16(10, 1);
    bytes[12] = 1;
    put32(36, intervals.size());
    put32(40, glyphs.size());
    bytes[44] = data.advanceY;
    put16(45, data.ascender);
    put16(47, uint16_t(data.descender));
    put32(56, 64);
    for (auto& i : intervals) append(bytes, i);
    for (auto& g : glyphs) append(bytes, g);
    bytes.insert(bytes.end(), pixels.begin(), pixels.end());
    return bytes;
  }
};

TEST(EpubTextGrayscaleTest, RealTextRasterMatchesFullAndStripTargets) {
  for (bool sd : {false, true})
    for (int size : {12, 20})
      for (int orientation = 0; orientation < 4; ++orientation) {
        SCOPED_TRACE(testing::Message() << sd << ' ' << size << ' ' << orientation);
        fakeheap::reset(true);
        Storage.reset();
        RasterFont fixture(size);
        SdCardFont sdFont;
        HalDisplay display(orientation % 2 ? 800 : 792, 481);
        GfxRenderer renderer(display);
        renderer.begin();
        if (sd) {
          Storage.put("font.cpfont", fixture.file());
          ASSERT_TRUE(sdFont.load("font.cpfont"));
          renderer.insertFont(1, EpdFontFamily(sdFont.getEpdFont()));
          renderer.registerSdCardFont(1, &sdFont);
        } else
          renderer.insertFont(1, EpdFontFamily(&fixture.font));
        renderer.setOrientation(GfxRenderer::Orientation(orientation));
        FontCacheManager cache(renderer.getFontMap(), renderer.getSdCardFonts());
        renderer.setFontCacheManager(&cache);
        const std::vector<std::string> words = {"Abc", "e\u0301", "שלום", "سلام", "一", "\U0001F642", " "};
        const std::vector<int16_t> offsets = {0, 65, 120, 190, 260, 290, 330};
        const std::vector<EpdFontFamily::Style> styles = {
            EpdFontFamily::REGULAR, EpdFontFamily::BOLD,    EpdFontFamily::ITALIC, EpdFontFamily::BOLD_ITALIC,
            EpdFontFamily::REGULAR, EpdFontFamily::REGULAR, EpdFontFamily::REGULAR};
        const auto bw = display.bw;
        for (int feature = 0; feature < 5; ++feature) {
          auto variants = styles;
          BlockStyle blockStyle;
          blockStyle.isRtl = feature == 4;
          for (auto& style : variants)
            style = EpdFontFamily::Style(style | (feature == 0 ? EpdFontFamily::UNDERLINE | EpdFontFamily::STRIKETHROUGH
                                                  : feature == 1 ? EpdFontFamily::SMALL_CAPS
                                                  : feature == 2 ? EpdFontFamily::SUP
                                                  : feature == 3 ? EpdFontFamily::SUB
                                                                 : 0));
          TextBlock line(words, offsets, variants,
                         feature == 1 ? std::vector<uint8_t>(words.size(), 1) : std::vector<uint8_t>{},
                         feature == 1 ? std::vector<uint16_t>(words.size(), 12) : std::vector<uint16_t>{},
                         std::vector<uint16_t>(words.size(), 6),
                         std::vector<uint8_t>(words.size(), TextBlock::WORD_FLAG_BACKGROUND_BLACK), {}, blockStyle,
                         feature == 4 ? std::vector<std::string>(words.size(), "Ab") : std::vector<std::string>{});
          ASSERT_TRUE(line.valid());
          auto draw = [&] {
            for (int y : {-8, 65, 79, 145, 159, 230, 310, 390, 470}) line.render(renderer, 1, 7, y, true);
          };
          {
            auto scan = cache.createPrewarmScope();
            draw();
            scan.endScanAndPrewarm();
          }
          EXPECT_EQ(display.bw, bw);
          for (auto mode : {GfxRenderer::BW, GfxRenderer::GRAYSCALE_LSB, GfxRenderer::GRAYSCALE_MSB}) {
            renderer.setRenderMode(mode);
            std::vector<uint8_t> full(display.bw.size()), strips(display.bw.size());
            renderer.beginStripTarget(full.data(), 0, display.height);
            renderer.clearScreen(mode == GfxRenderer::BW ? 255 : 0);
            draw();
            renderer.endStripTarget();
            for (int y = 0; y < display.height; y += 80) {
              int rows = std::min(80, display.height - y);
              renderer.beginStripTarget(strips.data() + size_t(y) * display.stride, y, rows);
              renderer.clearScreen(mode == GfxRenderer::BW ? 255 : 0);
              draw();
              renderer.endStripTarget();
            }
            EXPECT_EQ(full, strips) << "feature=" << feature << " mode=" << mode;
            EXPECT_TRUE(std::any_of(full.begin(), full.end(),
                                    [&](auto b) { return b != (mode == GfxRenderer::BW ? 255 : 0); }));
            EXPECT_EQ(display.bw, bw);
            EXPECT_EQ(renderer.getWriteTarget(), display.bw.data());
          }
          renderer.setRenderMode(GfxRenderer::BW);
        }
        renderer.setFontCacheManager(nullptr);
        renderer.removeFont(1);
      }
}
}  // namespace

TEST(EpubTextRaster, VariationSelectorsDoNotDrawOrAdvance) {
  for (const bool sd : {false, true}) {
    SCOPED_TRACE(testing::Message() << "sd=" << sd);
    fakeheap::reset(true);
    Storage.reset();
    RasterFont fixture(12);
    SdCardFont sdFont;
    HalDisplay display;
    GfxRenderer renderer(display);
    renderer.begin();
    if (sd) {
      Storage.put("font.cpfont", fixture.file());
      ASSERT_TRUE(sdFont.load("font.cpfont"));
      renderer.insertFont(1, EpdFontFamily(sdFont.getEpdFont()));
      renderer.registerSdCardFont(1, &sdFont);
    } else {
      renderer.insertFont(1, EpdFontFamily(&fixture.font));
    }

    const auto render = [&renderer, &display](const char* text) {
      renderer.clearScreen();
      renderer.drawText(1, 25, 40, text);
      return display.bw;
    };

    const auto base = render("!");
    const int baseAdvance = renderer.getTextAdvanceX(1, "!", EpdFontFamily::REGULAR);
    for (const char* text : {"!\xE1\xA0\x8B", "!\xEF\xB8\x8E", "!\xEF\xB8\x8F"}) {  // U+180B/U+FE0E/U+FE0F
      EXPECT_EQ(renderer.getTextAdvanceX(1, text, EpdFontFamily::REGULAR), baseAdvance);
      EXPECT_EQ(render(text), base);
    }
    renderer.removeFont(1);
  }
}

TEST(EpubTextGrayscaleTest, PairedStripsMatchSeparatePlanesForTextStylesAndShapes) {
  for (bool sd : {false, true})
    for (int orientation = 0; orientation < 4; ++orientation) {
      fakeheap::reset(true);
      Storage.reset();
      RasterFont fixture(12);
      SdCardFont sdFont;
      HalDisplay display(792, 481);
      GfxRenderer renderer(display);
      renderer.begin();
      if (sd) {
        Storage.put("paired.cpfont", fixture.file());
        ASSERT_TRUE(sdFont.load("paired.cpfont"));
        renderer.insertFont(1, EpdFontFamily(sdFont.getEpdFont()));
        renderer.registerSdCardFont(1, &sdFont);
      } else {
        renderer.insertFont(1, EpdFontFamily(&fixture.font));
      }
      renderer.setOrientation(GfxRenderer::Orientation(orientation));
      FontCacheManager cache(renderer.getFontMap(), renderer.getSdCardFonts());
      renderer.setFontCacheManager(&cache);
      const uint8_t icon[] = {0x55, 0xAA, 0xF0};
      const auto draw = [&] {
        renderer.fillRect(12, 60, 30, 22, false);
        renderer.fillRectDither(40, 75, 19, 21, Color::DarkGray);
        renderer.drawLine(2, 82, 190, 82);
        renderer.drawMonoBitmap(icon, 8, 3, 61, 79);
        for (auto style : {EpdFontFamily::REGULAR, EpdFontFamily::SMALL_CAPS, EpdFontFamily::SUP,
                           EpdFontFamily::SUB})
          renderer.drawText(1, 5, 70 + static_cast<int>(style), "Abc e\u0301 שלום سلام", true, style);
      };
      {
        auto scan = cache.createPrewarmScope();
        draw();
        scan.endScanAndPrewarm();
      }
      const auto bw = display.bw;
      for (int origin : {0, 79, 160, 480}) {
        const int rows = std::min(80, display.height - origin);
        std::array<std::vector<uint8_t>, 2> expected;
        for (int plane = 0; plane < 2; ++plane) {
          expected[plane].resize(display.stride * rows);
          renderer.setRenderMode(plane == 0 ? GfxRenderer::GRAYSCALE_LSB : GfxRenderer::GRAYSCALE_MSB);
          renderer.beginStripTarget(expected[plane].data(), origin, rows);
          renderer.clearScreen(0);
          draw();
          renderer.endStripTarget();
        }
        std::vector<uint8_t> low(display.stride * rows + 32, 0xA5), high = low;
        renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
        renderer.beginDualStripTarget(low.data() + 16, high.data() + 16, origin, rows);
        renderer.clearScreen(0);
        draw();
        renderer.endStripTarget();
        EXPECT_EQ(std::vector<uint8_t>(low.begin() + 16, low.end() - 16), expected[0]);
        EXPECT_EQ(std::vector<uint8_t>(high.begin() + 16, high.end() - 16), expected[1]);
        for (const auto* buffer : {&low, &high}) {
          EXPECT_TRUE(std::all_of(buffer->begin(), buffer->begin() + 16, [](auto b) { return b == 0xA5; }));
          EXPECT_TRUE(std::all_of(buffer->end() - 16, buffer->end(), [](auto b) { return b == 0xA5; }));
        }
        EXPECT_EQ(display.bw, bw);
        EXPECT_FALSE(renderer.isDualStripTargetActive());
      }
      renderer.setRenderMode(GfxRenderer::BW);
    }
}

TEST(AbsoluteImageRaster, TextMatchesBlackWhiteInBothPlanesAndCancellationResetsMode) {
  fakeheap::reset(true);
  Storage.reset();
  RasterFont fixture(12);
  HalDisplay display;
  GfxRenderer renderer(display);
  renderer.begin();
  renderer.insertFont(1, EpdFontFamily(&fixture.font));
  for (int orientation = 0; orientation < 4; ++orientation) {
    renderer.setOrientation(GfxRenderer::Orientation(orientation));
    renderer.clearScreen();
    renderer.drawText(1, 25, 40, "Book cover");
    const auto expected = display.bw;
    ASSERT_TRUE(renderer.displayAbsoluteGrayscaleBase());
    for (auto mode : {GfxRenderer::GRAYSCALE_LSB, GfxRenderer::GRAYSCALE_MSB}) {
      renderer.clearScreen();
      renderer.setRenderMode(mode);
      renderer.drawText(1, 25, 40, "Book cover");
      EXPECT_EQ(display.bw, expected);
    }
    renderer.setRenderMode(GfxRenderer::BW);
    EXPECT_FALSE(renderer.grayPlanesAreAbsolute());
  }
  EXPECT_EQ(display.canceled, 4);
  display.absoluteSupported = false;
  EXPECT_FALSE(renderer.supportsAbsoluteGrayscale());
  EXPECT_FALSE(renderer.displayAbsoluteGrayscaleBase());
  EXPECT_FALSE(renderer.grayPlanesAreAbsolute());
}

TEST(AbsoluteImageRaster, BitmapPlanesPreserveFourTonesAndWhiteMargins) {
  fakeheap::reset(true);
  Storage.reset();
  // Two identical bottom-up rows of black/dark/light/white, padded to four bytes.
  auto data = std::make_shared<HostFileData>();
  data->bytes.resize(78, 0);
  auto put16 = [&](int offset, uint16_t value) { memcpy(data->bytes.data() + offset, &value, 2); };
  auto put32 = [&](int offset, uint32_t value) { memcpy(data->bytes.data() + offset, &value, 4); };
  put16(0, 0x4d42);
  put32(2, 78);
  put32(10, 70);
  put32(14, 40);
  put32(18, 4);
  put32(22, 2);
  put16(26, 1);
  put16(28, 2);
  put32(34, 8);
  put32(46, 4);
  for (int level = 0; level < 4; ++level)
    for (int channel = 0; channel < 3; ++channel) data->bytes[54 + level * 4 + channel] = level * 85;
  data->bytes[70] = data->bytes[74] = 0x1b;
  HalFile file(data);
  Bitmap bitmap(file);
  ASSERT_EQ(bitmap.parseHeaders(), BmpReaderError::Ok);
  HalDisplay display(8, 4);
  GfxRenderer renderer(display);
  renderer.begin();
  renderer.setOrientation(GfxRenderer::LandscapeCounterClockwise);
  ASSERT_TRUE(renderer.displayAbsoluteGrayscaleBase());
  int plane = 0;
  for (auto mode : {GfxRenderer::GRAYSCALE_LSB, GfxRenderer::GRAYSCALE_MSB}) {
    ASSERT_EQ(bitmap.rewindToData(), BmpReaderError::Ok);
    renderer.clearScreen();
    renderer.setRenderMode(mode);
    ASSERT_TRUE(renderer.drawBitmap(bitmap, 0, 3, 8, 4));
    // The first file row lies below the screen. The second must still be read.
    EXPECT_EQ(display.bw[0], 0xff);
    EXPECT_EQ(display.bw[1], 0xff);
    EXPECT_EQ(display.bw[2], 0xff);
    EXPECT_EQ(display.bw[3], plane++ == 0 ? 0x5f : 0x3f);
  }
  // A truncated second pass must be reported, allowing the caller to cancel it.
  ASSERT_EQ(bitmap.rewindToData(), BmpReaderError::Ok);
  data->readFailAt = 74;
  EXPECT_FALSE(renderer.drawBitmap(bitmap, 0, 0, 8, 4));
  renderer.setRenderMode(GfxRenderer::BW);
  EXPECT_EQ(display.canceled, 1);
  file.close();
}

// A font switch begins with no resident glyphs. A wide replacement glyph makes
// the cold scan fit fewer words than the final, correctly measured preview.
TEST(EpubTextGrayscaleTest, ColdSdSamplePreviewMatchesFullyLoadedFont) {
  for (int size : {12, 20}) {
    for (int width : {160, 280}) {
      for (bool focus : {false, true}) {
        for (bool guide : {false, true}) {
          SCOPED_TRACE(testing::Message() << size << " width=" << width << " focus=" << focus << " guide=" << guide);
          fakeheap::reset(false);
          Storage.reset();
          RasterFont fixture(size);
          fixture.glyphs.back().advanceX = size * 16 * 4;
          SdCardFont sdFont;
          Storage.put("preview.cpfont", fixture.file());
          ASSERT_TRUE(sdFont.load("preview.cpfont"));
          HalDisplay display;
          GfxRenderer renderer(display);
          renderer.begin();
          renderer.insertFont(1, EpdFontFamily(sdFont.getEpdFont()));
          renderer.registerSdCardFont(1, &sdFont);
          renderer.insertFont(2, EpdFontFamily(&fixture.font));
          FontCacheManager cache(renderer.getFontMap(), renderer.getSdCardFonts());
          renderer.setFontCacheManager(&cache);
          SampleReaderPreviewModel model;
          ASSERT_TRUE(model.captureParagraph(READER_PREVIEW_PARAGRAPH));
          auto draw = [&](int font) {
            model.renderText(renderer, font, 10, 10, width, 100, 0, static_cast<uint8_t>(CssTextAlign::Left), focus,
                             guide, true, 85);
          };
          renderer.clearScreen();
          draw(2);
          const auto expected = display.bw;
          renderer.clearScreen();
          const auto blank = display.bw;
          auto scope = cache.createPrewarmScope();
          draw(1);
          EXPECT_EQ(display.bw, blank);  // Scanning must not paint the display.
          ASSERT_TRUE(scope.endScanAndPrewarm());
          draw(1);
          EXPECT_NE(display.bw, blank);
          EXPECT_TRUE(display.bw == expected);
        }
      }
    }
  }
}
