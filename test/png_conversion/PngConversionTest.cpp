#include <HalDisplay.h>
#include <PngToBmpConverter.h>
#include <Print.h>
#include <gtest/gtest.h>
#include <zlib.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace {
struct Format {
  uint8_t color;
  uint8_t depth;
  uint8_t channels;
};

constexpr std::array<Format, 15> formats{{{0, 1, 1},
                                          {0, 2, 1},
                                          {0, 4, 1},
                                          {0, 8, 1},
                                          {0, 16, 1},
                                          {2, 8, 3},
                                          {2, 16, 3},
                                          {3, 1, 1},
                                          {3, 2, 1},
                                          {3, 4, 1},
                                          {3, 8, 1},
                                          {4, 8, 2},
                                          {4, 16, 2},
                                          {6, 8, 4},
                                          {6, 16, 4}}};

struct Output : Print {
  std::vector<uint8_t> bytes;
  size_t write(uint8_t value) override {
    bytes.push_back(value);
    return 1;
  }
};

void append32(std::vector<uint8_t>& out, uint32_t value) {
  for (int shift : {24, 16, 8, 0}) out.push_back(value >> shift);
}

void appendChunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& data) {
  append32(out, data.size());
  const size_t crcStart = out.size();
  out.insert(out.end(), type, type + 4);
  out.insert(out.end(), data.begin(), data.end());
  append32(out, crc32(0, out.data() + crcStart, out.size() - crcStart));
}

uint8_t predictor(uint8_t left, uint8_t above, uint8_t diagonal, uint8_t filter) {
  switch (filter) {
    case 1:
      return left;
    case 2:
      return above;
    case 3:
      return (static_cast<int>(left) + above) / 2;
    case 4: {
      const int p = static_cast<int>(left) + above - diagonal;
      const int a = std::abs(p - left), b = std::abs(p - above), c = std::abs(p - diagonal);
      if (a <= b && a <= c) return left;
      return b <= c ? above : diagonal;
    }
    default:
      return 0;
  }
}

std::vector<std::vector<uint8_t>> makeRows(Format format, int width, int height) {
  std::vector<std::vector<uint8_t>> rows;
  for (int y = 0; y < height; ++y) {
    std::vector<uint8_t> row((width * format.channels * format.depth + 7) / 8);
    for (int x = 0; x < width; ++x) {
      for (int channel = 0; channel < format.channels; ++channel) {
        const uint8_t value = x * 23 + y * 11 + channel * 37;
        if (format.depth < 8) {
          row[x * format.depth / 8] |= (value >> (8 - format.depth)) << (8 - format.depth - x * format.depth % 8);
        } else if (format.depth == 16) {
          const int index = (x * format.channels + channel) * 2;
          row[index] = value;
          row[index + 1] = value * 7;
        } else {
          row[x * format.channels + channel] = value;
        }
      }
    }
    rows.push_back(std::move(row));
  }
  return rows;
}

std::vector<uint8_t> makePng(Format format, int width, const std::vector<std::vector<uint8_t>>& rows, uint8_t filter) {
  std::vector<uint8_t> png{137, 80, 78, 71, 13, 10, 26, 10};
  std::vector<uint8_t> header;
  append32(header, width);
  append32(header, rows.size());
  header.insert(header.end(), {format.depth, format.color, 0, 0, 0});
  appendChunk(png, "IHDR", header);
  if (format.color == 3) {
    std::vector<uint8_t> palette;
    for (int index = 0; index < 1 << format.depth; ++index) {
      palette.push_back(index * 23);
      palette.push_back(index * 71);
      palette.push_back(index * 53);
    }
    appendChunk(png, "PLTE", palette);
  }
  std::vector<uint8_t> raw;
  std::vector<uint8_t> previous(rows.front().size());
  const size_t bpp = std::max(1, format.channels * format.depth / 8);
  for (const auto& row : rows) {
    raw.push_back(filter);
    for (size_t index = 0; index < row.size(); ++index) {
      const uint8_t left = index >= bpp ? row[index - bpp] : 0;
      const uint8_t diagonal = index >= bpp ? previous[index - bpp] : 0;
      raw.push_back(row[index] - predictor(left, previous[index], diagonal, filter));
    }
    previous = row;
  }
  uLongf compressedSize = compressBound(raw.size());
  std::vector<uint8_t> compressed(compressedSize);
  if (compress2(compressed.data(), &compressedSize, raw.data(), raw.size(), 1) != Z_OK) return {};
  compressed.resize(compressedSize);
  appendChunk(png, "IDAT", compressed);
  appendChunk(png, "IEND", {});
  return png;
}

