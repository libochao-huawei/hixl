/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <limits>
#include <mutex>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#define private public
#include "comm_statistic_manager.h"
#undef private

namespace llm {
namespace {

class StartGate {
 public:
  explicit StartGate(const size_t participant_count) : remaining_(participant_count) {}

  void Wait() {
    std::unique_lock<std::mutex> lock(mutex_);
    if (--remaining_ == 0U) {
      open_ = true;
      lock.unlock();
      condition_.notify_all();
      return;
    }
    condition_.wait(lock, [this]() { return open_; });
  }

 private:
  size_t remaining_;
  bool open_{false};
  std::mutex mutex_;
  std::condition_variable condition_;
};

TEST(CommStatisticManagerTest, AddBatchPutCostUpdatesExtrema) {
  auto &manager = CommStatisticManager::GetInstance();
  manager.Reset();

  manager.AddBatchPutCost(100U);
  manager.AddBatchPutCost(10U);

  const auto &stats = manager.send_statistic_info_;
  EXPECT_EQ(stats.batch_put_times, 2U);
  EXPECT_EQ(stats.batch_put_total_cost, 110U);
  EXPECT_EQ(stats.batch_put_min_cost, 10U);
  EXPECT_EQ(stats.batch_put_max_cost, 100U);

  manager.Reset();
}

TEST(CommStatisticManagerTest, UpdateCostKeepsConcurrentExtrema) {
  constexpr size_t kThreadCount = 16U;
  constexpr size_t kRounds = 64U;
  constexpr uint64_t kMinCost = 10U;
  constexpr uint64_t kMaxCost = 1000000U;
  constexpr uint64_t kExpectedTotal = (kThreadCount / 2U) * (kMinCost + kMaxCost);

  for (size_t round = 0U; round < kRounds; ++round) {
    std::atomic<uint64_t> total_times{0U};
    std::atomic<uint64_t> min_cost{std::numeric_limits<uint64_t>::max()};
    std::atomic<uint64_t> max_cost{0U};
    std::atomic<uint64_t> total_cost{0U};
    StartGate start_gate(kThreadCount);
    std::vector<std::thread> workers;
    workers.reserve(kThreadCount);

    for (size_t index = 0U; index < kThreadCount; ++index) {
      const uint64_t cost = (index % 2U == 0U) ? kMinCost : kMaxCost;
      workers.emplace_back([&, cost]() {
        start_gate.Wait();
        std::this_thread::yield();
        CommStatisticManager::UpdateCost(cost, total_times, min_cost, max_cost, total_cost);
      });
    }
    for (auto &worker : workers) {
      worker.join();
    }

    EXPECT_EQ(total_times.load(std::memory_order_relaxed), kThreadCount);
    EXPECT_EQ(total_cost.load(std::memory_order_relaxed), kExpectedTotal);
    EXPECT_EQ(min_cost.load(std::memory_order_relaxed), kMinCost);
    EXPECT_EQ(max_cost.load(std::memory_order_relaxed), kMaxCost);
  }
}

}  // namespace
}  // namespace llm
