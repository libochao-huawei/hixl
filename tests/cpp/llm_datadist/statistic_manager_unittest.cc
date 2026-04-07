/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <vector>
#include <gtest/gtest.h>

#include "adxl/statistic_manager.h"

namespace adxl {
namespace {
constexpr char kChannelId[] = "test";
constexpr uint64_t kCost = 100;
}  // namespace
class StatisticManagerUTest : public ::testing::Test {
 protected:
  void SetUp() override {
    StatisticManager::GetInstance().RegisterChannel(kChannelId);
  }
  void TearDown() override {
    StatisticManager::GetInstance().RemoveChannel(kChannelId);
  }
};

TEST_F(StatisticManagerUTest, TestDump) {
  StatisticManager::GetInstance().UpdateBufferTransferCost(kChannelId, kCost);
  StatisticManager::GetInstance().Dump();
}

TEST_F(StatisticManagerUTest, TestDirectTransferDump) {
  StatisticManager::GetInstance().UpdateDirectTransferCost(kChannelId, kCost);
  StatisticManager::GetInstance().Dump();
}

TEST_F(StatisticManagerUTest, TestConnectStatisticSnapshot) {
  StatisticManager::GetInstance().UpdateConnectTotalCost(kChannelId, kCost * 4U);
  StatisticManager::GetInstance().UpdateTcpConnectCost(kChannelId, kCost);
  StatisticManager::GetInstance().UpdateHcclTotalCost(kChannelId, kCost * 3U);
  StatisticManager::GetInstance().UpdateHcclCommInitCost(kChannelId, kCost);
  StatisticManager::GetInstance().UpdateHcclCommBindMemCost(kChannelId, kCost);
  StatisticManager::GetInstance().UpdateHcclCommPrepareCost(kChannelId, kCost);

  const auto snapshot = StatisticManager::GetInstance().GetStatisticInfoSnapshot(kChannelId);
  EXPECT_EQ(snapshot.connect_statistic_info.connect_total.times, 1UL);
  EXPECT_EQ(snapshot.connect_statistic_info.connect_total.total_cost, kCost * 4U);
  EXPECT_EQ(snapshot.connect_statistic_info.tcp_connect.total_cost, kCost);
  EXPECT_EQ(snapshot.connect_statistic_info.hccl_total.total_cost, kCost * 3U);
  EXPECT_EQ(snapshot.connect_statistic_info.hccl_comm_init.total_cost, kCost);
  EXPECT_EQ(snapshot.connect_statistic_info.hccl_comm_bind_mem.total_cost, kCost);
  EXPECT_EQ(snapshot.connect_statistic_info.hccl_comm_prepare.total_cost, kCost);
}

TEST_F(StatisticManagerUTest, TestRemoveChannelAndFabricMemDump) {
  StatisticManager::GetInstance().SetEnableUseFabricMem(true);
  StatisticManager::GetInstance().UpdateConnectTotalCost(kChannelId, kCost);
  StatisticManager::GetInstance().UpdateTcpConnectCost(kChannelId, kCost);
  StatisticManager::GetInstance().UpdateFabricMemTransferCost(kChannelId, kCost);
  StatisticManager::GetInstance().Dump();
  StatisticManager::GetInstance().RemoveChannel(kChannelId);

  const auto snapshot = StatisticManager::GetInstance().GetStatisticInfoSnapshot(kChannelId);
  EXPECT_EQ(snapshot.connect_statistic_info.connect_total.times, 0UL);
  StatisticManager::GetInstance().SetEnableUseFabricMem(false);
}

}  // namespace adxl
