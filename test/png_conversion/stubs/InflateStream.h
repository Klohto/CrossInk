#pragma once
#include <zlib.h>

#include <cstddef>
#include <cstdint>
class InflateStream {
 public:
  using FillFn = size_t (*)(void*, const uint8_t**);
  ~InflateStream() {
    if (active) inflateEnd(&stream);
  }
  bool init(bool) {
    active = inflateInit(&stream) == Z_OK;
    return active;
  }
  void setFill(FillFn fn, void* ctx) {
    fill = fn;
    context = ctx;
  }
  void setZlibWrapped() {}
  bool read(uint8_t* destination, size_t count) {
    stream.next_out = destination;
    stream.avail_out = count;
    while (stream.avail_out) {
      if (!stream.avail_in) {
        const uint8_t* data = nullptr;
        const size_t available = fill(context, &data);
        if (!available) return false;
        stream.next_in = const_cast<uint8_t*>(data);
        stream.avail_in = available;
      }
      const auto beforeIn = stream.avail_in, beforeOut = stream.avail_out;
      const int result = inflate(&stream, Z_NO_FLUSH);
      if (result == Z_STREAM_END) return stream.avail_out == 0;
      if (result != Z_OK || (beforeIn == stream.avail_in && beforeOut == stream.avail_out)) return false;
    }
    return true;
  }

 private:
  z_stream stream{};
  FillFn fill = nullptr;
  void* context = nullptr;
  bool active = false;
};
