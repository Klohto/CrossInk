#include <Epub/parsers/HtmlEventCache.h>
#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {
using Step = HtmlEventCache::Step;
void prepare() {
  HtmlEventCache writer;
  ASSERT_TRUE(writer.begin("chapter.html", 5000, 7));
  const char* attributes[] = {"style", "font-weight:bold", "id", "heading", nullptr};
  writer.recordStart("p", attributes);
  writer.recordText("Hello", 5);
  writer.recordChunk(1024, Step::More);
  writer.recordEntity("&nbsp;", 6);
  writer.recordText("world", 5);
  writer.recordEnd("p");
  writer.recordChunk(5000, Step::Done);
  ASSERT_TRUE(writer.finish());
}

TEST(HtmlEventCache, ReplaysAttributesEntitiesAndSourceProgress) {
  Storage.reset();
  prepare();
  HtmlEventCache reader;
  ASSERT_TRUE(reader.begin("chapter.html", 5000, 7));
  ASSERT_TRUE(reader.replaying());
  std::vector<std::string> events;
  HtmlEventCache::Callbacks callbacks{
      &events,
      [](void* ctx, const char* name, const char** attrs) {
        auto& events = *static_cast<std::vector<std::string>*>(ctx);
        events.push_back(std::string("start:") + name);
        while (*attrs) {
          events.push_back(*attrs);
          ++attrs;
        }
      },
      [](void* ctx, const char* name) {
        static_cast<std::vector<std::string>*>(ctx)->push_back(std::string("end:") + name);
      },
      [](void* ctx, const char* text, int len) {
        static_cast<std::vector<std::string>*>(ctx)->emplace_back(text, len);
      },
      [](void* ctx, const char* text, int len) {
        static_cast<std::vector<std::string>*>(ctx)->emplace_back(text, len);
      }};
  uint32_t offset = 0;
  EXPECT_EQ(reader.replay(callbacks, offset), Step::More);
  EXPECT_EQ(offset, 1024u);
  EXPECT_EQ(reader.replay(callbacks, offset), Step::Done);
  EXPECT_EQ(offset, 5000u);
  EXPECT_EQ(events, (std::vector<std::string>{"start:p", "style", "font-weight:bold", "id", "heading", "Hello",
                                              "&nbsp;", "world", "end:p"}));
}

TEST(HtmlEventCache, RejectsTruncatedAndCorruptEventsBeforeAnyReplay) {
  Storage.reset();
  prepare();
  const auto original = Storage.bytes("chapter.html.mid");
  for (size_t i = 0; i < original.size(); ++i) {
    auto damaged = original;
    damaged[i] ^= 1;
    Storage.put("chapter.html.mid", std::move(damaged));
    HtmlEventCache reader;
    ASSERT_TRUE(reader.begin("chapter.html", 5000, 7));
    EXPECT_FALSE(reader.replaying()) << i;
  }
  for (size_t cut = 0; cut < original.size(); ++cut) {
    Storage.put("chapter.html.mid", {original.begin(), original.begin() + cut});
    HtmlEventCache reader;
    ASSERT_TRUE(reader.begin("chapter.html", 5000, 7));
    EXPECT_FALSE(reader.replaying()) << cut;
  }
}

TEST(HtmlEventCache, KeepsIncompleteOversizedAndFailedPreparationOutOfTheCache) {
  Storage.reset();
  {
    HtmlEventCache writer;
    ASSERT_TRUE(writer.begin("chapter.html", 5000, 7));
    writer.recordText("hello", 5);
    writer.recordChunk(1024, Step::More);
    EXPECT_FALSE(writer.finish());
  }
  EXPECT_FALSE(Storage.exists("chapter.html.mid"));
  {
    HtmlEventCache writer;
    ASSERT_TRUE(writer.begin("chapter.html", 5000, 7));
    const std::string oversized(5000, 'a');
    writer.recordText(oversized.data(), oversized.size());
    writer.recordChunk(5000, Step::Done);
    EXPECT_FALSE(writer.finish());
  }
  EXPECT_FALSE(Storage.exists("chapter.html.mid"));
  prepare();
  HtmlEventCache changed;
  ASSERT_TRUE(changed.begin("chapter.html", 5001, 7));
  EXPECT_FALSE(changed.replaying());
}
}  // namespace
