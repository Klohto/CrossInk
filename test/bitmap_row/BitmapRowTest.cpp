#include <gtest/gtest.h>

#include <array>
#include <utility>
#include <vector>

#include "lib/GfxRenderer/BitmapRow.h"

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

void compare(int orientation, bitmapRow::Plane plane, bool absolute, int width, int x, int y, int origin, int rows,
             glyphBitmap::Clip logicalClip, int crop) {
  SCOPED_TRACE(::testing::Message() << orientation << ',' << int(plane) << ',' << absolute << ',' << width << " at "
                                    << x << ',' << y << " band " << origin << ',' << rows << " crop " << crop);
  std::vector<uint8_t> source((width + 3) / 4);
  for (size_t i = 0; i < source.size(); ++i) source[i] = static_cast<uint8_t>(i * 73 + 0x1b);
  std::vector<uint8_t> expected(rows * STRIDE + 32);
  for (size_t i = 0; i < expected.size(); ++i) expected[i] = static_cast<uint8_t>(i * 53 + 0xa5);
  auto actual = expected;
  for (int sx = crop; sx < width - crop; ++sx) {
    const int lx = x + sx;
    if (lx < logicalClip.left || lx >= logicalClip.right || y < logicalClip.top || y >= logicalClip.bottom) continue;
    const auto [px, py] = physical(orientation, lx, y);
    if (px < 0 || px >= WIDTH || py < origin || py >= origin + rows) continue;
    const int value = (source[sx / 4] >> (6 - (sx % 4) * 2)) & 3;
    bool write = value < 3;
    bool black = true;
    if (plane != bitmapRow::Plane::BW) {
      if (absolute) {
        write = true;
        black = !(value == 3 || value == (plane == bitmapRow::Plane::GrayMSB ? 2 : 1));
      } else {
        write = value == 1 || (plane == bitmapRow::Plane::GrayMSB && value == 2);
        black = false;
      }
    }
    if (write) {
      auto& byte = expected[16 + (py - origin) * STRIDE + px / 8];
      const uint8_t mask = 0x80 >> (px % 8);
      if (black)
        byte &= static_cast<uint8_t>(~mask);
      else
        byte |= mask;
    }
  }
  const glyphBitmap::Frame logical{x, y, 1, 0, 0, 1};
  glyphBitmap::Clip clip{crop, 0, width - crop, 1};
  glyphBitmap::clipToRect(logical, logicalClip.left, logicalClip.top, logicalClip.right, logicalClip.bottom, clip);
  const auto [px, py] = physical(orientation, x, y);
  const auto [xx, xy] = physical(orientation, x + 1, y);
  const auto [yx, yy] = physical(orientation, x, y + 1);
  const bitmapRow::Target target{
      actual.data() + 16, WIDTH, STRIDE, origin, rows, {px, py, xx - px, xy - py, yx - px, yy - py}};
  bitmapRow::draw(source.data(), width, plane, absolute, target, clip);
  EXPECT_EQ(expected, actual);
}
}  // namespace

TEST(BitmapRow, MatchesPixelReferenceWithEveryPlaneOrientationCropAndStrip) {
  for (int orientation = 0; orientation < 4; ++orientation)
    for (auto plane : {bitmapRow::Plane::BW, bitmapRow::Plane::GrayLSB, bitmapRow::Plane::GrayMSB})
      for (bool absolute : {false, true})
        for (int width : {0, 1, 3, 7, 16, 31, 80})
          for (auto [x, y] :
               {std::pair{-50, -3}, std::pair{-5, 0}, std::pair{0, 7}, std::pair{9, 11}, std::pair{29, 31}})
            for (auto [origin, rows] : {std::pair{0, 32}, std::pair{0, 7}, std::pair{7, 11}, std::pair{28, 4}})
              for (int crop : {0, 1, 3}) {
                compare(orientation, plane, absolute, width, x, y, origin, rows, {-100, -100, 100, 100}, crop);
                compare(orientation, plane, absolute, width, x, y, origin, rows, {2, 3, 18, 20}, crop);
              }
}
