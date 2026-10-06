#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>

#include "../Memory/Memory.h"

struct BmpHeader;

// Helper functions
uint8_t quantize(int gray, int x, int y);
uint8_t quantizeSimple(int gray);
uint8_t quantize1bit(int gray, int x, int y);
int adjustPixel(int gray);

struct GrayPlanePixel {
  bool write;
  bool black;
};

// Levels: black, dark, light, white. drawPixel(true) clears a framebuffer bit.
constexpr GrayPlanePixel grayPlanePixel(uint8_t level, bool msb, bool absolute) {
  if (absolute) return {true, !(level == 3 || level == (msb ? 2 : 1))};
  return {msb ? (level == 1 || level == 2) : level == 1, false};
}

enum class BmpRowOrder { BottomUp, TopDown };

// Populates a 1-bit BMP header in the provided memory.
void createBmpHeader(BmpHeader* bmpHeader, int width, int height, BmpRowOrder rowOrder);

// 1-bit Atkinson dithering - better quality than noise dithering for thumbnails
// Error distribution pattern (same as 2-bit but quantizes to 2 levels):
//     X  1/8 1/8
// 1/8 1/8 1/8
//     1/8
class Atkinson1BitDitherer {
 public:
  explicit Atkinson1BitDitherer(int width) {
    if (width <= 0) return;
    const size_t candidateRowSize = static_cast<size_t>(width) + 4;
    if (candidateRowSize > SIZE_MAX / (3 * sizeof(int16_t))) return;
    rowSize = candidateRowSize;
    errorRows = makeUniqueNoThrow<int16_t[]>(rowSize * 3);
    if (!errorRows) return;
    errorRow0 = errorRows.get();
    errorRow1 = errorRow0 + rowSize;
    errorRow2 = errorRow1 + rowSize;
  }

  bool isValid() const { return errorRows != nullptr; }

  // EXPLICITLY DELETE THE COPY CONSTRUCTOR
  Atkinson1BitDitherer(const Atkinson1BitDitherer& other) = delete;

  // EXPLICITLY DELETE THE COPY ASSIGNMENT OPERATOR
  Atkinson1BitDitherer& operator=(const Atkinson1BitDitherer& other) = delete;

  uint8_t processPixel(int gray, int x) {
    if (!isValid()) return adjustPixel(gray) < 128 ? 0 : 1;

    // Apply brightness/contrast/gamma adjustments
    gray = adjustPixel(gray);

    // Add accumulated error
    int adjusted = gray + errorRow0[x + 2];
    if (adjusted < 0) adjusted = 0;
    if (adjusted > 255) adjusted = 255;

    // Quantize to 2 levels (1-bit): 0 = black, 1 = white
    uint8_t quantized;
    int quantizedValue;
    if (adjusted < 128) {
      quantized = 0;
      quantizedValue = 0;
    } else {
      quantized = 1;
      quantizedValue = 255;
    }

    // Calculate error (only distribute 6/8 = 75%)
    int error = (adjusted - quantizedValue) >> 3;  // error/8

    // Distribute 1/8 to each of 6 neighbors
    errorRow0[x + 3] += error;  // Right
    errorRow0[x + 4] += error;  // Right+1
    errorRow1[x + 1] += error;  // Bottom-left
    errorRow1[x + 2] += error;  // Bottom
    errorRow1[x + 3] += error;  // Bottom-right
    errorRow2[x + 2] += error;  // Two rows down

    return quantized;
  }

  void nextRow() {
    if (!isValid()) return;
    int16_t* temp = errorRow0;
    errorRow0 = errorRow1;
    errorRow1 = errorRow2;
    errorRow2 = temp;
    memset(errorRow2, 0, rowSize * sizeof(int16_t));
  }

  void reset() {
    if (!isValid()) return;
    memset(errorRow0, 0, rowSize * sizeof(int16_t));
    memset(errorRow1, 0, rowSize * sizeof(int16_t));
    memset(errorRow2, 0, rowSize * sizeof(int16_t));
  }

