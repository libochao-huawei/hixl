/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <gtest/gtest.h>

#define private public
#include "comm_statistic_manager.h"
#undef private

namespace llm {
namespace {

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

}  // namespace
}  // namespace llm
