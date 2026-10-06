#include <EpdFontFamily.h>
#include <Utf8.h>
#include <builtinFonts/bitter_10_regular.h>
#include <builtinFonts/bitter_12_regular.h>
#include <builtinFonts/bitter_14_regular.h>
#include <builtinFonts/bitter_16_regular.h>
#include <builtinFonts/inter_10_bold.h>
#include <builtinFonts/inter_10_regular.h>
#include <builtinFonts/inter_12_bold.h>
#include <builtinFonts/inter_12_regular.h>
#include <builtinFonts/inter_8_regular.h>
#include <builtinFonts/lexenddeca_10_regular.h>
#include <builtinFonts/lexenddeca_12_regular.h>
#include <builtinFonts/lexenddeca_14_regular.h>
#include <builtinFonts/lexenddeca_16_regular.h>
#include <builtinFonts/ui_symbols_10.h>
#include <gtest/gtest.h>

#include "src/activities/reader/TxtLineFit.h"

namespace {
constexpr uint32_t POWER = 0x23FB;
const EpdFont symbols(&ui_symbols_10);
const EpdFont smallRegular(&inter_10_regular), smallBold(&inter_10_bold);
const EpdFont largeRegular(&inter_12_regular), largeBold(&inter_12_bold);
const EpdFontFamily small(&smallRegular, &smallBold, nullptr, nullptr, &symbols);
const EpdFontFamily large(&largeRegular, &largeBold, nullptr, nullptr, &symbols);
}  // namespace

TEST(UiSymbolFallback, ContainsExactlyOneGlyph) {
  EXPECT_EQ(sizeof(ui_symbols_10Glyphs) / sizeof(ui_symbols_10Glyphs[0]), 1u);
  EXPECT_EQ(sizeof(ui_symbols_10Intervals) / sizeof(ui_symbols_10Intervals[0]), 1u);
  EXPECT_EQ(ui_symbols_10Intervals[0].first, POWER);
  EXPECT_TRUE(symbols.hasCodepoint(POWER));
  EXPECT_FALSE(symbols.hasCodepoint('A'));
}

TEST(UiSymbolFallback, FirstIntervalLookupKeepsGapsAndOverlappingBoundaryChoice) {
  const EpdGlyph glyphs[14]{};
  const EpdUnicodeInterval intervals[] = {{10, 15, 0}, {15, 20, 6}, {30, 31, 12}};
  EpdFontData data{};
  data.glyph = glyphs;
  data.intervals = intervals;
  data.intervalCount = 3;
  const EpdFont font(&data);
  EXPECT_EQ(font.findGlyph(9), nullptr);
  EXPECT_EQ(font.findGlyph(10), &glyphs[0]);
  EXPECT_EQ(font.findGlyph(14), &glyphs[4]);
  EXPECT_EQ(font.findGlyph(15), &glyphs[6]);
  EXPECT_EQ(font.findGlyph(20), &glyphs[11]);
  EXPECT_EQ(font.findGlyph(21), nullptr);
  EXPECT_EQ(font.findGlyph(30), &glyphs[12]);
  EXPECT_EQ(font.findGlyph(31), &glyphs[13]);
  EXPECT_EQ(font.findGlyph(32), nullptr);
  data.intervalCount = 1;
  EXPECT_EQ(font.findGlyph(15), &glyphs[5]);
  EXPECT_EQ(font.findGlyph(16), nullptr);
  data.intervalCount = 0;
  EXPECT_EQ(font.findGlyph(10), nullptr);
}

TEST(UiSymbolFallback, LigatureBoundsKeepDuplicateKeysAndPresentationFormRules) {
  const EpdLigaturePair pairs[] = {
      {0x00660069, 0xFB01}, {0x00660069, 0xFB02}, {0x0066006C, 0xFB02}, {0x03B103B2, 0x1000}, {0xFB500061, 0x1001}};
  EpdFontData data{};
  data.ligaturePairs = pairs;
  data.ligaturePairCount = sizeof(pairs) / sizeof(pairs[0]);
  const EpdFont font(&data);
  EXPECT_EQ(font.getLigature('e', 'i'), 0u);
  EXPECT_EQ(font.getLigature('f', 'i'), 0xFB01u);
  EXPECT_EQ(font.getLigature('f', 'l'), 0xFB02u);
  EXPECT_EQ(font.getLigature('f', 'k'), 0u);
  EXPECT_EQ(font.getLigature(0x03B1, 0x03B2), 0x1000u);
  EXPECT_EQ(font.getLigature(0xFB50, 'a'), 0u);
  EXPECT_EQ(font.getLigature(0xFFFF, 'a'), 0u);
  EXPECT_EQ(font.getLigature(0x10000, 'a'), 0u);
  EXPECT_EQ(font.getLigature('f', 0x10000), 0u);
  data.ligaturePairCount = 0;
  EXPECT_EQ(font.getLigature('f', 'i'), 0u);
  data.ligaturePairCount = 1;
  data.ligaturePairs = nullptr;
  EXPECT_EQ(font.getLigature('f', 'i'), 0u);
}