 private:
  size_t rowSize{0};
  std::unique_ptr<int16_t[]> errorRows;
  int16_t* errorRow0 = nullptr;
  int16_t* errorRow1 = nullptr;
  int16_t* errorRow2 = nullptr;
};

// Atkinson dithering - distributes only 6/8 (75%) of error for cleaner results
// Error distribution pattern:
//     X  1/8 1/8
// 1/8 1/8 1/8
//     1/8
// Less error buildup = fewer artifacts than Floyd-Steinberg
class AtkinsonDitherer {
 public:
  explicit AtkinsonDitherer(int width, bool imageLevels = false) : imageLevels(imageLevels) {
    if (width <= 0) return;
    const size_t candidateRowSize = static_cast<size_t>(width) + 4;
    if (candidateRowSize > SIZE_MAX / (3 * sizeof(int16_t))) return;
    rowSize = candidateRowSize;
    errorRows = makeUniqueNoThrow<int16_t[]>(rowSize * 3);
    if (!errorRows) return;
    errorRow0 = errorRows.get();
    errorRow1 = errorRow0 + rowSize;
    errorRow2 = errorRow1 + rowSize;
  }
  // **1. EXPLICITLY DELETE THE COPY CONSTRUCTOR**
  AtkinsonDitherer(const AtkinsonDitherer& other) = delete;

  // **2. EXPLICITLY DELETE THE COPY ASSIGNMENT OPERATOR**
  AtkinsonDitherer& operator=(const AtkinsonDitherer& other) = delete;

  bool isValid() const { return errorRows != nullptr; }

  uint8_t processPixel(int gray, int x) {
    // Add accumulated error
    int adjusted = gray + (isValid() ? errorRow0[x + 2] : 0);
    if (adjusted < 0) adjusted = 0;
    if (adjusted > 255) adjusted = 255;

    // Quantize to 4 levels
    uint8_t quantized;
    int quantizedValue;
    if (imageLevels) {  // evenly spaced image tones
      if (adjusted < 43) {
        quantized = 0;
        quantizedValue = 0;
      } else if (adjusted < 128) {
        quantized = 1;
        quantizedValue = 85;
      } else if (adjusted < 213) {
        quantized = 2;
        quantizedValue = 170;
      } else {
        quantized = 3;
        quantizedValue = 255;
      }
    } else {  // fine-tuned to X4 eink display
      if (adjusted < 30) {
        quantized = 0;
        quantizedValue = 15;
      } else if (adjusted < 50) {
        quantized = 1;
        quantizedValue = 30;
      } else if (adjusted < 140) {
        quantized = 2;
        quantizedValue = 80;
      } else {
        quantized = 3;
        quantizedValue = 210;
      }
    }

    if (!isValid()) return quantized;

    // Calculate error (only distribute 6/8 = 75%)
    int error = (adjusted - quantizedValue) >> 3;  // error/8

    // Distribute 1/8 to each of 6 neighbors
    errorRow0[x + 3] += error;  // Right
    errorRow0[x + 4] += error;  // Right+1
    errorRow1[x + 1] += error;  // Bottom-left
    errorRow1[x + 2] += error;  // Bottom
    errorRow1[x + 3] += error;  // Bottom-right
    errorRow2[x + 2] += error;  // Two rows down

    return quantized;
  }

  void nextRow() {
    if (!isValid()) return;
    int16_t* temp = errorRow0;
    errorRow0 = errorRow1;
    errorRow1 = errorRow2;
    errorRow2 = temp;
    memset(errorRow2, 0, rowSize * sizeof(int16_t));
  }

  void reset() {
    if (!isValid()) return;
    memset(errorRow0, 0, rowSize * sizeof(int16_t));
    memset(errorRow1, 0, rowSize * sizeof(int16_t));
    memset(errorRow2, 0, rowSize * sizeof(int16_t));
  }

