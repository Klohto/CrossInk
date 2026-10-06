#pragma once

#include "GlyphBitmap.h"

// Rows are byte-padded and MSB first. Zero is black. White pixels leave the
// destination alone, as they do when the reader calls drawPixel only for ink.
namespace monoBitmap {

__attribute__((always_inline)) inline void paint(uint8_t* buffer, int bit, uint8_t source, uint8_t mask) {
  if (!(source & mask)) buffer[bit >> 3] &= static_cast<uint8_t>(~(0x80u >> (bit & 7)));
}

inline void draw(const uint8_t* bitmap, int width, int height, const glyphBitmap::Target& target,
                 glyphBitmap::Clip clip) {
  clip.left = std::max(clip.left, 0);
  clip.top = std::max(clip.top, 0);
  clip.right = std::min(clip.right, width);
  clip.bottom = std::min(clip.bottom, height);
  glyphBitmap::clipToRect(target.frame, 0, target.originY, target.width, target.originY + target.rows, clip);
  if (clip.left >= clip.right || clip.top >= clip.bottom) return;

  const int sourceStride = (width + 7) / 8;
  const int strideBits = target.stride * 8;
  const auto& frame = target.frame;
  const int stepX = frame.dxY * strideBits + frame.dxX;
  const int stepY = frame.dyY * strideBits + frame.dyX;
  int rowBit = (frame.y - target.originY) * strideBits + frame.x + clip.left * stepX + clip.top * stepY;
  for (int y = clip.top; y < clip.bottom; ++y, rowBit += stepY) {
    const uint8_t* row = bitmap + y * sourceStride;
    int source = clip.left;
    int bit = rowBit;
    int remaining = clip.right - source;
    while (remaining && (source & 7)) {
      paint(target.buffer, bit, row[source >> 3], 0x80u >> (source & 7));
      ++source;
      bit += stepX;
      --remaining;
    }
    if (stepX == 1 && !(bit & 7)) {
      while (remaining >= 8) {
        target.buffer[bit >> 3] &= row[source >> 3];
        source += 8;
        bit += 8;
        remaining -= 8;
      }
    } else {
      while (remaining >= 8) {
        const uint8_t packed = row[source >> 3];
        if (packed != 0xff) {
          paint(target.buffer, bit, packed, 0x80);
          paint(target.buffer, bit + stepX, packed, 0x40);
          paint(target.buffer, bit + 2 * stepX, packed, 0x20);
          paint(target.buffer, bit + 3 * stepX, packed, 0x10);
          paint(target.buffer, bit + 4 * stepX, packed, 0x08);
          paint(target.buffer, bit + 5 * stepX, packed, 0x04);
          paint(target.buffer, bit + 6 * stepX, packed, 0x02);
          paint(target.buffer, bit + 7 * stepX, packed, 0x01);
        }
        source += 8;
        bit += 8 * stepX;
        remaining -= 8;
      }
    }
    while (remaining--) {
      paint(target.buffer, bit, row[source >> 3], 0x80u >> (source & 7));
      ++source;
      bit += stepX;
    }
  }
}

}  // namespace monoBitmap
