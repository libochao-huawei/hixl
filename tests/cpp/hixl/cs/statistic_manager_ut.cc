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
#include <string>
#include "cs/statistic_manager.h"

namespace hixl {
namespace {
constexpr uint64_t kCost = 100UL;
constexpr uint64_t kBytes = 4096UL;
constexpr uint64_t kSubmitCost = 40UL;
constexpr uint64_t kWaitCost = 60UL;
constexpr uint64_t kOpDescCount = 2UL;
}  // namespace

class HixlCSStatisticManagerUTest : public ::testing::Test {
 protected:
  void SetUp() override {
    client_channel_id_ = HixlCSStatisticManager::GetClientChannelId("127.0.0.1:26000");
    server_channel_id_ = HixlCSStatisticManager::GetServerChannelId("fd:5");
    auto &manager = HixlCSStatisticManager::GetInstance();
    manager.RegisterChannel(client_channel_id_);
    manager.RegisterChannel(server_channel_id_);
  }

  void TearDown() override {
    auto &manager = HixlCSStatisticManager::GetInstance();
    manager.RemoveChannel(client_channel_id_);
    manager.RemoveChannel(server_channel_id_);
  }

  std::string client_channel_id_;
  std::string server_channel_id_;
};

TEST_F(HixlCSStatisticManagerUTest, GetSnapshotForConnectStage) {
  auto &manager = HixlCSStatisticManager::GetInstance();
  manager.UpdateStageCost(client_channel_id_, StatisticStage::kConnectTotal, kCost * 4U);
  manager.UpdateStageCost(client_channel_id_, StatisticStage::kTcpConnect, kCost);
  manager.UpdateStageCost(client_channel_id_, StatisticStage::kMatchEndpoint, kCost);
  manager.UpdateStageCost(client_channel_id_, StatisticStage::kGetRemoteMemTotal, kCost);
  manager.UpdateStageCost(client_channel_id_, StatisticStage::kImportRemoteMem, kCost);
  manager.UpdateStageCost(client_channel_id_, StatisticStage::kCreateChannelReq, kCost);
  manager.UpdateStageCost(client_channel_id_, StatisticStage::kLocalCreateChannel, kCost);
  manager.UpdateStageCost(client_channel_id_, StatisticStage::kWaitCreateChannelResp, kCost);

  const auto snapshot = manager.GetSnapshot(client_channel_id_);
  EXPECT_EQ(snapshot.connect.connect_total.times, 1UL);
  EXPECT_EQ(snapshot.connect.connect_total.total_cost, kCost * 4U);
  EXPECT_EQ(snapshot.connect.tcp_connect.total_cost, kCost);
  EXPECT_EQ(snapshot.connect.match_endpoint.total_cost, kCost);
  EXPECT_EQ(snapshot.connect.get_remote_mem_total.total_cost, kCost);
  EXPECT_EQ(snapshot.connect.import_remote_mem.total_cost, kCost);
  EXPECT_EQ(snapshot.connect.create_channel_req.total_cost, kCost);
  EXPECT_EQ(snapshot.connect.local_create_channel.total_cost, kCost);
  EXPECT_EQ(snapshot.connect.wait_create_channel_resp.total_cost, kCost);
}

TEST_F(HixlCSStatisticManagerUTest, GetSnapshotForTransferStage) {
  auto &manager = HixlCSStatisticManager::GetInstance();
  manager.UpdateTransferCost(client_channel_id_, kCost, kSubmitCost, kWaitCost, kBytes, kOpDescCount, false);
  manager.UpdateStageCost(client_channel_id_, StatisticStage::kCheckStatusHost, 10UL);
  manager.UpdateTransferCost(client_channel_id_, kCost * 2U, kSubmitCost * 2U, kWaitCost * 2U, kBytes * 2U,
                             kOpDescCount * 2U, true);
  manager.UpdateStageCost(client_channel_id_, StatisticStage::kCheckStatusDevice, 20UL);

  const auto snapshot = manager.GetSnapshot(client_channel_id_);
  EXPECT_EQ(snapshot.transfer.transfer_total.times, 2UL);
  EXPECT_EQ(snapshot.transfer.transfer_total.total_cost, kCost * 3U);
  EXPECT_EQ(snapshot.transfer.transfer_submit.total_cost, kSubmitCost * 3U);
  EXPECT_EQ(snapshot.transfer.transfer_wait_complete.total_cost, kWaitCost * 3U);
  EXPECT_EQ(snapshot.transfer.total_bytes, kBytes * 3U);
  EXPECT_EQ(snapshot.transfer.total_op_desc_count, kOpDescCount * 3U);
  EXPECT_EQ(snapshot.transfer.host_transfer_times, 1UL);
  EXPECT_EQ(snapshot.transfer.device_transfer_times, 1UL);
  EXPECT_EQ(snapshot.transfer.check_status_host.total_cost, 10UL);
  EXPECT_EQ(snapshot.transfer.check_status_device.total_cost, 20UL);
}

TEST_F(HixlCSStatisticManagerUTest, GetSnapshotForServerStage) {
  auto &manager = HixlCSStatisticManager::GetInstance();
  manager.UpdateStageCost(server_channel_id_, StatisticStage::kServerInitialize, kCost);
  manager.UpdateStageCost(server_channel_id_, StatisticStage::kServerListen, kCost);
  manager.UpdateStageCost(server_channel_id_, StatisticStage::kServerMatchEndpoint, kCost);
  manager.UpdateStageCost(server_channel_id_, StatisticStage::kServerCreateChannel, kCost);
  manager.UpdateStageCost(server_channel_id_, StatisticStage::kServerExportMem, kCost);
  manager.UpdateStageCost(server_channel_id_, StatisticStage::kServerDestroyChannel, kCost);
  manager.UpdateStageCost(server_channel_id_, StatisticStage::kServerCleanupClient, kCost);
  manager.UpdateStageCost(server_channel_id_, StatisticStage::kServerFinalize, kCost);

  const auto snapshot = manager.GetSnapshot(server_channel_id_);
  EXPECT_EQ(snapshot.server.initialize.total_cost, kCost);
  EXPECT_EQ(snapshot.server.listen.total_cost, kCost);
  EXPECT_EQ(snapshot.server.server_match_endpoint.total_cost, kCost);
  EXPECT_EQ(snapshot.server.server_create_channel.total_cost, kCost);
  EXPECT_EQ(snapshot.server.server_export_mem.total_cost, kCost);
  EXPECT_EQ(snapshot.server.server_destroy_channel.total_cost, kCost);
  EXPECT_EQ(snapshot.server.server_cleanup_client.total_cost, kCost);
  EXPECT_EQ(snapshot.server.finalize.total_cost, kCost);
}

TEST_F(HixlCSStatisticManagerUTest, RemoveChannelClearsSnapshot) {
  auto &manager = HixlCSStatisticManager::GetInstance();
  manager.UpdateStageCost(client_channel_id_, StatisticStage::kConnectTotal, kCost);
  manager.RemoveChannel(client_channel_id_);

  const auto snapshot = manager.GetSnapshot(client_channel_id_);
  EXPECT_EQ(snapshot.connect.connect_total.times, 0UL);
}

TEST_F(HixlCSStatisticManagerUTest, DumpDoesNotCrash) {
  auto &manager = HixlCSStatisticManager::GetInstance();
  manager.UpdateStageCost(client_channel_id_, StatisticStage::kConnectTotal, kCost);
  manager.UpdateTransferCost(client_channel_id_, kCost, kSubmitCost, kWaitCost, kBytes, kOpDescCount, false);
  manager.UpdateStageCost(server_channel_id_, StatisticStage::kServerCreateChannel, kCost);
  manager.Dump();
}

}  // namespace hixl
