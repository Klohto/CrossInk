#include "HtmlEventCache.h"

#include <Epub/ReaderRaster.h>
#include <Logging.h>

#include <cstring>

bool HtmlEventCache::begin(const std::string& htmlPath, const uint32_t sourceBytes, const uint32_t sourceModified,
                           const bool allowRecord) {
  cancel();
  path_ = htmlPath + ".mid";
  temporary_ = path_ + ".part";
  sourceBytes_ = sourceBytes;
  failed_ = complete_ = false;
  // One 4 KiB event buffer and one 1 KiB I/O buffer per parse. Both are reused
  // for the whole chapter; large attributes retain the XML fallback.
  event_ = makeUniqueNoThrow<uint8_t[]>(MAX_EVENT_BYTES);
  if (!event_) return false;
  const uint32_t expected[4] = {MAGIC, VERSION, sourceBytes, sourceModified};
  if (Storage.openFileForRead("MID", path_, file_)) {
    uint32_t header[4];
    if (file_.read(header, sizeof(header)) == sizeof(header) && memcmp(header, expected, sizeof(header)) == 0) {
      input_ = makeUniqueNoThrow<serialization::BufferedFileReader>(file_, 1024);
      if (input_ && validate() && input_->seek(HEADER_BYTES)) {
        attributes_ = makeUniqueNoThrow<const char*[]>(MAX_ATTRIBUTES * 2 + 1);
        if (attributes_) {
          LOG_DBG("MID", "Replaying prepared chapter: %u source bytes", sourceBytes);
          return true;
        }
      }
    }
    input_.reset();
    file_.close();
    Storage.remove(path_.c_str());
  }
  if (!allowRecord) {
    cancel();
    return false;
  }
  if (!Storage.openFileForWrite("MID", temporary_, file_)) return false;
  uint32_t header[4] = {0, VERSION, sourceBytes, sourceModified};
  if (file_.write(header, sizeof(header)) != sizeof(header)) {
    cancel();
    return false;
  }
  output_ = makeUniqueNoThrow<serialization::BufferedFileWriter>(file_, 1024);
  if (!output_) {
    cancel();
    return false;
  }
  return true;
}

bool HtmlEventCache::validPayload(const Record& record) const {
  if (record.type == static_cast<uint32_t>(Type::Start)) {
    if (record.bytes < 2) return false;
    size_t pos = 0;
    unsigned strings = 0;
    while (pos < record.bytes) {
      const void* end = memchr(event_.get() + pos, '\0', record.bytes - pos);
      if (!end) return false;
      pos = static_cast<const uint8_t*>(end) - event_.get() + 1;
      ++strings;
    }
    return strings && strings % 2 == 1 && strings <= MAX_ATTRIBUTES * 2 + 1 && event_[0];
  }
  if (record.type == static_cast<uint32_t>(Type::End)) {
    return record.bytes > 1 && event_[record.bytes - 1] == 0 && !memchr(event_.get(), '\0', record.bytes - 1);
  }
  if (record.type == static_cast<uint32_t>(Type::Text) || record.type == static_cast<uint32_t>(Type::Entity)) {
    return record.bytes > 0;
  }
  if (record.type == static_cast<uint32_t>(Type::Chunk)) {
    if (record.bytes != 8) return false;
    uint32_t chunk[2];
    memcpy(chunk, event_.get(), sizeof(chunk));
    return chunk[0] <= sourceBytes_ && chunk[1] <= 2;
  }
  return false;
}

bool HtmlEventCache::readRecord(Record& record) {
  return input_ && input_->read(&record, sizeof(record)) == sizeof(record) && record.bytes <= MAX_EVENT_BYTES &&
         input_->read(event_.get(), record.bytes) == record.bytes &&
         ReaderRaster::hash(event_.get(), record.bytes, 2166136261U ^ record.type) == record.hash &&
         validPayload(record);
}

bool HtmlEventCache::validate() {
  uint32_t offset = 0;
  bool done = false;
  while (input_->position() < file_.size()) {
    Record record;
    if (done || !readRecord(record)) return false;
    if (record.type == static_cast<uint32_t>(Type::Chunk)) {
      uint32_t chunk[2];
      memcpy(chunk, event_.get(), sizeof(chunk));
      if (chunk[0] < offset) return false;
      offset = chunk[0];
      done = chunk[1] != 0;
    }
  }
  return done && input_->position() == file_.size();
}

