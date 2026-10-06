#include "ReaderRaster.h"

#include <Logging.h>

#include <algorithm>
#include <array>
#include <cstring>

uint32_t ReaderRaster::hash(const uint8_t* bytes, const size_t count, uint32_t value) {
  for (size_t i = 0; i < count; ++i) value = (value ^ bytes[i]) * 16777619U;
  return value;
}

bool ReaderRaster::begin(const std::string& path, const Key& key) {
  cancel();
  close();
  if (!key.width || key.width % 8 || !key.height || key.height > 4096 || key.width > 4096 || key.gray > 1) return false;
  path_ = path;
  temporary_ = path + ".part";
  key_ = key;
  row_ = 0;
  failed_ = false;
  if (!Storage.openFileForWrite("RPG", temporary_, writer_)) return false;
  const uint32_t header[10] = {0,        VERSION,   key.session, key.profile, key.spine,
                               key.page, key.width, key.height,  key.gray,    BAND_ROWS};
  failed_ = writer_.write(header, sizeof(header)) != sizeof(header);
  return !failed_;
}

bool ReaderRaster::writePlane(const uint8_t* data, const size_t bytes, uint32_t& stored) {
  size_t runs = 0;
  for (size_t pos = 0; pos < bytes;) {
    size_t end = pos + 1;
    while (end < bytes && end - pos < 255 && data[end] == data[pos]) ++end;
    ++runs;
    pos = end;
  }
  if (runs * 2 >= bytes) {
    stored = static_cast<uint32_t>(bytes) | 0x80000000U;
    return writer_.write(data, bytes) == bytes;
  }
  // A 256-byte stack batch keeps small runs from causing separate SD writes.
  std::array<uint8_t, 256> batch;
  size_t fill = 0;
  stored = static_cast<uint32_t>(runs * 2);
  for (size_t pos = 0; pos < bytes;) {
    size_t end = pos + 1;
    while (end < bytes && end - pos < 255 && data[end] == data[pos]) ++end;
    batch[fill++] = static_cast<uint8_t>(end - pos);
    batch[fill++] = data[pos];
    if (fill == batch.size()) {
      if (writer_.write(batch.data(), fill) != fill) return false;
      fill = 0;
    }
    pos = end;
  }
  return !fill || writer_.write(batch.data(), fill) == fill;
}

bool ReaderRaster::append(const uint8_t* bw, const uint8_t* low, const uint8_t* high, const uint16_t rows) {
  if (!writer_ || failed_ || !bw || !low || !high || !rows || rows > BAND_ROWS || row_ + rows > key_.height)
    return false;
  const size_t bytes = static_cast<size_t>(key_.width / 8) * rows;
  const size_t start = writer_.position();
  uint32_t header[5] = {rows, 0, 0, 0, hash(high, bytes, hash(low, bytes, hash(bw, bytes)))};
  bool ok = writer_.write(header, sizeof(header)) == sizeof(header);
  ok =
      ok && writePlane(bw, bytes, header[1]) && writePlane(low, bytes, header[2]) && writePlane(high, bytes, header[3]);
  const size_t end = writer_.position();
  ok = ok && writer_.seekSet(start) && writer_.write(header, sizeof(header)) == sizeof(header) && writer_.seekSet(end);
  failed_ = !ok;
  if (ok) row_ += rows;
  return ok;
}

bool ReaderRaster::commit() {
  if (!writer_ || failed_ || row_ != key_.height) return false;
  const uint32_t magic = MAGIC;
  bool ok = writer_.seekSet(0) && writer_.write(&magic, sizeof(magic)) == sizeof(magic) && writer_.sync();
  writer_.close();
  if (ok) {
    if (Storage.exists(path_.c_str())) ok = Storage.remove(path_.c_str());
    ok = ok && Storage.rename(temporary_.c_str(), path_.c_str());
  }
  if (!ok) {
    LOG_ERR("RPG", "Could not commit finished page");
    Storage.remove(temporary_.c_str());
  }
  temporary_.clear();
  return ok;
}

void ReaderRaster::cancel() {
  writer_.close();
  if (!temporary_.empty()) Storage.remove(temporary_.c_str());
  temporary_.clear();
}

bool ReaderRaster::open(const std::string& path) {
  close();
  uint32_t header[10];
  reader_ = Storage.open(path.c_str(), O_RDONLY);
  if (!reader_) return false;
  if (reader_.read(header, sizeof(header)) != sizeof(header) || header[0] != MAGIC || header[1] != VERSION ||
      header[9] != BAND_ROWS || !header[6] || header[6] % 8 || header[6] > 4096 || !header[7] || header[7] > 4096 ||
      header[8] > 1) {
    close();
    return false;
  }
  key_ = {header[2], header[3], header[4], header[5], header[6], header[7], header[8]};
  row_ = 0;
  return true;
}

bool ReaderRaster::open(const std::string& path, const Key& key) {
  if (!open(path)) return false;
  if (key_ == key) return true;
  close();
  return false;
}

bool ReaderRaster::readPlane(uint8_t* data, const size_t bytes, const uint32_t stored) {
  if (stored & 0x80000000U) {
    return (stored & 0x7fffffffU) == bytes && reader_.read(data, bytes) == static_cast<int>(bytes);
  }
  if (!stored || stored % 2 || stored >= bytes) return false;
  std::array<uint8_t, 256> batch;
  size_t written = 0;
  for (size_t left = stored; left;) {
    const size_t count = std::min(left, batch.size());
    if (reader_.read(batch.data(), count) != static_cast<int>(count)) return false;
    for (size_t i = 0; i < count; i += 2) {
      if (!batch[i] || written + batch[i] > bytes) return false;
      memset(data + written, batch[i + 1], batch[i]);
      written += batch[i];
    }
    left -= count;
  }
  return written == bytes;
}

bool ReaderRaster::readBand(uint8_t* bw, uint8_t* low, uint8_t* high, uint16_t& rows) {
  if (!reader_ || !bw || !low || !high || atEnd()) return false;
  uint32_t header[5];
  if (reader_.read(header, sizeof(header)) != sizeof(header) || !header[0] || header[0] > BAND_ROWS ||
      row_ + header[0] > key_.height)
    return false;
  rows = static_cast<uint16_t>(header[0]);
  const size_t bytes = static_cast<size_t>(key_.width / 8) * rows;
  if (!readPlane(bw, bytes, header[1]) || !readPlane(low, bytes, header[2]) || !readPlane(high, bytes, header[3]) ||
      header[4] != hash(high, bytes, hash(low, bytes, hash(bw, bytes))))
    return false;
  row_ += rows;
  return !atEnd() || reader_.position() == reader_.size();
}

bool ReaderRaster::rewind() {
  row_ = 0;
  return reader_ && reader_.seekSet(HEADER_BYTES);
}

void ReaderRaster::close() { reader_.close(); }
