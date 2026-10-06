#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Logging.h>

#include <atomic>
#include <mutex>
#include <string>

/**
 * @brief Non-template core of PersistableStore.
 *
 * All ArduinoJson parse/serialize machinery is instantiated once here (in
 * PersistableStore.cpp) instead of in every store's translation unit. GCC
 * emits the JSON serializer/parser templates as local .isra clones per TU
 * (~0.5KB each), so keeping serializeJson/deserializeJson out of the stores
 * is what makes the abstraction flash-neutral.
 */
class PersistableStoreBase {
 protected:
  PersistableStoreBase() = default;
  ~PersistableStoreBase() = default;

  // Serializes saveToFile/loadFromFile against each other across FreeRTOS
  // tasks, so the JSON snapshot cannot tear mid-serialize and two concurrent
  // saves cannot write their documents out of order. Concurrent saves are
  // reachable: the web server task saves settings while the main task can too.
  //
  // It is deliberately held across the SD write. That is safe only because the
  // read path does NOT take it — derived stores build their snapshots (e.g.
  // CrossPointSettings::statusBarSpec) unlocked. If you ever lock this mutex on
  // a read path, you put it on the render path and stall rendering behind SD
  // I/O, and you create a storeMutex/storageMutex ordering hazard. Don't.
  mutable std::mutex storeMutex;

  // fromJson() implementations call this (instead of saveToFile()) when the
  // on-disk JSON used a legacy shape that was upgraded in memory.
  // loadFromFile() performs the save after releasing storeMutex; calling
  // saveToFile() from inside fromJson() would deadlock on storeMutex.
  void requestResave() { resaveRequested = true; }

  bool resaveRequested = false;

  // Tracks whether this store has had a load attempted, including attempts
  // that fail because the file is missing or unreadable. This is a per-store
  // lifecycle marker, not synchronization: ensureLoaded() must not be used to
  // make arbitrary cross-task access to a store safe.
  mutable bool loadAttempted_ = false;

  // Call under storeMutex. Each store writes to one fixed path. Keep only a
  // checksum and length, so a save does not retain a second JSON document.
  bool writeDocIfChanged(const char* path, const JsonDocument& doc, bool atomic = false) const;
  void invalidateSavedDocument() const { savedDocumentValid_ = false; }

 private:
  mutable uint32_t savedDocumentCrc_ = 0;
  mutable size_t savedDocumentLength_ = 0;
  mutable bool savedDocumentValid_ = false;

 public:
  // Public so non-store JSON files (e.g. per-book bookmarks) can reuse them
  // instead of instantiating serializeJson/deserializeJson in their own TU —
  // that per-TU duplication is exactly what this class exists to prevent.

  // Serializes doc and writes it to path (ensures /.crosspoint exists). Logs on failure.
  static bool writeDocToFile(const char* path, const JsonDocument& doc);

  // Writes through path.tmp and keeps path.bak until replacement succeeds. readDocFromFile()
  // recovers that backup after an interrupted replacement.
  static bool writeDocToFileAtomically(const char* path, const JsonDocument& doc);

  // Reads path and parses it into doc. Returns false silently when the file
  // does not exist (expected on first boot); logs on read/parse failure.
  static bool readDocFromFile(const char* path, JsonDocument& doc);
};

/**
 * @brief Base class for persistable singletons using CRTP.
 *
 * Derived classes must provide:
 * - A private default constructor
 * - friend class PersistableStore<Derived>;
 * - static const char* getFilePath();
 * - void toJson(JsonDocument& doc) const;
 * - bool fromJson(JsonVariantConst doc);
 *
 * Note for implementers: read string values as `const char*` (e.g.
 * `obj["name"] | ""`), never as `| std::string("")` — ArduinoJson's
 * std::string converter drags a per-TU copy of the whole JSON serializer
 * into flash via its serializeJson fallback.
 *
 * Concurrency: saveToFile/loadFromFile lock storeMutex, so toJson/fromJson
 * always run under it. fromJson must signal legacy-shape upgrades with
 * requestResave(), never by calling saveToFile() directly (deadlock).
 */
template <typename T>
class PersistableStore : public PersistableStoreBase {
 protected:
  PersistableStore() = default;
  ~PersistableStore() = default;

 public:
  // Delete copy constructor and assignment
  PersistableStore(const PersistableStore&) = delete;
  PersistableStore& operator=(const PersistableStore&) = delete;

  // Load the store once before a caller uses it. The load attempt is marked by
  // loadFromFile() before any file or JSON work begins, so fromJson() can call
  // guarded mutators without recursively reloading the store.
  void ensureLoaded() const {
    if (loadAttempted_) return;
    const_cast<T*>(static_cast<const T*>(this))->loadFromFile();
  }

  static T& getInstance() {
    // ESP32-C3 calls the runtime guard on each access to a local singleton.
    // Publish the completed object once. Later reads need one atomic load.
    static std::atomic<T*> ready{nullptr};
    if (T* cached = ready.load(std::memory_order_acquire)) return *cached;
    static T instance;
    ready.store(&instance, std::memory_order_release);
    return instance;
  }

  bool saveToFile() const {
    std::lock_guard<std::mutex> lock(storeMutex);
    JsonDocument doc;
    static_cast<const T*>(this)->toJson(doc);
    return writeDocIfChanged(T::getFilePath(), doc);
  }

  bool loadFromFile() {
    loadAttempted_ = true;
    bool ok;
    bool doResave;
    {
      std::lock_guard<std::mutex> lock(storeMutex);
      invalidateSavedDocument();
      resaveRequested = false;
      JsonDocument doc;
      if (!readDocFromFile(T::getFilePath(), doc)) {
        return false;
      }
      ok = static_cast<T*>(this)->fromJson(doc.as<JsonVariantConst>());
      // Read the flag under the lock that guards the fromJson() that set it.
      doResave = resaveRequested;
      resaveRequested = false;
    }
    // Deliberately outside the lock: saveToFile() takes storeMutex itself.
    if (ok && doResave && !saveToFile()) {
      LOG_ERR("PERSIST", "Failed to resave %s after format update", T::getFilePath());
    }
    return ok;
  }
};
