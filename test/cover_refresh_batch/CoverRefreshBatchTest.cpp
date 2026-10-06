#include <gtest/gtest.h>

#include "src/activities/library/CoverRefreshBatch.h"

TEST(CoverRefreshBatch, NineQuickCoversNeedFiveRefreshes) {
  CoverRefreshBatch batch;
  batch.reset(100);
  int refreshes = 0;
  for (int cover = 0; cover < 9; ++cover) {
    batch.changed();
    if (batch.take(110 + cover * 10, cover == 8)) ++refreshes;
  }
  EXPECT_EQ(refreshes, 5);
  EXPECT_FALSE(batch.take(1000, true));
}

TEST(CoverRefreshBatch, SlowCoverUpdatesImmediatelyAndPageChangeDropsOldWork) {
  CoverRefreshBatch batch;
  batch.reset(100);
  batch.changed();
  EXPECT_TRUE(batch.take(400, false));
  batch.changed();
  batch.reset(450);
  EXPECT_FALSE(batch.take(900, true));
}

TEST(CoverRefreshBatch, PendingCoverFlushesAfterMissingCoversOrClockWrap) {
  CoverRefreshBatch batch;
  batch.reset(0xfffffff0);
  batch.changed();
  EXPECT_FALSE(batch.take(10, false));
  EXPECT_TRUE(batch.take(250, false));
  batch.changed();
  EXPECT_TRUE(batch.take(251, true));
}