 private:
  const bool imageLevels;
  size_t rowSize{0};
  std::unique_ptr<int16_t[]> errorRows;
  int16_t* errorRow0 = nullptr;
  int16_t* errorRow1 = nullptr;
  int16_t* errorRow2 = nullptr;
};

// Floyd-Steinberg error diffusion dithering with serpentine scanning
// Serpentine scanning alternates direction each row to reduce "worm" artifacts
// Error distribution pattern (left-to-right):
//       X   7/16
// 3/16 5/16 1/16
// Error distribution pattern (right-to-left, mirrored):
// 1/16 5/16 3/16
//      7/16  X
class FloydSteinbergDitherer {
 public:
  explicit FloydSteinbergDitherer(int width, bool imageLevels = true, bool monochrome = false)
      : imageLevels(imageLevels), monochrome(monochrome) {
    if (width <= 0) return;
    rowSize = static_cast<size_t>(width) + 2;
    if (rowSize > SIZE_MAX / (2 * sizeof(int32_t))) return;
    // Two reusable error rows use 4,240 bytes at X3 width; they exceed the task stack.
    errorRows = makeUniqueNoThrow<int32_t[]>(rowSize * 2);
    if (!errorRows) return;
    errorCurRow = errorRows.get();
    errorNextRow = errorCurRow + rowSize;
    reset();
  }
  FloydSteinbergDitherer(const FloydSteinbergDitherer&) = delete;
  FloydSteinbergDitherer& operator=(const FloydSteinbergDitherer&) = delete;
  bool isValid() const { return errorRows != nullptr; }
  bool isReverseRow() const { return (rowCount & 1) != 0; }

  uint8_t processPixel(int gray, int x) {
    constexpr int unit = 1 << 16;
    int adjusted = gray * unit + (isValid() ? errorCurRow[x + 1] : 0);
    if (adjusted < 0) adjusted = 0;
    if (adjusted > 255 * unit) adjusted = 255 * unit;
    uint8_t level;
    int value;
    if (monochrome) {
      level = adjusted >= 255 * unit / 2;
      value = level * 255 * unit;
    } else if (imageLevels) {
      level = static_cast<uint8_t>((adjusted + 85 * unit / 2) / (85 * unit));
      value = level * 85 * unit;
    } else {
      if (adjusted < 30 * unit) {
        level = 0;
        value = 15 * unit;
      } else if (adjusted < 50 * unit) {
        level = 1;
        value = 30 * unit;
      } else if (adjusted < 140 * unit) {
        level = 2;
        value = 80 * unit;
      } else {
        level = 3;
        value = 210 * unit;
      }
    }
    if (!isValid()) return level;
    const int error = adjusted - value;
    if (!isReverseRow()) {
      errorCurRow[x + 2] += (error * 7) >> 4;
      errorNextRow[x] += (error * 3) >> 4;
      errorNextRow[x + 1] += (error * 5) >> 4;
      errorNextRow[x + 2] += error >> 4;
    } else {
      errorCurRow[x] += (error * 7) >> 4;
      errorNextRow[x + 2] += (error * 3) >> 4;
      errorNextRow[x + 1] += (error * 5) >> 4;
      errorNextRow[x] += error >> 4;
    }
    return level;
  }
  void nextRow() {
    if (!isValid()) return;
    auto* temp = errorCurRow;
    errorCurRow = errorNextRow;
    errorNextRow = temp;
    std::memset(errorNextRow, 0, rowSize * sizeof(int32_t));
    ++rowCount;
  }
  void reset() {
    if (!isValid()) return;
    std::memset(errorCurRow, 0, rowSize * sizeof(int32_t));
    std::memset(errorNextRow, 0, rowSize * sizeof(int32_t));
    rowCount = 0;
  }

 private:
  const bool imageLevels;
  const bool monochrome;
  int rowCount = 0;
  size_t rowSize = 0;
  std::unique_ptr<int32_t[]> errorRows;
  int32_t* errorCurRow = nullptr;
  int32_t* errorNextRow = nullptr;
};
