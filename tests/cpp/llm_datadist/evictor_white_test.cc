/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <vector>
#include <atomic>
#include <algorithm>
#include "adxl/channel.h"
#include "adxl/channel_manager.h"
#include "adxl/channel_msg_handler.h"
#include "adxl/channel_evictor.h"
#include "adxl/buffer_transfer_service.h"
#include "hixl/hixl.h"
#include "depends/llm_datadist/src/data_cache_engine_test_helper.h"
#include "depends/mmpa/src/mmpa_stub.h"

namespace adxl{
class MockChannelEvictor : public ChannelEvictor {
public:
  explicit MockChannelEvictor(ChannelManager* channel_manager)
    : ChannelEvictor(channel_manager) {};
  
  ~MockChannelEvictor() = default;

  void ProcessEvictionByChannelId(ChannelType channel_type, const std::string& channel_id) {
    auto channel = channel_manager_->GetChannel(channel_type, channel_id);
    if (channel == nullptr){
      return;
    }
    if (channel->GetTransferCount() > 0) {
      return;
    }
    channel_manager_->DestroyChannel(channel_type, channel_id);
    return;
  }
};

class ChannelEvictorWhiteboxTest : public ::testing::Test {
protected:
  void SetUp() override {
    channel_options_["adxl.max_channel"] = "10";
    channel_options_["adxl.high_waterline"] = "0.8";
    channel_options_["adxl.low_waterline"] = "0.5";

    SetMockRtGetDeviceWay(1);
    llm::MockMmpaForHcclApi::Install();
    llm::AutoCommResRuntimeMock::Install();
    llm::HcclAdapter::GetInstance().Initialize();
    llm::AutoCommResRuntimeMock::SetDevice(0);

    channel_manager_ = std::make_unique<ChannelManager>();
    Status ret = channel_manager_->Initialize(buffer_transfer_service_.get());
    ASSERT_EQ(ret, SUCCESS) << "Failed to initialize ChannelManager";
    std::string listen_info = "127.0.0.1:20000";
    channel_evictor_ = std::make_unique<ChannelEvictor>(channel_manager_.get());
  }

  void TearDown() override {
    if (channel_evictor_) {
        channel_evictor_->Finalize();
    }
    
    if (channel_manager_) {
        channel_manager_->Finalize();
    }

    if (buffer_transfer_service_) {
        buffer_transfer_service_->Finalize();
    }

    llm::HcclAdapter::GetInstance().Finalize();
    llm::MockMmpaForHcclApi::Reset();
    llm::AutoCommResRuntimeMock::Reset();
    SetMockRtGetDeviceWay(0);
  }

  void CreateChannels(int count, ChannelType channel_type = ChannelType::kClient) {
    for (int i = 0; i < count; i++) {
      std::string channel_id = channel_type == ChannelType::kClient 
                    ? "127.0.0.1:" + std::to_string(20000 + channel_manager_->GetAllClientChannel().size())
                    : "127.0.0.1:" + std::to_string(26000 + channel_manager_->GetAllServerChannel().size());

      ChannelInfo channel_info{};
      channel_info.channel_type = channel_type;
      channel_info.channel_id = channel_id;
      channel_info.peer_rank_id = 1;
      channel_info.local_rank_id = 0;

      ChannelPtr created_channel;
      LLMLOGI("Create channel: %s", channel_id.c_str());
      Status ret = channel_manager_->CreateChannel(channel_info, created_channel);
      ASSERT_EQ(ret, SUCCESS) << "Failed to create channel: " << channel_id;

      created_channel_ids_.push_back(channel_id);
    }
  }

  bool ChannelExists(const std::string& channel_id, ChannelType channel_type = ChannelType::kClient) {
    ChannelPtr channel = channel_manager_->GetChannel(channel_type, channel_id);
    if (channel) {
      return true;
    }

    channel = channel_manager_->GetChannel(channel_type == ChannelType::kClient ? ChannelType::kServer : ChannelType::kClient, channel_id);
    return channel != nullptr;
  }

  std::vector<std::string> GetCurrentChannelIds() const {
    std::vector<std::string> channel_ids;

    auto client_channels = channel_manager_->GetAllClientChannel();
    for (const auto& channel : client_channels) {
      channel_ids.push_back(channel->GetChannelId());
    }

    auto server_channels = channel_manager_->GetAllServerChannel();
    for (const auto& channel : server_channels) {
      channel_ids.push_back(channel->GetChannelId());
    }

    return channel_ids;
  }

