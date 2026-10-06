#include <HalStorage.h>
#include <PersistableStore.h>
#include <gtest/gtest.h>

namespace {
constexpr char STORE_PATH[] = "/.crosspoint/recent.json";

JsonDocument documentWithPath(const char* path) {
  JsonDocument doc;
  doc["books"][0]["path"] = path;
  return doc;
}

const char* firstPath(const JsonDocument& doc) { return doc["books"][0]["path"] | ""; }

class CachedStore : public PersistableStore<CachedStore> {
 public:
  std::string value = "initial";
  static const char* getFilePath() { return STORE_PATH; }
  void toJson(JsonDocument& doc) const { doc["value"] = value; }
  bool fromJson(JsonVariantConst doc) {
    value = doc["value"] | "";
    return true;
  }
  bool saveAtomically() const {
    std::lock_guard<std::mutex> lock(storeMutex);
    JsonDocument doc;
    toJson(doc);
    return writeDocIfChanged(STORE_PATH, doc, true);
  }
};
}  // namespace

class PersistableStoreAtomicTest : public testing::Test {
 protected:
  void SetUp() override { Storage.reset(); }
};

TEST_F(PersistableStoreAtomicTest, RestoresOriginalWhenReplacementRenameFails) {
  ASSERT_TRUE(PersistableStoreBase::writeDocToFile(STORE_PATH, documentWithPath("/old.epub")));
  Storage.failNextRenameFrom(std::string(STORE_PATH) + ".tmp");

  EXPECT_FALSE(PersistableStoreBase::writeDocToFileAtomically(STORE_PATH, documentWithPath("/new.epub")));

  JsonDocument loaded;
  ASSERT_TRUE(PersistableStoreBase::readDocFromFile(STORE_PATH, loaded));
  EXPECT_STREQ(firstPath(loaded), "/old.epub");
  EXPECT_FALSE(Storage.exists((std::string(STORE_PATH) + ".tmp").c_str()));
  EXPECT_FALSE(Storage.exists((std::string(STORE_PATH) + ".bak").c_str()));
}

TEST_F(PersistableStoreAtomicTest, RecoversBackupAfterInterruptedReplacement) {
  Storage.put(std::string(STORE_PATH) + ".bak", R"({"books":[{"path":"/old.epub"}]})");

  JsonDocument loaded;
  ASSERT_TRUE(PersistableStoreBase::readDocFromFile(STORE_PATH, loaded));
  EXPECT_STREQ(firstPath(loaded), "/old.epub");
  EXPECT_TRUE(Storage.exists(STORE_PATH));
  EXPECT_FALSE(Storage.exists((std::string(STORE_PATH) + ".bak").c_str()));
}

TEST_F(PersistableStoreAtomicTest, ReplacesStoreAndRemovesTemporaryFiles) {
  ASSERT_TRUE(PersistableStoreBase::writeDocToFile(STORE_PATH, documentWithPath("/old.epub")));
  ASSERT_TRUE(PersistableStoreBase::writeDocToFileAtomically(STORE_PATH, documentWithPath("/new.epub")));

  JsonDocument loaded;
  ASSERT_TRUE(PersistableStoreBase::readDocFromFile(STORE_PATH, loaded));
  EXPECT_STREQ(firstPath(loaded), "/new.epub");
  EXPECT_FALSE(Storage.exists((std::string(STORE_PATH) + ".tmp").c_str()));
  EXPECT_FALSE(Storage.exists((std::string(STORE_PATH) + ".bak").c_str()));
}

TEST_F(PersistableStoreAtomicTest, RepeatedSavesWriteOnceForBothWriteModes) {
  for (bool atomic : {false, true}) {
    Storage.reset();
    CachedStore store;
    for (int i = 0; i < 100; ++i) ASSERT_TRUE(atomic ? store.saveAtomically() : store.saveToFile());
    EXPECT_EQ(Storage.writeAttempts, 1);
    store.value = "changed";
    ASSERT_TRUE(atomic ? store.saveAtomically() : store.saveToFile());
    EXPECT_EQ(Storage.writeAttempts, 2);
  }
}

TEST_F(PersistableStoreAtomicTest, SameLengthChangesAndRemovedFileAreSaved) {
  CachedStore store;
  ASSERT_TRUE(store.saveAtomically());
  store.value = "revised";
  ASSERT_TRUE(store.saveAtomically());
  EXPECT_EQ(Storage.writeAttempts, 2);
  ASSERT_TRUE(Storage.remove(STORE_PATH));
  ASSERT_TRUE(store.saveAtomically());
  EXPECT_EQ(Storage.writeAttempts, 3);
  JsonDocument doc;
  ASSERT_TRUE(PersistableStoreBase::readDocFromFile(STORE_PATH, doc));
  EXPECT_STREQ(doc["value"], "revised");
}

TEST_F(PersistableStoreAtomicTest, FailedWriteAndFailedReplacementCanRetry) {
  CachedStore store;
  Storage.failNextWrite();
  EXPECT_FALSE(store.saveAtomically());
  ASSERT_TRUE(store.saveAtomically());
  EXPECT_EQ(Storage.writeAttempts, 2);
  store.value = "changed";
  Storage.failNextRenameFrom(std::string(STORE_PATH) + ".tmp");
  EXPECT_FALSE(store.saveAtomically());
  ASSERT_TRUE(store.saveAtomically());
  EXPECT_EQ(Storage.writeAttempts, 4);
  JsonDocument doc;
  ASSERT_TRUE(PersistableStoreBase::readDocFromFile(STORE_PATH, doc));
  EXPECT_STREQ(doc["value"], "changed");
}

TEST_F(PersistableStoreAtomicTest, ReloadInvalidatesRememberedSnapshot) {
  CachedStore store;
  ASSERT_TRUE(store.saveToFile());
  Storage.put(STORE_PATH, R"({"value":"external"})");
  ASSERT_TRUE(store.loadFromFile());
  EXPECT_EQ(store.value, "external");
  store.value = "initial";
  ASSERT_TRUE(store.saveToFile());
  EXPECT_EQ(Storage.writeAttempts, 2);
  JsonDocument doc;
  ASSERT_TRUE(PersistableStoreBase::readDocFromFile(STORE_PATH, doc));
  EXPECT_STREQ(doc["value"], "initial");
}