TEST(UiSymbolFallback, SharesTheSameRasterAtBothScalesAndStyles) {
  for (const auto* family : {&small, &large}) {
    for (const auto style : {EpdFontFamily::REGULAR, EpdFontFamily::BOLD}) {
      const auto glyph = family->getGlyphData(POWER, style);
      EXPECT_EQ(glyph.fontData, &ui_symbols_10);
      EXPECT_EQ(glyph.glyph, symbols.findGlyph(POWER));
      EXPECT_TRUE(family->hasCodepoint(POWER, style));
      EXPECT_EQ(family->getFallbackCodepoint(POWER, style), POWER);
      int width = 0, height = 0;
      family->getTextDimensions("⏻", &width, &height, style);
      EXPECT_EQ(width, 18);
      EXPECT_EQ(height, 18);
    }
  }
}

TEST(UiSymbolFallback, PreservesNormalGlyphsAndMissingGlyphBehavior) {
  EXPECT_EQ(small.getGlyphData('A').fontData, &inter_10_regular);
  EXPECT_EQ(large.getGlyphData('*', EpdFontFamily::BOLD).fontData, &inter_12_bold);
  const EpdFontFamily noFallback(&smallRegular);
  EXPECT_FALSE(noFallback.hasCodepoint(POWER));
  EXPECT_EQ(noFallback.getGlyphData(POWER).glyph, smallRegular.getGlyph(REPLACEMENT_GLYPH));
}

TEST(UiSymbolFallback, ResolvesTheSameCodepointAndFontAsSeparateLookups) {
  const EpdFontFamily noFallback(&smallRegular);
  EpdFontData emptyData{};
  const EpdFont empty(&emptyData);
  const EpdFontFamily emptyFamily(&empty);
  for (const auto* family : {&small, &large, &noFallback, &emptyFamily}) {
    for (unsigned flags = 0; flags < 256; ++flags) {
      const auto style = static_cast<EpdFontFamily::Style>(flags);
      for (const uint32_t sourceCp :
           {0u,      32u,     65u,   0xA0u,   0x2BBu,  0x301u,  0x391u,  0x2003u,  0x200Bu,   0x2011u,
            0x2018u, 0x2022u, POWER, 0x4E00u, 0xFB01u, 0xFE0Fu, 0xFFFDu, 0x1F4D6u, 0x110000u, 0xFFFFFFFFu}) {
        const uint32_t expectedCp = family->getFallbackCodepoint(sourceCp, style);
        const auto expected = family->findGlyphData(expectedCp, style);
        uint32_t resolvedCp = sourceCp;
        const auto resolved = family->resolveGlyph(resolvedCp, style);
        EXPECT_EQ(resolvedCp, expectedCp);
        EXPECT_EQ(resolved.fontData, expected.fontData);
        EXPECT_EQ(resolved.glyph, expected.glyph);
      }
    }
  }
}

TEST(UiSymbolFallback, TxtSearchKeepsBreaksWithProductionLigatureAndKerningMetrics) {
  const EpdFontData* fonts[] = {&inter_8_regular,       &inter_10_regular,      &inter_12_regular,
                                &bitter_10_regular,     &bitter_12_regular,     &bitter_14_regular,
                                &bitter_16_regular,     &lexenddeca_10_regular, &lexenddeca_12_regular,
                                &lexenddeca_14_regular, &lexenddeca_16_regular};
  for (const auto* data : fonts) {
    const EpdFont regular(data);
    const EpdFontFamily font(&regular);
    for (const char* source : {"office staff fill coffee flasks in the first room", "WWWiiiAVAToflffifffi",
                               "   AVATAR follows the office staff  ", "hello world"}) {
      for (int limit = 1; limit < 400; ++limit) {
        const auto measure = [&](const char* text) {
          int width, height;
          font.getTextDimensions(text, &width, &height);
          return width;
        };
        std::string line = source;
        if (measure(source) <= limit) continue;
        size_t end = line.size();
        while (end && measure(line.substr(0, end).c_str()) > limit) {
          const size_t space = line.rfind(' ', end - 1);
          if (space != std::string::npos && space > 0)
            end = space;
          else
            --end;
        }
        if (!end) end = 1;
        EXPECT_EQ(txtLineFit::breakPosition(line, limit, measure), end) << source << " limit=" << limit;
      }
    }
  }
}