bool convert(const std::vector<uint8_t>& png, Output& output, int width, int height, bool oneBit, bool option) {
  FsFile input(png);
  if (oneBit) return PngToBmpConverter::pngFileTo1BitBmpStreamWithSize(input, output, width, height, option);
  display.height = width;
  display.width = height;
  return PngToBmpConverter::pngFileToBmpStream(input, output, option);
}
}  // namespace

class PngConversion : public testing::TestWithParam<Format> {};

TEST_P(PngConversion, AllFiltersPreservePixelsThroughCropAndScale) {
  const auto format = GetParam();
  const auto rows = makeRows(format, 12, 20);
  const auto unfiltered = makePng(format, 12, rows, 0);
  for (const auto target : {std::pair{8, 8}, std::pair{24, 40}, std::pair{12, 20}, std::pair{8, 20}}) {
    for (bool oneBit : {false, true}) {
      for (bool option : {false, true}) {
        Output expected;
        ASSERT_TRUE(convert(unfiltered, expected, target.first, target.second, oneBit, option));
        ASSERT_FALSE(expected.bytes.empty());
        for (uint8_t filter = 1; filter <= 4; ++filter) {
          Output result;
          ASSERT_TRUE(convert(makePng(format, 12, rows, filter), result, target.first, target.second, oneBit, option));
          EXPECT_EQ(result.bytes, expected.bytes) << "filter=" << unsigned(filter) << " target=" << target.first << "x"
                                                  << target.second << " oneBit=" << oneBit << " option=" << option;
        }
      }
    }
  }
}

INSTANTIATE_TEST_SUITE_P(ValidColorDepthPairs, PngConversion, testing::ValuesIn(formats), [](const auto& info) {
  return "Color" + std::to_string(info.param.color) + "Depth" + std::to_string(info.param.depth);
});

TEST(PngConversion, RejectsInvalidDepthBeforeWritingOutput) {
  const auto rows = makeRows({0, 8, 1}, 12, 20);
  for (uint8_t color : {0, 2, 3, 4, 6}) {
    for (uint8_t depth : {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 15, 16, 17, 255}) {
      const bool valid = depth == 8 || (depth == 16 && color != 3) ||
                         ((color == 0 || color == 3) && (depth == 1 || depth == 2 || depth == 4));
      if (valid) continue;
      auto png = makePng({0, 8, 1}, 12, rows, 0);
      png[24] = depth;
      png[25] = color;
      const uint32_t crc = crc32(0, png.data() + 12, 17);
      for (int byte = 0; byte < 4; ++byte) png[29 + byte] = crc >> (24 - byte * 8);
      Output result;
      EXPECT_FALSE(convert(png, result, 8, 8, true, false))
          << "color=" << unsigned(color) << " depth=" << unsigned(depth);
      EXPECT_TRUE(result.bytes.empty());
    }
  }
}

TEST(PngConversion, AllRgbTriplesMatchTheWeightedGrayscaleControl) {
  constexpr int width = 2048, height = 2048;
  for (unsigned block = 0; block < 4; ++block) {
    std::vector<std::vector<uint8_t>> rgbRows(height, std::vector<uint8_t>(width * 3));
    std::vector<std::vector<uint8_t>> grayRows(height, std::vector<uint8_t>(width));
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        const unsigned value = block * width * height + y * width + x;
        const uint8_t red = value >> 16, green = value >> 8, blue = value;
        rgbRows[y][x * 3] = red;
        rgbRows[y][x * 3 + 1] = green;
        rgbRows[y][x * 3 + 2] = blue;
        grayRows[y][x] = (red * 25 + green * 50 + blue * 25) / 100;
      }
    }
    Output rgb, gray;
    ASSERT_TRUE(convert(makePng({2, 8, 3}, width, rgbRows, 0), rgb, width, height, true, false));
    ASSERT_TRUE(convert(makePng({0, 8, 1}, width, grayRows, 0), gray, width, height, true, false));
    EXPECT_EQ(rgb.bytes, gray.bytes) << "RGB block=" << block;
  }
}
