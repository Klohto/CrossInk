#pragma once

#include "GlyphBitmap.h"

// BMP rows contain four pixels per byte, with 0=black and 3=white.
// Resolve the plane and clip before decoding the row. No scratch allocation.
namespace bitmapRow {

using glyphBitmap::Clip;
using glyphBitmap::Plane;
using glyphBitmap::Target;

__attribute__((always_inline)) inline void paint(uint8_t* buffer, int bit, uint8_t value, uint8_t blackLevels,
                                                 uint8_t whiteLevels) {
  const uint8_t level = 1u << value;
  const uint8_t mask = 0x80u >> (bit & 7);
  if (blackLevels & level) buffer[bit >> 3] &= static_cast<uint8_t>(~mask);
  if (whiteLevels & level) buffer[bit >> 3] |= mask;
}

inline void draw(const uint8_t* row, int width, Plane plane, bool absolute, const Target& target, Clip clip) {
  uint8_t blackLevels = 0x07;
  uint8_t whiteLevels = 0;
  if (plane != Plane::BW) {
    blackLevels = absolute ? (plane == Plane::GrayMSB ? 0x03 : 0x05) : 0;
    whiteLevels = absolute ? (plane == Plane::GrayMSB ? 0x0c : 0x0a) : (plane == Plane::GrayMSB ? 0x06 : 0x02);
  }
  clip.left = std::max(clip.left, 0);
  clip.right = std::min(clip.right, width);
  clip.top = std::max(clip.top, 0);
  clip.bottom = std::min(clip.bottom, 1);
  glyphBitmap::clipToRect(target.frame, 0, target.originY, target.width, target.originY + target.rows, clip);
  if (clip.left >= clip.right || clip.top >= clip.bottom) return;

  const int step = target.frame.dxY * target.stride * 8 + target.frame.dxX;
  int source = clip.left;
  int bit = (target.frame.y - target.originY) * target.stride * 8 + target.frame.x + source * step;
  int remaining = clip.right - source;
  while (remaining && (source & 3)) {
    paint(target.buffer, bit, (row[source >> 2] >> (6 - (source & 3) * 2)) & 3, blackLevels, whiteLevels);
    ++source;
    bit += step;
    --remaining;
  }
  while (remaining >= 4) {
    const uint8_t packed = row[source >> 2];
    paint(target.buffer, bit, packed >> 6, blackLevels, whiteLevels);
    paint(target.buffer, bit + step, (packed >> 4) & 3, blackLevels, whiteLevels);
    paint(target.buffer, bit + 2 * step, (packed >> 2) & 3, blackLevels, whiteLevels);
    paint(target.buffer, bit + 3 * step, packed & 3, blackLevels, whiteLevels);
    source += 4;
    bit += 4 * step;
    remaining -= 4;
  }
  while (remaining--) {
    paint(target.buffer, bit, (row[source >> 2] >> (6 - (source & 3) * 2)) & 3, blackLevels, whiteLevels);
    ++source;
    bit += step;
  }
}

}  // namespace bitmapRow
