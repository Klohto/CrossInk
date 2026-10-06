#include <gtest/gtest.h>

#include "HalSpiBus.h"

namespace {
int mutexToken;
int depth;
int takes;
int gives;
BaseType_t takeResult = pdTRUE;
SemaphoreHandle_t takenHandle;
SemaphoreHandle_t givenHandle;

class SpiBusOwnership : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_EQ(depth, 0);
    takes = gives = 0;
    takenHandle = givenHandle = nullptr;
    takeResult = pdTRUE;
  }
  void TearDown() override { EXPECT_EQ(depth, 0); }
};
}  // namespace

SemaphoreHandle_t xSemaphoreCreateRecursiveMutex() { return &mutexToken; }

BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t mutex, TickType_t wait) {
  EXPECT_EQ(mutex, &mutexToken);
  EXPECT_EQ(wait, portMAX_DELAY);
  takenHandle = mutex;
  ++takes;
  if (takeResult == pdTRUE) ++depth;
  return takeResult;
}

BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t mutex) {
  EXPECT_EQ(mutex, &mutexToken);
  EXPECT_GT(depth, 0);
  givenHandle = mutex;
  ++gives;
  --depth;
  return pdTRUE;
}

TEST_F(SpiBusOwnership, ReleasesTheAcquiredHandleAtScopeExit) {
  {
    HalSpiBus::Lock lock;
    EXPECT_EQ(depth, 1);
    EXPECT_EQ(takes, 1);
    EXPECT_EQ(gives, 0);
  }
  EXPECT_EQ(gives, 1);
  EXPECT_EQ(givenHandle, takenHandle);
}

TEST_F(SpiBusOwnership, NestedGuardsKeepTheOuterAcquisition) {
  {
    HalSpiBus::Lock outer;
    {
      HalSpiBus::Lock inner;
      EXPECT_EQ(depth, 2);
    }
    EXPECT_EQ(depth, 1);
    EXPECT_EQ(gives, 1);
  }
  EXPECT_EQ(takes, 2);
  EXPECT_EQ(gives, 2);
}

TEST_F(SpiBusOwnership, FailedAcquisitionLeavesTheMutexUnowned) {
  takeResult = pdFALSE;
  {
    HalSpiBus::Lock lock;
    EXPECT_EQ(takes, 1);
    EXPECT_EQ(depth, 0);
  }
  EXPECT_EQ(gives, 0);
  EXPECT_EQ(givenHandle, nullptr);
}
