#include <gtest/gtest.h>

#include <utility>
#include <vector>

#include "lib/GfxRenderer/MonoBitmap.h"

namespace {
constexpr int WIDTH = 40;
constexpr int HEIGHT = 32;
constexpr int STRIDE = WIDTH / 8;

std::pair<int, int> physical(int orientation, int x, int y) {
  switch (orientation) {
    case 0:
      return {y, HEIGHT - 1 - x};
    case 1:
      return {WIDTH - 1 - x, HEIGHT - 1 - y};
    case 2:
      return {WIDTH - 1 - y, x};
    default:
      return {x, y};
  }
}

void compare(int orientation, int width, int height, int x, int y, int origin, int rows,
             glyphBitmap::Clip logicalClip) {
  SCOPED_TRACE(::testing::Message() << orientation << ',' << width << ',' << height << " at " << x << ',' << y
                                    << " band " << origin << ',' << rows);
  const int sourceStride = (width + 7) / 8;
  std::vector<uint8_t> source(sourceStride * height);
  for (size_t i = 0; i < source.size(); ++i) source[i] = static_cast<uint8_t>(i * 73 + 0x9b);
  std::vector<uint8_t> expected(rows * STRIDE + 32);
  for (size_t i = 0; i < expected.size(); ++i) expected[i] = static_cast<uint8_t>(i * 53 + 0xa5);
  auto actual = expected;
  for (int sy = 0; sy < height; ++sy) {
    for (int sx = 0; sx < width; ++sx) {
      if (x + sx < logicalClip.left || x + sx >= logicalClip.right || y + sy < logicalClip.top ||
          y + sy >= logicalClip.bottom)
        continue;
      const auto [px, py] = physical(orientation, x + sx, y + sy);
      if (px < 0 || px >= WIDTH || py < origin || py >= origin + rows) continue;
      if (source[sy * sourceStride + sx / 8] & (0x80u >> (sx % 8))) continue;
      expected[16 + (py - origin) * STRIDE + px / 8] &= static_cast<uint8_t>(~(0x80u >> (px % 8)));
    }
  }
  const glyphBitmap::Frame logical{x, y, 1, 0, 0, 1};
  glyphBitmap::Clip clip{0, 0, width, height};
  glyphBitmap::clipToRect(logical, logicalClip.left, logicalClip.top, logicalClip.right, logicalClip.bottom, clip);
  const auto [px, py] = physical(orientation, x, y);
  const auto [xx, xy] = physical(orientation, x + 1, y);
  const auto [yx, yy] = physical(orientation, x, y + 1);
  const glyphBitmap::Target target{
      actual.data() + 16, WIDTH, STRIDE, origin, rows, {px, py, xx - px, xy - py, yx - px, yy - py}};
  monoBitmap::draw(source.data(), width, height, target, clip);
  EXPECT_EQ(expected, actual);
}
}  // namespace

TEST(MonoBitmap, MatchesPixelReferenceWithRotationsClipsPaddingAndStrips) {
  for (int orientation = 0; orientation < 4; ++orientation)
    for (int width : {0, 1, 7, 8, 9, 15, 40, 72})
      for (int height : {0, 1, 3, 9, 41})
        for (auto [x, y] : {std::pair{-50, -3}, std::pair{-5, 0}, std::pair{0, 7}, std::pair{3, 5}, std::pair{9, 11},
                            std::pair{29, 31}})
          for (auto [origin, rows] : {std::pair{0, 32}, std::pair{0, 7}, std::pair{7, 11}, std::pair{28, 4}}) {
            compare(orientation, width, height, x, y, origin, rows, {-100, -100, 100, 100});
            compare(orientation, width, height, x, y, origin, rows, {2, 3, 18, 20});
          }
}

TEST(MonoBitmap, WhitePixelsPreserveInkAndPaddingStaysOutsideThePage) {
  const uint8_t source[]{0xff, 0x80, 0xff, 0x80};
  std::vector<uint8_t> buffer(8, 0xa5);
  const auto expected = buffer;
  const glyphBitmap::Target target{buffer.data(), 16, 2, 0, 4, {0, 0, 1, 0, 0, 1}};
  monoBitmap::draw(source, 9, 2, target, {0, 0, 9, 2});
  EXPECT_EQ(expected, buffer);
}
