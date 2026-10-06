#include <GfxRenderer.h>
#include <gtest/gtest.h>

#include <array>

#include "src/util/InputWorkPriority.h"
#include "src/util/ReaderWakeFrame.h"

namespace {
constexpr char PATH[] = "/.crosspoint/reader_wake.bin";
void book(const char* path) { Storage.put(path, {1, 2, 3}); }
TEST(ReaderWakeFrame, ShowsSavedPageAndConsumesOnlyMatchingPixels) {
  Storage.reset();
  book("one.epub");
  GfxRenderer saved;
  saved.frame[12] = 51;
  ASSERT_TRUE(ReaderWakeFrame::save(saved, "one.epub"));
  ASSERT_TRUE(ReaderWakeFrame::preflight("one.epub"));
  GfxRenderer visible;
  ASSERT_TRUE(ReaderWakeFrame::restore(visible, "one.epub"));
  EXPECT_EQ(visible.frame, saved.frame);
  EXPECT_EQ(visible.bwRefreshes, 1u);
  EXPECT_TRUE(ReaderWakeFrame::wasRestored());
  EXPECT_FALSE(ReaderWakeFrame::consumeIfSame(visible.frame.data(), visible.frame.size(), true));
  ASSERT_TRUE(ReaderWakeFrame::restore(visible, "one.epub"));
  EXPECT_TRUE(ReaderWakeFrame::consumeIfSame(visible.frame.data(), visible.frame.size(), false));
  EXPECT_FALSE(ReaderWakeFrame::wasRestored());
  ASSERT_TRUE(ReaderWakeFrame::restore(visible, "one.epub"));
  visible.frame[0] ^= 1;
  EXPECT_FALSE(ReaderWakeFrame::consumeIfSame(visible.frame.data(), visible.frame.size(), false));
}
TEST(ReaderWakeFrame, RejectsTruncationChangedBookAndDamagedPixels) {
  Storage.reset();
  book("two.epub");
  book("other.epub");
  GfxRenderer saved;
  saved.frame[1] = 17;
  ASSERT_TRUE(ReaderWakeFrame::save(saved, "two.epub"));
  const auto bytes = Storage.bytes(PATH);
  EXPECT_FALSE(ReaderWakeFrame::preflight("other.epub"));
  Storage.put("two.epub", {4, 5});
  EXPECT_FALSE(ReaderWakeFrame::preflight("two.epub"));
  book("two.epub");
  for (size_t cut = 0; cut < bytes.size(); ++cut) {
    Storage.put(PATH, {bytes.begin(), bytes.begin() + cut});
    EXPECT_FALSE(ReaderWakeFrame::preflight("two.epub")) << cut;
  }
  auto bad = bytes;
  bad.back() ^= 1;
  Storage.put(PATH, bad);
  GfxRenderer visible;
  EXPECT_FALSE(ReaderWakeFrame::restore(visible, "two.epub"));
  EXPECT_FALSE(Storage.exists(PATH));
}
TEST(ReaderWakeFrame, ShowsGrayFromValidatedBandsAndFallsBackAfterDamage) {
  Storage.reset();
  book("gray.epub");
  GfxRenderer saved;
  saved.frame[0] = 0;
  ReaderRaster::Key key{1, 2, 3, 4, 64, 35, 1};
  ReaderRaster writer;
  ASSERT_TRUE(writer.begin("gray.rpg", key));
  std::array<uint8_t, 8 * ReaderRaster::BAND_ROWS> bw{}, low{}, high{};
  bw.fill(255);
  low.fill(0x55);
  high.fill(0xaa);
  for (int y = 0; y < 35; y += ReaderRaster::BAND_ROWS)
    ASSERT_TRUE(writer.append(bw.data(), low.data(), high.data(), std::min<int>(ReaderRaster::BAND_ROWS, 35 - y)));
  ASSERT_TRUE(writer.commit());
  ASSERT_TRUE(ReaderWakeFrame::save(saved, "gray.epub", "gray.rpg", &key));
  GfxRenderer visible;
  ASSERT_TRUE(ReaderWakeFrame::restore(visible, "gray.epub"));
  EXPECT_EQ(visible.grayRefreshes, 1u);
  EXPECT_EQ(visible.bwRefreshes, 0u);
  EXPECT_EQ(visible.low[0], 0);
  EXPECT_EQ(visible.high[0], 0);
  EXPECT_EQ(visible.low[1], 0x55);
  EXPECT_EQ(visible.high[1], 0xaa);
  EXPECT_TRUE(ReaderWakeFrame::consumeIfSame(visible.frame.data(), visible.frame.size(), true));
  auto bad = Storage.bytes("gray.rpg");
  bad.back() ^= 1;
  Storage.put("gray.rpg", bad);
  ASSERT_TRUE(ReaderWakeFrame::restore(visible, "gray.epub"));
  EXPECT_EQ(visible.bwRefreshes, 1u);
  EXPECT_FALSE(ReaderWakeFrame::consumeIfSame(visible.frame.data(), visible.frame.size(), true));
}
TEST(ReaderWakeFrame, FailedSaveRemovesStaleWakePage) {
  Storage.reset();
  book("failed.epub");
  GfxRenderer saved;
  saved.frame[0] = 123;
  ASSERT_TRUE(ReaderWakeFrame::save(saved, "failed.epub"));
  saved.frame[0] ^= 1;
  Storage.failWrite = true;
  EXPECT_FALSE(ReaderWakeFrame::save(saved, "failed.epub"));
  EXPECT_FALSE(Storage.exists(PATH));
}
TEST(ReaderWakeFrame, DiscardRemovesAnUnfinishedPageSnapshot) {
  Storage.reset();
  book("discard.epub");
  GfxRenderer saved;
  saved.frame[0] = 91;
  ASSERT_TRUE(ReaderWakeFrame::save(saved, "discard.epub"));
  ASSERT_TRUE(ReaderWakeFrame::restore(saved, "discard.epub"));
  ReaderWakeFrame::discard();
  EXPECT_FALSE(ReaderWakeFrame::wasRestored());
  EXPECT_FALSE(ReaderWakeFrame::preflight("discard.epub"));
}
TEST(ReaderWakeFrame, KeepsFooterChangesFromPaintingAgainAndChecksThePageKey) {
  for (const auto orientation : {GfxRenderer::Portrait, GfxRenderer::PortraitInverted, GfxRenderer::LandscapeClockwise,
                                 GfxRenderer::LandscapeCounterClockwise}) {
    Storage.reset();
    book("footer.epub");
    GfxRenderer saved;
    saved.orientation = orientation;
    saved.frame[81] = 73;
    ReaderRaster::Key key{1, 2, 3, 4, 64, 35, 0};
    ASSERT_TRUE(ReaderWakeFrame::save(saved, "footer.epub", nullptr, &key, 7));
    GfxRenderer visible;
    ASSERT_TRUE(ReaderWakeFrame::restore(visible, "footer.epub"));
    const size_t footerByte = orientation == GfxRenderer::Portrait                    ? 7
                              : orientation == GfxRenderer::LandscapeCounterClockwise ? visible.frame.size() - 1
                                                                                      : 0;
    visible.frame[footerByte] ^= orientation == GfxRenderer::Portrait ? 1 : 128;
    EXPECT_TRUE(ReaderWakeFrame::consumeIfSame(visible.frame.data(), visible.frame.size(), false, &key));
    ASSERT_TRUE(ReaderWakeFrame::restore(visible, "footer.epub"));
    visible.frame[footerByte] ^= orientation == GfxRenderer::Portrait ? 1 : 128;
    auto other = key;
    other.page++;
    EXPECT_FALSE(ReaderWakeFrame::consumeIfSame(visible.frame.data(), visible.frame.size(), false, &other));
    ASSERT_TRUE(ReaderWakeFrame::restore(visible, "footer.epub"));
    visible.frame[81] ^= 1;
    EXPECT_FALSE(ReaderWakeFrame::consumeIfSame(visible.frame.data(), visible.frame.size(), false, &key));
  }
}
TEST(InputWorkPriority, WaitsForQuietAndCancelsThePriorTicket) {
  using namespace InputWorkPriority;
  sawInput = false;
  epoch = 0;
  EXPECT_TRUE(canPrepare(0));
  Ticket old;
  notify(UINT32_MAX - 100);
  EXPECT_TRUE(old.interrupted());
  EXPECT_FALSE(canPrepare(100));
  EXPECT_TRUE(canPrepare(149));
  Ticket next;
  EXPECT_FALSE(next.interrupted());
  notify(150);
  EXPECT_TRUE(next.interrupted());
}
TEST(ReaderWakeFrame, RequiresTheSameProfileWhenTheBwPixelsMatch) {
  Storage.reset();
  book("profile.epub");
  GfxRenderer saved;
  ReaderRaster::Key key{1, 2, 3, 4, 64, 35, 0};
  ASSERT_TRUE(ReaderWakeFrame::save(saved, "profile.epub", nullptr, &key, 7));
  GfxRenderer visible;
  ASSERT_TRUE(ReaderWakeFrame::restore(visible, "profile.epub"));
  auto changed = key;
  changed.profile++;
  EXPECT_FALSE(ReaderWakeFrame::consumeIfSame(visible.frame.data(), visible.frame.size(), false, &changed));
  ASSERT_TRUE(ReaderWakeFrame::restore(visible, "profile.epub"));
  EXPECT_TRUE(ReaderWakeFrame::consumeIfSame(visible.frame.data(), visible.frame.size(), false, &key));
}
}  // namespace
