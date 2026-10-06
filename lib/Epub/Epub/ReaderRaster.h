#pragma once

#include <HalStorage.h>

#include <cstddef>
#include <cstdint>
#include <string>

// A bounded disk cache of finished page pixels. Each band holds the BW base
// and two complete gray planes. Byte runs reduce SD traffic on white margins.
// Layout and interaction policy remain in the reader activity.
class ReaderRaster {
 public:
  static constexpr uint32_t VERSION = 1;
  static constexpr uint16_t BAND_ROWS = 24;
  struct Key {
    uint32_t session = 0;
    uint32_t profile = 0;
    uint32_t spine = 0;
    uint32_t page = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t gray = 0;
    bool operator==(const Key&) const = default;
  };
  ~ReaderRaster() {
    cancel();
    close();
  }
  bool begin(const std::string& path, const Key& key);
  bool append(const uint8_t* bw, const uint8_t* low, const uint8_t* high, uint16_t rows);
  bool commit();
  void cancel();
  bool open(const std::string& path, const Key& key);
  bool open(const std::string& path);
  bool readBand(uint8_t* bw, uint8_t* low, uint8_t* high, uint16_t& rows);
  bool rewind();
  void close();
  const Key& key() const { return key_; }
  bool atEnd() const { return row_ == key_.height; }
  // Small deterministic codec helpers are shared by the host tests.
  static uint32_t hash(const uint8_t* bytes, size_t count, uint32_t value = 2166136261U);

 private:
  static constexpr uint32_t MAGIC = 0x31504752;  // RPG1, little endian
  static constexpr size_t HEADER_BYTES = 10 * sizeof(uint32_t);
  HalFile writer_, reader_;
  std::string path_, temporary_;
  Key key_;
  uint32_t row_ = 0;
  bool failed_ = false;
  bool writePlane(const uint8_t* data, size_t bytes, uint32_t& stored);
  bool readPlane(uint8_t* data, size_t bytes, uint32_t stored);
};
