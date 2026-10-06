#include <gtest/gtest.h>
#include <zlib.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

#include "uzlib.h"

namespace {
struct Input {
  TINF_DATA data{};
  const uint8_t* end;
  unsigned chunk;

  static int refill(TINF_DATA* data) {
    auto& input = *reinterpret_cast<Input*>(data);
    if (data->source == input.end) return -1;
    const auto remaining = static_cast<unsigned>(input.end - data->source);
    data->source_limit = data->source + std::min(remaining, input.chunk);
    return *data->source++;
  }
};

struct Result {
  int status;
  std::vector<uint8_t> output;
};

Result inflate(const std::vector<uint8_t>& compressed, size_t capacity, unsigned inputChunk, unsigned outputChunk,
               bool ring) {
  Input input{};
  std::vector<uint8_t> dictionary(ring ? 32768 : 0);
  std::vector<uint8_t> output(capacity + 16, 0xa5);
  auto& state = input.data;
  uzlib_uncompress_init(&state, ring ? dictionary.data() : nullptr, dictionary.size());
  input.end = compressed.data() + compressed.size();
  input.chunk = inputChunk;
  state.source = compressed.data();
  state.source_limit = compressed.data() + std::min(compressed.size(), static_cast<size_t>(inputChunk));
  state.source_read_cb = Input::refill;
  state.dest = state.dest_start = output.data();
  int status = TINF_OK;
  while (status == TINF_OK && static_cast<size_t>(state.dest - output.data()) < capacity) {
    const auto remaining = capacity - static_cast<size_t>(state.dest - output.data());
    state.dest_limit = state.dest + std::min(remaining, static_cast<size_t>(outputChunk));
    status = uzlib_uncompress(&state);
  }
  EXPECT_TRUE(std::all_of(output.begin() + capacity, output.end(), [](uint8_t byte) { return byte == 0xa5; }));
  output.resize(state.dest - output.data());
  return {status, std::move(output)};
}

std::vector<uint8_t> compress(const std::vector<uint8_t>& data, int level, int strategy) {
  z_stream stream{};
  EXPECT_EQ(deflateInit2(&stream, level, Z_DEFLATED, -15, 8, strategy), Z_OK);
  std::vector<uint8_t> compressed(deflateBound(&stream, data.size()));
  stream.next_in = const_cast<uint8_t*>(data.data());
  stream.avail_in = data.size();
  stream.next_out = compressed.data();
  stream.avail_out = compressed.size();
  EXPECT_EQ(deflate(&stream, Z_FINISH), Z_STREAM_END);
  compressed.resize(stream.total_out);
  EXPECT_EQ(deflateEnd(&stream), Z_OK);
  return compressed;
}
}  // namespace

TEST(UzlibInflate, RejectsEmptyDistanceAlphabetBeforeWritingOutput) {
  // Valid EOB and length-257 codes with a distance alphabet that has no codes.
  const std::vector<uint8_t> compressed{0x0d, 0xc0, 0x81, 0x08, 0, 0, 0, 0, 0x20, 0x7f, 0xeb, 0x2f, 0, 0};
  for (bool ring : {false, true}) {
    for (unsigned inputChunk : {1, 7, 4096}) {
      for (unsigned outputChunk : {1, 17, 4096}) {
        SCOPED_TRACE(::testing::Message() << ring << ':' << inputChunk << ':' << outputChunk);
        const auto result = inflate(compressed, 16, inputChunk, outputChunk, ring);
        EXPECT_EQ(result.status, TINF_DATA_ERROR);
        EXPECT_TRUE(result.output.empty());
      }
    }
  }
}

TEST(UzlibInflate, DecodesStoredFixedAndDynamicBlocksAcrossChunkBoundaries) {
  uint32_t random = 161;
  for (size_t length : {0, 1, 2, 3, 7, 8, 31, 255, 256, 1024, 8192, 65536}) {
    std::vector<uint8_t> data(length);
    for (int sample = 0; sample < 3; ++sample) {
      for (size_t i = 0; i < length; ++i) {
        random = random * 1664525u + 1013904223u;
        data[i] = sample == 0   ? 'A'
                  : sample == 1 ? static_cast<uint8_t>(random >> 24)
                                : "The window lets daylight in.\n"[i % 28];
      }
      for (int level : {0, 1, 6, 9}) {
        for (int strategy : {Z_DEFAULT_STRATEGY, Z_FIXED, Z_HUFFMAN_ONLY, Z_RLE}) {
          SCOPED_TRACE(::testing::Message() << length << ':' << sample << ':' << level << ':' << strategy);
          const auto compressed = compress(data, level, strategy);
          for (bool ring : {false, true}) {
            for (unsigned inputChunk : {1, 7, 4096}) {
              for (unsigned outputChunk : {1, 17, 4096}) {
                const auto result = inflate(compressed, length + 1, inputChunk, outputChunk, ring);
                EXPECT_EQ(result.status, TINF_DONE);
                EXPECT_EQ(result.output, data);
              }
            }
          }
        }
      }
    }
  }
}
