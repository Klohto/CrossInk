#pragma once

#include <BufferedFile.h>
#include <HalStorage.h>
#include <Memory.h>

#include <cstdint>
#include <memory>
#include <string>

// Font-independent content between ZIP/HTML and page layout. Events retain
// original element attributes, text, entities and source chunk boundaries.
// Reflow replays the existing layout callbacks with the current render spec.
class HtmlEventCache {
 public:
  static constexpr uint32_t VERSION = 1;
  static constexpr uint32_t MAX_EVENT_BYTES = 4096;
  static constexpr uint32_t MAX_ATTRIBUTES = 64;
  enum class Type : uint32_t { Start = 1, End, Text, Entity, Chunk };
  enum class Step { More, Done, Malformed, Error };
  struct Callbacks {
    void* context;
    void (*start)(void*, const char*, const char**);
    void (*end)(void*, const char*);
    void (*text)(void*, const char*, int);
    void (*entity)(void*, const char*, int);
  };
  ~HtmlEventCache() { cancel(); }
  bool begin(const std::string& htmlPath, uint32_t sourceBytes, uint32_t sourceModified, bool allowRecord = true);
  bool replaying() const { return static_cast<bool>(input_); }
  void recordStart(const char* name, const char** attributes);
  void recordEnd(const char* name);
  void recordText(const char* data, uint32_t bytes);
  void recordEntity(const char* data, uint32_t bytes);
  void recordChunk(uint32_t sourceOffset, Step result);
  Step replay(const Callbacks& callbacks, uint32_t& sourceOffset);
  bool finish();
  void cancel();

 private:
  struct Record {
    uint32_t type, bytes, hash;
  };
  static constexpr uint32_t MAGIC = 0x3144494d;  // MID1
  static constexpr size_t HEADER_BYTES = 4 * sizeof(uint32_t);
  HalFile file_;
  std::unique_ptr<serialization::BufferedFileWriter> output_;
  std::unique_ptr<serialization::BufferedFileReader> input_;
  std::unique_ptr<uint8_t[]> event_;
  std::unique_ptr<const char*[]> attributes_;
  std::string path_, temporary_;
  uint32_t sourceBytes_ = 0;
  bool complete_ = false;
  bool failed_ = false;
  bool validate();
  bool validPayload(const Record& record) const;
  bool readRecord(Record& record);
  void record(Type type, const void* data, uint32_t bytes);
};
