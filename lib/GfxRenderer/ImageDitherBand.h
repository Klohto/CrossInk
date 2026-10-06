#pragma once

#include "BitmapHelpers.h"

// Decoder blocks must form complete rows before serpentine diffusion runs.
class ImageDitherBand {
 public:
  bool begin(int width, int height, int bandRows, bool monochrome) {
    if (width <= 0 || height <= 0 || bandRows <= 0) return false;
    if (bandRows > height) bandRows = height;
    constexpr size_t limit = 24 * 1024;
    if (static_cast<size_t>(width) > limit / static_cast<size_t>(bandRows)) return false;
    const size_t bytes = static_cast<size_t>(width) * bandRows;
    // One reusable MCU band, capped at 24 KiB, avoids a full image allocation.
    rows = makeUniqueNoThrow<uint8_t[]>(bytes);
    dither = makeUniqueNoThrow<FloydSteinbergDitherer>(width, true, monochrome);
    if (!rows || !dither || !dither->isValid()) {
      rows.reset();
      dither.reset();
      return false;
    }
    std::memset(rows.get(), 255, bytes);
    this->width = width;
    this->height = height;
    this->bandRows = bandRows;
    this->monochrome = monochrome;
    nextRow = 0;
    return true;
  }
  bool valid() const { return rows != nullptr; }
  bool put(int x, int y, uint8_t gray) {
    if (!valid() || x < 0 || x >= width || y < nextRow || y >= height || y >= nextRow + bandRows) return false;
    rows[static_cast<size_t>(y % bandRows) * width + x] = gray;
    return true;
  }
  template <class WriteRow>
  bool advanceTo(int y, WriteRow write) {
    if (!valid() || y < nextRow || y > height) return false;
    while (nextRow < y) {
      auto* row = rows.get() + static_cast<size_t>(nextRow % bandRows) * width;
      for (int step = 0; step < width; ++step) {
        const int x = dither->isReverseRow() ? width - 1 - step : step;
        row[x] = dither->processPixel(row[x], x) * (monochrome ? 3 : 1);
      }
      write(nextRow, row, width);
      std::memset(row, 255, width);
      dither->nextRow();
      ++nextRow;
    }
    return true;
  }

 private:
  std::unique_ptr<uint8_t[]> rows;
  std::unique_ptr<FloydSteinbergDitherer> dither;
  int width = 0, height = 0, bandRows = 0, nextRow = 0;
  bool monochrome = false;
};