void HtmlEventCache::record(const Type type, const void* data, const uint32_t bytes) {
  if (!output_ || failed_) return;
  if (!bytes || bytes > MAX_EVENT_BYTES) {
    failed_ = true;
    return;
  }
  const Record header{
      static_cast<uint32_t>(type), bytes,
      ReaderRaster::hash(static_cast<const uint8_t*>(data), bytes, 2166136261U ^ static_cast<uint32_t>(type))};
  output_->write(&header, sizeof(header));
  output_->write(data, bytes);
}

void HtmlEventCache::recordStart(const char* name, const char** attributes) {
  if (!output_ || failed_) return;
  size_t bytes = strlen(name) + 1;
  if (bytes > MAX_EVENT_BYTES) {
    failed_ = true;
    return;
  }
  memcpy(event_.get(), name, bytes);
  unsigned count = 0;
  if (attributes) {
    for (const char** value = attributes; *value; ++value) {
      const size_t length = strlen(*value) + 1;
      if (++count > MAX_ATTRIBUTES * 2 || bytes + length > MAX_EVENT_BYTES) {
        failed_ = true;
        return;
      }
      memcpy(event_.get() + bytes, *value, length);
      bytes += length;
    }
  }
  record(Type::Start, event_.get(), static_cast<uint32_t>(bytes));
}
void HtmlEventCache::recordEnd(const char* name) { record(Type::End, name, strlen(name) + 1); }
void HtmlEventCache::recordText(const char* data, const uint32_t bytes) { record(Type::Text, data, bytes); }
void HtmlEventCache::recordEntity(const char* data, const uint32_t bytes) {
  // Other Expat default events have no effect on this layout engine.
  if (bytes >= 3 && data[0] == '&' && data[bytes - 1] == ';') record(Type::Entity, data, bytes);
}
void HtmlEventCache::recordChunk(const uint32_t sourceOffset, const Step result) {
  if (result == Step::Error) {
    failed_ = true;
    return;
  }
  const uint32_t chunk[2] = {sourceOffset, result == Step::Done ? 1U : result == Step::Malformed ? 2U : 0U};
  record(Type::Chunk, chunk, sizeof(chunk));
  complete_ = result != Step::More;
}

HtmlEventCache::Step HtmlEventCache::replay(const Callbacks& callbacks, uint32_t& sourceOffset) {
  while (input_ && input_->position() < file_.size()) {
    Record record;
    if (!readRecord(record)) return Step::Error;
    const char* data = reinterpret_cast<const char*>(event_.get());
    switch (static_cast<Type>(record.type)) {
      case Type::Start: {
        const char* cursor = data + strlen(data) + 1;
        unsigned count = 0;
        while (cursor < data + record.bytes) {
          attributes_[count++] = cursor;
          cursor += strlen(cursor) + 1;
        }
        attributes_[count] = nullptr;
        callbacks.start(callbacks.context, data, attributes_.get());
        break;
      }
      case Type::End:
        callbacks.end(callbacks.context, data);
        break;
      case Type::Text:
        callbacks.text(callbacks.context, data, record.bytes);
        break;
      case Type::Entity:
        callbacks.entity(callbacks.context, data, record.bytes);
        break;
      case Type::Chunk: {
        uint32_t chunk[2];
        memcpy(chunk, event_.get(), sizeof(chunk));
        sourceOffset = chunk[0];
        return chunk[1] == 2 ? Step::Malformed : chunk[1] == 1 ? Step::Done : Step::More;
      }
    }
  }
  return Step::Error;
}

bool HtmlEventCache::finish() {
  if (input_) {
    cancel();
    return true;
  }
  if (!output_) return false;
  const bool written = output_->flush();
  output_.reset();
  const uint32_t magic = MAGIC;
  bool ok = written && !failed_ && complete_ && file_.seekSet(0) &&
            file_.write(&magic, sizeof(magic)) == sizeof(magic) && file_.sync();
  file_.close();
  if (ok) {
    if (Storage.exists(path_.c_str())) ok = Storage.remove(path_.c_str());
    ok = ok && Storage.rename(temporary_.c_str(), path_.c_str());
  }
  if (!ok) Storage.remove(temporary_.c_str());
  temporary_.clear();
  return ok;
}

void HtmlEventCache::cancel() {
  output_.reset();
  input_.reset();
  file_.close();
  if (!temporary_.empty()) Storage.remove(temporary_.c_str());
  temporary_.clear();
  attributes_.reset();
  event_.reset();
}
