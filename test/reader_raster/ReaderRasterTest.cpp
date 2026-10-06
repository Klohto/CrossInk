#include <Epub/ReaderRaster.h>
#include <gtest/gtest.h>

#include <array>
#include <vector>

namespace {
const ReaderRaster::Key KEY{1, 2, 3, 4, 792, 35, 1};
constexpr size_t BAND = 99 * ReaderRaster::BAND_ROWS;

void createPage() {
  ReaderRaster writer;
  ASSERT_TRUE(writer.begin("page.rpg", KEY));
  std::array<uint8_t, BAND> bw{}, low{}, high{};
  for (int y = 0; y < 35; y += ReaderRaster::BAND_ROWS) {
    const int rows = std::min<int>(ReaderRaster::BAND_ROWS, 35 - y);
    for (size_t i = 0; i < BAND; ++i) {
      bw[i] = i < 700 ? 255 : static_cast<uint8_t>(i + y);
      low[i] = static_cast<uint8_t>(i * 71 + y);
      high[i] = 255;
    }
    ASSERT_TRUE(writer.append(bw.data(), low.data(), high.data(), rows));
  }
  ASSERT_TRUE(writer.commit());
}

TEST(ReaderRaster, CompressesMarginsAndRestoresEachBandExactly) {
  Storage.reset();
  createPage();
  EXPECT_LT(Storage.bytes("page.rpg").size(), 99u * 35 * 3);
  ReaderRaster reader;
  ASSERT_TRUE(reader.open("page.rpg", KEY));
  std::array<uint8_t, BAND> bw{}, low{}, high{};
  for (int y = 0; y < 35; y += ReaderRaster::BAND_ROWS) {
    uint16_t rows = 0;
    ASSERT_TRUE(reader.readBand(bw.data(), low.data(), high.data(), rows));
    ASSERT_EQ(rows, std::min<int>(ReaderRaster::BAND_ROWS, 35 - y));
    for (size_t i = 0; i < 99u * rows; ++i) {
      EXPECT_EQ(bw[i], i < 700 ? 255 : static_cast<uint8_t>(i + y));
      EXPECT_EQ(low[i], static_cast<uint8_t>(i * 71 + y));
      EXPECT_EQ(high[i], 255);
    }
  }
  EXPECT_TRUE(reader.atEnd());
  ASSERT_TRUE(reader.rewind());
  EXPECT_FALSE(reader.atEnd());
}

TEST(ReaderRaster, RejectsEveryTruncationAndChangesToPayload) {
  Storage.reset();
  createPage();
  const auto original = Storage.bytes("page.rpg");
  std::array<uint8_t, BAND> bw{}, low{}, high{};
  for (size_t cut = 0; cut < original.size(); ++cut) {
    Storage.put("bad.rpg", {original.begin(), original.begin() + cut});
    ReaderRaster reader;
    bool ok = reader.open("bad.rpg", KEY);
    while (ok && !reader.atEnd()) {
      uint16_t rows;
      ok = reader.readBand(bw.data(), low.data(), high.data(), rows);
    }
    EXPECT_FALSE(ok) << cut;
  }
  for (size_t i = 60; i < original.size(); i += 17) {
    auto corrupt = original;
    corrupt[i] ^= 1;
    Storage.put("bad.rpg", std::move(corrupt));
    ReaderRaster reader;
    bool ok = reader.open("bad.rpg", KEY);
    while (ok && !reader.atEnd()) {
      uint16_t rows;
      ok = reader.readBand(bw.data(), low.data(), high.data(), rows);
    }
    EXPECT_FALSE(ok) << i;
  }
}

TEST(ReaderRaster, PartialAndFailedWritesKeepTheAcceptedPage) {
  Storage.reset();
  createPage();
  const auto original = Storage.bytes("page.rpg");
  ReaderRaster writer;
  ASSERT_TRUE(writer.begin("page.rpg", KEY));
  EXPECT_FALSE(writer.commit());
  writer.cancel();
  EXPECT_EQ(Storage.bytes("page.rpg"), original);
  ASSERT_TRUE(writer.begin("page.rpg", KEY));
  Storage.failWritesAt("page.rpg.part", 55);
  std::array<uint8_t, BAND> data{};
  EXPECT_FALSE(writer.append(data.data(), data.data(), data.data(), 16));
  writer.cancel();
  EXPECT_EQ(Storage.bytes("page.rpg"), original);
  auto different = KEY;
  different.profile++;
  EXPECT_FALSE(writer.open("page.rpg", different));
}
}  // namespace
