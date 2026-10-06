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
