#include <gtest/gtest.h>

#include <cstdint>
#include <iterator>

extern "C" {
#include "minibidi.h"
}

namespace {

constexpr struct {
  uint32_t first;
  uint32_t last;
  uchar type;
} ranges[] = {
#include "bidiclasses.t"
};

uchar expectedClass(const uint32_t cp) {
  for (const auto& range : ranges) {
    if (cp >= range.first && cp <= range.last) return range.type;
  }
  return ON;
}

}  // namespace

TEST(BidiClass, EveryUnicodeValueMatchesRangeData) {
  size_t range = 0;
  for (uint32_t cp = 0; cp <= 0x10FFFF; ++cp) {
    while (range < std::size(ranges) && cp > ranges[range].last) ++range;
    const uchar expected = range < std::size(ranges) && cp >= ranges[range].first ? ranges[range].type : ON;
    ASSERT_EQ(bidi_class(cp), expected) << "codepoint=" << cp;
  }
}

TEST(BidiClass, RangeOrderBoundariesAndLargeValues) {
  uint32_t previousLast = 0;
  bool first = true;
  for (const auto& range : ranges) {
    ASSERT_LE(range.first, range.last);
    if (!first) ASSERT_LT(previousLast, range.first);
    for (const uint32_t cp : {range.first, range.last, range.first - 1, range.last + 1}) {
      ASSERT_EQ(bidi_class(cp), expectedClass(cp)) << "codepoint=" << cp;
    }
    previousLast = range.last;
    first = false;
  }
  for (const uint32_t cp : {0x110000u, 0x7FFFFFFFu, 0x80000000u, 0xFFFFFFFFu}) {
    ASSERT_EQ(bidi_class(cp), expectedClass(cp)) << "codepoint=" << cp;
  }
}