  int GetCurrentChannelCount() const {
    return channel_manager_->GetAllClientChannel().size() + channel_manager_->GetAllServerChannel().size();
  }

  std::unique_ptr<BufferTransferService> buffer_transfer_service_;
  std::unique_ptr<ChannelManager> channel_manager_;
  std::unique_ptr<ChannelEvictor> channel_evictor_;

  std::map<AscendString, AscendString> channel_options_;
  std::vector<std::string> created_channel_ids_;
};

TEST_F(ChannelEvictorWhiteboxTest, TestTrigger) {
  Status ret = channel_evictor_->Initialize(channel_options_);
  ASSERT_EQ(ret, SUCCESS);

  EXPECT_EQ(GetCurrentChannelCount(), 0);
  EXPECT_EQ(created_channel_ids_.size(), 0);

  EXPECT_FALSE(channel_evictor_->ShouldTriggerEviction());
  // create 3 client channels
  CreateChannels(3, ChannelType::kClient);
  // current channel count is 3
  EXPECT_EQ(GetCurrentChannelCount(), 3);
  EXPECT_FALSE(channel_evictor_->ShouldTriggerEviction());
  // create 5 client channels
  CreateChannels(5, ChannelType::kClient);
  // current channel count is 8
  EXPECT_EQ(GetCurrentChannelCount(), 8);
  EXPECT_TRUE(channel_evictor_->ShouldTriggerEviction());
  // create 9 client channels
  CreateChannels(1, ChannelType::kClient);
  // current channel count is 9
  EXPECT_EQ(GetCurrentChannelCount(), 9);
  // should trigger evcition
  EXPECT_TRUE(channel_evictor_->ShouldTriggerEviction());
}

TEST_F(ChannelEvictorWhiteboxTest, TestChannelEvictionByCreateTime) { 
  Status ret = channel_evictor_->Initialize(channel_options_);
  ASSERT_EQ(ret, SUCCESS);

  // create 8 client channels
  CreateChannels(8, ChannelType::kClient);
  EXPECT_TRUE(channel_evictor_->ShouldTriggerEviction());

  auto current_channels = GetCurrentChannelIds();
  // current channel count is 8
  EXPECT_EQ(current_channels.size(), 8);

  for (const auto& channel_id : created_channel_ids_) {
    EXPECT_TRUE(std::find(current_channels.begin(), current_channels.end(), channel_id) != current_channels.end());
  }

  CreateChannels(1, ChannelType::kClient);
  // notify eviction task
  channel_evictor_->NotifyEviction();
  // sleep 500 ms for eviction task
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  // current channel count should be 5
  EXPECT_EQ(GetCurrentChannelCount(), 5);
  EXPECT_FALSE(ChannelExists("127.0.0.1:20000", ChannelType::kClient));
  EXPECT_FALSE(ChannelExists("127.0.0.1:20001", ChannelType::kClient));
  EXPECT_FALSE(ChannelExists("127.0.0.1:20002", ChannelType::kClient));
  EXPECT_FALSE(ChannelExists("127.0.0.1:20003", ChannelType::kClient));
  EXPECT_TRUE(ChannelExists("127.0.0.1:20004", ChannelType::kClient));
}

TEST_F(ChannelEvictorWhiteboxTest, TestClientEvictionByTransferFlag) {
  Status ret = channel_evictor_->Initialize(channel_options_);
  ASSERT_EQ(ret, SUCCESS);
  // create 8 client channels
  CreateChannels(8, ChannelType::kClient);
  EXPECT_TRUE(channel_evictor_->ShouldTriggerEviction());

  auto current_channels = GetCurrentChannelIds();
  // current channel count should be 8
  EXPECT_EQ(current_channels.size(), 8);

  for (const auto& channel_id : created_channel_ids_) {
    EXPECT_TRUE(std::find(current_channels.begin(), current_channels.end(), channel_id) != current_channels.end());
  }

  ChannelPtr trans_channel = channel_manager_->GetChannel(ChannelType::kClient, "127.0.0.1:20000");
  trans_channel->SetHasTransferred(true);
  // create 1 client channel
  CreateChannels(1, ChannelType::kClient);
  channel_evictor_->NotifyEviction();
  // sleep 500 ms for evcition
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  // current channel count should be 5
  EXPECT_EQ(GetCurrentChannelCount(), 5);
  EXPECT_TRUE(ChannelExists("127.0.0.1:20000", ChannelType::kClient));
  EXPECT_FALSE(ChannelExists("127.0.0.1:20001", ChannelType::kClient));
  EXPECT_FALSE(ChannelExists("127.0.0.1:20002", ChannelType::kClient));
  EXPECT_FALSE(ChannelExists("127.0.0.1:20003", ChannelType::kClient));
  EXPECT_FALSE(ChannelExists("127.0.0.1:20004", ChannelType::kClient));
}

TEST_F(ChannelEvictorWhiteboxTest, TestMixChannelStrategy) {
  std::unique_ptr<MockChannelEvictor> mock_channel_evictor_;
  std::string mock_listen_info = "127.0.0.1:24000";
  mock_channel_evictor_ = std::make_unique<MockChannelEvictor>(channel_manager_.get());
  Status ret = mock_channel_evictor_->Initialize(channel_options_);
  ASSERT_EQ(ret, SUCCESS);
  // craete 5 client channels
  CreateChannels(5, ChannelType::kClient);
  // create 4 client channels
  CreateChannels(4, ChannelType::kServer);
  EXPECT_TRUE(mock_channel_evictor_->ShouldTriggerEviction());

  auto current_channels = GetCurrentChannelIds();
  // current channel count shoule be 9
  EXPECT_EQ(current_channels.size(), 9);

  for(const auto& channel_id : created_channel_ids_) {
    EXPECT_TRUE(std::find(current_channels.begin(), current_channels.end(), channel_id) != current_channels.end());
  }

  ChannelPtr trans_channel = channel_manager_->GetChannel(ChannelType::kClient, "127.0.0.1:20000");
  trans_channel->IncrementTransferCount();
  // select 9 candiadates for evcition
  std::vector<EvictItem> candidates = channel_evictor_->SelectEvictionCandidates(9);
  for (auto& item : candidates) {
    mock_channel_evictor_->ProcessEvictionByChannelId(item.channel_type, item.channel_id);
  }
  // shoule left 1 channel which has transfer task
  EXPECT_EQ(GetCurrentChannelCount(), 1);
  EXPECT_TRUE(ChannelExists("127.0.0.1:20000", ChannelType::kClient));
  EXPECT_FALSE(ChannelExists("127.0.0.1:20001", ChannelType::kClient));
  EXPECT_FALSE(ChannelExists("127.0.0.1:20002", ChannelType::kClient));
  EXPECT_FALSE(ChannelExists("127.0.0.1:26000", ChannelType::kServer));

  // left 1 client channel
  EXPECT_EQ(channel_manager_->GetAllClientChannel().size(), 1);
  // left 0 server channel
  EXPECT_EQ(channel_manager_->GetAllServerChannel().size(), 0);
  mock_channel_evictor_->Finalize();
}

TEST_F(ChannelEvictorWhiteboxTest, TestSelectClientEvictionCandidates) {
  // create 6 client channels
  CreateChannels(6, ChannelType::kClient);
  // create 2 server channels
  CreateChannels(2, ChannelType::kServer);

  channel_manager_->GetChannel(ChannelType::kClient, "127.0.0.1:20000")->SetHasTransferred(true);
  channel_manager_->GetChannel(ChannelType::kServer, "127.0.0.1:26000")->SetHasTransferred(true);
  // select 5 candidates
  std::vector<EvictItem> candidates = channel_evictor_->SelectEvictionCandidates(5);
  // get 5 candidates
  EXPECT_EQ(candidates.size(), 5);

  int client_count = 0;
  int server_count = 0;
  for(const auto& item: candidates) {
    auto channel = channel_manager_->GetChannel(item.channel_type, item.channel_id);
    if (item.channel_type == ChannelType::kClient) {
      client_count++;
      EXPECT_FALSE(channel->GetHasTransferred());
    } else {
      server_count++;
      EXPECT_FALSE(channel->GetHasTransferred());
    }
    EXPECT_TRUE(channel->IsDisconnecting());
  }
  // should get 5 client channels
  EXPECT_EQ(client_count, 5);
  EXPECT_EQ(server_count, 0);
}

TEST_F(ChannelEvictorWhiteboxTest, TestSelectServerEvictionCandidates) {
  // create 7 server channels
  CreateChannels(7, ChannelType::kServer);
  // create 2 client channels
  CreateChannels(2, ChannelType::kClient);

  // select 5 candidates
  std::vector<EvictItem> candidates = channel_evictor_->SelectEvictionCandidates(5);
  // should get 5 candidates
  EXPECT_EQ(candidates.size(), 5);

  int client_count_ = 0;
  int server_count_ = 0;
  for(const auto& item: candidates) {
    auto channel = channel_manager_->GetChannel(item.channel_type, item.channel_id);
    if (item.channel_type == ChannelType::kClient) {
      client_count_++;
      EXPECT_FALSE(channel->GetHasTransferred());
    } else {
      server_count_++;
      EXPECT_FALSE(channel->GetHasTransferred());
    }
    EXPECT_TRUE(channel->IsDisconnecting());
  }
  // shoule get 5 server channels
  EXPECT_EQ(server_count_, 5);
  EXPECT_EQ(client_count_, 0);
}

TEST_F(ChannelEvictorWhiteboxTest, TestMixEvictionCandidates) {
  // create 3 server channels
  CreateChannels(3, ChannelType::kServer);
  // create 3 client channels
  CreateChannels(3, ChannelType::kClient);
  // set transferflag for 3 server and client channels
  for (int i = 0; i < 3; i++) {
    std::string client_id = "127.0.0.1:2000" + std::to_string(i);
    channel_manager_->GetChannel(ChannelType::kClient, client_id)->SetHasTransferred(true);
    std::string server_id = "127.0.0.1:2600" + std::to_string(i);
    channel_manager_->GetChannel(ChannelType::kServer, server_id)->SetHasTransferred(false);
  }
  // select 4 candiates
  std::vector<EvictItem> candidates = channel_evictor_->SelectEvictionCandidates(4);
  // should get 4 channels
  EXPECT_EQ(candidates.size(), 4);

  // check expected 4 channels
  for (int i = 0; i < 4; i++) {
    // mix channels between 2 types
    if (i % 2 == 0) {
      EXPECT_EQ(candidates[i].channel_type, ChannelType::kClient);
    } else {
      EXPECT_EQ(candidates[i].channel_type, ChannelType::kServer);
    }
  }
}

TEST_F(ChannelEvictorWhiteboxTest, TestMultipleConcurrentEvictionRequests) {
  Status ret = channel_evictor_->Initialize(channel_options_);
  ASSERT_EQ(ret, SUCCESS);
  // create 8 client channels
  CreateChannels(8, ChannelType::kClient);
  EXPECT_TRUE(channel_evictor_->ShouldTriggerEviction());
  // make 5 concurrent requests
  const int kConcurrentRequests = 5;
  std::vector<std::thread> eviction_threads;
  eviction_threads.reserve(kConcurrentRequests);

  for (int i = 0; i < kConcurrentRequests; ++i) {
    eviction_threads.emplace_back([this]() {
      channel_evictor_->NotifyEviction();
    });
  }
  for (auto& thread : eviction_threads) {
    if (thread.joinable()) {
      thread.join();
    }
  }
  // sleep 500 ms wait for eviction
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  // after eviction should have no more than 5 channels
  EXPECT_LE(GetCurrentChannelCount(), 5);
}

TEST_F(ChannelEvictorWhiteboxTest, TestTransferCompletionDuringEviction) {
  Status ret = channel_evictor_->Initialize(channel_options_);
  ASSERT_EQ(ret, SUCCESS);
  // create 8 client channels
  CreateChannels(8, ChannelType::kClient);
  EXPECT_TRUE(channel_evictor_->ShouldTriggerEviction());

  std::string channel_id = "127.0.0.1:20000";
  ChannelPtr channel = channel_manager_->GetChannel(ChannelType::kClient, channel_id);
  channel->IncrementTransferCount();

  std::atomic<bool> transfer_complete(false);

  std::thread eviction_thread(
    [this]() {
    channel_evictor_->NotifyEviction();
  });

  std::thread transfer_complete_thread(
    [&channel, 
    &transfer_complete]() {
    // sleep 100 ms as transfer
    std::this_thread::sleep_for(std::chrono::milliseconds(100)); 
    channel->DecrementTransferCount(); 
    transfer_complete = true;
  });
  if (eviction_thread.joinable()) {
    eviction_thread.join();
  }
  if (transfer_complete_thread.joinable()) {
    transfer_complete_thread.join();
  }
  // sleep 500 ms for eviction
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  // Since the transfer completed during eviction, the channel still exist so result is 6
  EXPECT_EQ(GetCurrentChannelCount(), 6);
}
}
