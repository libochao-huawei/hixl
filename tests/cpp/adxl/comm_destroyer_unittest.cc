/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING
 * BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE. See LICENSE in the root of
 * the software repository for the full text of the License.
 */

#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

#include "adxl/comm_destroyer.h"
#include "depends/llm_datadist/src/data_cache_engine_test_helper.h"
#include "depends/mmpa/src/mmpa_stub.h"

namespace adxl {
namespace {
// HCCL stub that counts destroy/unbind invocations so the test can observe what the async worker did.
class CountingHcclStub : public llm::HcclApiStub {
 public:
  HcclResult HcclCommDestroy(HcclComm comm) override {
    (void)comm;
    destroy_count_.fetch_add(1);
    return HCCL_SUCCESS;
  }
  HcclResult HcclCommUnbindMem(HcclComm comm, void *memHandle) override {
    (void)comm;
    (void)memHandle;
    unbind_count_.fetch_add(1);
    return HCCL_SUCCESS;
  }
  std::atomic<int32_t> destroy_count_{0};
  std::atomic<int32_t> unbind_count_{0};
};

HcclComm MakeFakeComm(uintptr_t v) {
  return reinterpret_cast<HcclComm>(v);
}

bool WaitForCount(const std::atomic<int32_t> &counter, int32_t expected, int64_t timeout_ms) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  while (std::chrono::steady_clock::now() < deadline) {
    if (counter.load() >= expected) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return counter.load() >= expected;
}
}  // namespace

class CommDestroyerUnitTest : public ::testing::Test {
 protected:
  void SetUp() override {
    llm::MockMmpaForHcclApi::Install();
    llm::AutoCommResRuntimeMock::Install();
    llm::HcclAdapter::GetInstance().Initialize();
    llm::AutoCommResRuntimeMock::SetDevice(0);
    auto stub = std::make_unique<CountingHcclStub>();
    counting_stub_ = stub.get();
    llm::HcclApiStub::SetStub(std::move(stub));
  }

  void TearDown() override {
    llm::HcclAdapter::GetInstance().Finalize();
    llm::MockMmpaForHcclApi::Reset();
    llm::AutoCommResRuntimeMock::Reset();
    counting_stub_ = nullptr;
  }

  CountingHcclStub *counting_stub_{nullptr};
};

TEST_F(CommDestroyerUnitTest, DestroysCommAfterDelay) {
  CommDestroyer destroyer;
  destroyer.SetDestroyDelay(50);
  ASSERT_EQ(destroyer.Initialize(nullptr), SUCCESS);
  destroyer.Enqueue(MakeFakeComm(0x1234U), {});
  EXPECT_TRUE(WaitForCount(counting_stub_->destroy_count_, 1, 2000));
  destroyer.Finalize();
  EXPECT_EQ(counting_stub_->destroy_count_.load(), 1);
}

TEST_F(CommDestroyerUnitTest, DoesNotDestroyBeforeDelayThenFinalizeDrains) {
  CommDestroyer destroyer;
  destroyer.SetDestroyDelay(600000);  // effectively never within the test window
  ASSERT_EQ(destroyer.Initialize(nullptr), SUCCESS);
  destroyer.Enqueue(MakeFakeComm(0x1U), {});
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  EXPECT_EQ(counting_stub_->destroy_count_.load(), 0);
  // Finalize must drain everything immediately, ignoring the remaining delay, and join the worker.
  destroyer.Finalize();
  EXPECT_EQ(counting_stub_->destroy_count_.load(), 1);
}

TEST_F(CommDestroyerUnitTest, UnbindsBoundMemoryBeforeDestroy) {
  CommDestroyer destroyer;
  destroyer.SetDestroyDelay(10);
  ASSERT_EQ(destroyer.Initialize(nullptr), SUCCESS);
  int dummy0 = 0;
  int dummy1 = 0;
  destroyer.Enqueue(MakeFakeComm(0x2U), {static_cast<void *>(&dummy0), static_cast<void *>(&dummy1)});
  EXPECT_TRUE(WaitForCount(counting_stub_->destroy_count_, 1, 2000));
  destroyer.Finalize();
  EXPECT_EQ(counting_stub_->unbind_count_.load(), 2);
  EXPECT_EQ(counting_stub_->destroy_count_.load(), 1);
}

TEST_F(CommDestroyerUnitTest, EnqueueNullCommIsNoop) {
  CommDestroyer destroyer;
  destroyer.SetDestroyDelay(10);
  ASSERT_EQ(destroyer.Initialize(nullptr), SUCCESS);
  destroyer.Enqueue(nullptr, {});
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  destroyer.Finalize();
  EXPECT_EQ(counting_stub_->destroy_count_.load(), 0);
}

TEST_F(CommDestroyerUnitTest, DrainPendingExpeditesAndBlocksUntilDestroyed) {
  CommDestroyer destroyer;
  destroyer.SetDestroyDelay(600000);  // would not fire on its own within the test window
  ASSERT_EQ(destroyer.Initialize(nullptr), SUCCESS);
  int dummy = 0;
  destroyer.Enqueue(MakeFakeComm(0x77U), {static_cast<void *>(&dummy)});
  EXPECT_EQ(counting_stub_->destroy_count_.load(), 0);
  // DrainPending must not wait out the 600s delay; it returns only after the destroy actually completed.
  const auto start = std::chrono::steady_clock::now();
  destroyer.DrainPending();
  const auto elapsed_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
  EXPECT_EQ(counting_stub_->destroy_count_.load(), 1);
  EXPECT_EQ(counting_stub_->unbind_count_.load(), 1);
  EXPECT_LT(elapsed_ms, 5000);
  destroyer.Finalize();
}

TEST_F(CommDestroyerUnitTest, DrainPendingWithNothingPendingReturnsImmediately) {
  CommDestroyer destroyer;
  ASSERT_EQ(destroyer.Initialize(nullptr), SUCCESS);
  destroyer.DrainPending();  // must not hang when there is nothing to destroy
  destroyer.Finalize();
  EXPECT_EQ(counting_stub_->destroy_count_.load(), 0);
}
}  // namespace adxl
