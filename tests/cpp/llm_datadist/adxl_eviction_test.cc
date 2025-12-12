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
#include "adxl/channel.h"
#include "hixl/hixl.h"
#include "adxl/channel_manager.h"
#include "adxl/channel_msg_handler.h"
#include "depends/llm_datadist/src/data_cache_engine_test_helper.h"
#include "depends/mmpa/src/mmpa_stub.h"
namespace hixl{

class EvictionTest : public ::testing::Test {
 protected:
  void SetUp() override {
    SetMockRtGetDeviceWay(1);
    llm::MockMmpaForHcclApi::Install();
    llm::AutoCommResRuntimeMock::Install();
    llm::HcclAdapter::GetInstance().Initialize();
    options_["GlobalResourceConfig"] = "../tests/cpp/llm_datadist/evictor_config.json";
  }

  void TearDown() override {
    llm::HcclAdapter::GetInstance().Finalize();
    llm::MockMmpaForHcclApi::Reset();
    llm::AutoCommResRuntimeMock::Reset();
    SetMockRtGetDeviceWay(0);
  }
  std::map<AscendString, AscendString> options_;
};

TEST_F(EvictionTest, ClientEvictionTest) {
  llm::AutoCommResRuntimeMock::SetDevice(0);
  Hixl client_;
  client_.Initialize("127.0.0.1:20000", options_);
  llm::AutoCommResRuntimeMock::SetDevice(1);
  Hixl server_;
  server_.Initialize("127.0.0.1:20001", options_);
  // set device 2
  llm::AutoCommResRuntimeMock::SetDevice(2);
  Hixl server1_;
  server1_.Initialize("127.0.0.1:20002", options_);
  // set device 3
  llm::AutoCommResRuntimeMock::SetDevice(3);
  Hixl server2_;
  server2_.Initialize("127.0.0.1:20003", options_);
  // set device 4
  llm::AutoCommResRuntimeMock::SetDevice(4);
  Hixl server3_;
  server3_.Initialize("127.0.0.1:20004", options_);
  EXPECT_EQ(client_.Connect("127.0.0.1:20001"), SUCCESS);
  EXPECT_EQ(client_.Connect("127.0.0.1:20001"), ALREADY_CONNECTED);
  EXPECT_EQ(client_.Connect("127.0.0.1:20002"), SUCCESS);
  EXPECT_EQ(client_.Connect("127.0.0.1:20003"), SUCCESS);
  EXPECT_EQ(client_.Connect("127.0.0.1:20004"), SUCCESS);
  // sleep 500 ms for eviction
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  
  EXPECT_EQ(client_.Disconnect("127.0.0.1:20001"), NOT_CONNECTED);
  EXPECT_EQ(client_.Disconnect("127.0.0.1:20002"), NOT_CONNECTED);
  EXPECT_EQ(client_.Connect("127.0.0.1:20001"), SUCCESS);

  client_.Finalize();
  server_.Finalize();
  server1_.Finalize();
  server2_.Finalize();
  server3_.Finalize();
}

TEST_F(EvictionTest, ServerEvictionTest) {
  llm::AutoCommResRuntimeMock::SetDevice(0);
  Hixl server_;
  server_.Initialize("127.0.0.1:26000", options_);
  llm::AutoCommResRuntimeMock::SetDevice(1);
  Hixl client_;
  client_.Initialize("127.0.0.1:26001", options_);
  // set device 2
  llm::AutoCommResRuntimeMock::SetDevice(2);
  Hixl client1_;
  client1_.Initialize("127.0.0.1:26002", options_);
  // set device 3
  llm::AutoCommResRuntimeMock::SetDevice(3);
  Hixl client2_;
  client2_.Initialize("127.0.0.1:26003", options_);
  // set device 4
  llm::AutoCommResRuntimeMock::SetDevice(4);
  Hixl client3_;
  client3_.Initialize("127.0.0.1:26004", options_);
  EXPECT_EQ(client_.Connect("127.0.0.1:26000"), SUCCESS);
  EXPECT_EQ(client_.Connect("127.0.0.1:26000"), ALREADY_CONNECTED);
  EXPECT_EQ(client1_.Connect("127.0.0.1:26000"), SUCCESS);
  EXPECT_EQ(client2_.Connect("127.0.0.1:26000"), SUCCESS);
  EXPECT_EQ(client3_.Connect("127.0.0.1:26000"), SUCCESS);
  // sleep 2000 ms for eviction
  std::this_thread::sleep_for(std::chrono::milliseconds(2000));
  EXPECT_EQ(client_.Disconnect("127.0.0.1:26000"), NOT_CONNECTED);
  EXPECT_EQ(client1_.Disconnect("127.0.0.1:26000"), NOT_CONNECTED);
  EXPECT_EQ(client_.Connect("127.0.0.1:26000"), SUCCESS);

  server_.Finalize();
  client_.Finalize();
  client1_.Finalize();
  client2_.Finalize();
  client3_.Finalize();
}

TEST_F(EvictionTest, TestAtomicCounters) {
  adxl::ChannelInfo channel_info{};
  channel_info.channel_type = adxl::ChannelType::kClient;
  channel_info.channel_id = "test_channel";
  channel_info.peer_rank_id = 1;
  channel_info.local_rank_id = 0;
  
  adxl::ChannelPtr channel = std::make_shared<adxl::Channel>(channel_info);
  channel->Initialize();
  
  ASSERT_EQ(channel->GetTransferCount(), 0);
  ASSERT_FALSE(channel->GetHasTransferred());
  ASSERT_FALSE(channel->IsDisconnecting());

  const int num_threads = 10;
  const int transfers_per_thread = 100;
  std::vector<std::thread> threads;
  
  for (int i = 0; i < num_threads; i++) {
    threads.emplace_back([&channel, transfers_per_thread]() {
      for (int j = 0; j < transfers_per_thread; j++) {
        channel->IncrementTransferCount();
        channel->SetHasTransferred(true);
        // sleep 10 ms
        std::this_thread::sleep_for(std::chrono::microseconds(10));
        channel->DecrementTransferCount();
      }
    });
  }
  
  for (auto& t : threads) {
    t.join();
  }
  ASSERT_EQ(channel->GetTransferCount(), 0);
  ASSERT_TRUE(channel->GetHasTransferred());
  channel->Finalize();
}

TEST_F(EvictionTest, ClientDisconnectHandling) {
  llm::AutoCommResRuntimeMock::SetDevice(0);
  Hixl engine1;
  EXPECT_EQ(engine1.Initialize("127.0.0.1:26000", options_), SUCCESS);

  llm::AutoCommResRuntimeMock::SetDevice(1);
  Hixl engine2;
  EXPECT_EQ(engine2.Initialize("127.0.0.1:26001", options_), SUCCESS);

  hixl::MemDesc mem{};
  // mock addr 1234
  mem.addr = 1234;
  // mock len 10
  mem.len = 10;
  MemHandle handle1 = nullptr;
  EXPECT_EQ(engine1.RegisterMem(mem, MEM_DEVICE, handle1), SUCCESS);

  MemHandle handle2 = nullptr;
  EXPECT_EQ(engine2.RegisterMem(mem, MEM_DEVICE, handle2), SUCCESS);

  int32_t src = 1;
  int32_t dst = 2;
  TransferOpDesc desc{reinterpret_cast<uintptr_t>(&src), reinterpret_cast<uintptr_t>(&dst), sizeof(int32_t)};
  EXPECT_EQ(engine1.TransferSync("127.0.0.1:26001", READ, {desc}), SUCCESS);
  // after transfer, src set to 2
  EXPECT_EQ(src, 2);
  src = 1;
  EXPECT_EQ(engine1.TransferSync("127.0.0.1:26001", WRITE, {desc}), SUCCESS);
  EXPECT_EQ(dst, 1);

  EXPECT_EQ(engine1.Disconnect("127.0.0.1:26001"), SUCCESS);
  EXPECT_EQ(engine1.DeregisterMem(handle1), SUCCESS);
  EXPECT_EQ(engine2.DeregisterMem(handle2), SUCCESS);
  engine1.Finalize();
  engine2.Finalize();
}

TEST_F(EvictionTest, TestEvictionWithTransfer) {
  llm::AutoCommResRuntimeMock::SetDevice(0);
  Hixl engine1;
  EXPECT_EQ(engine1.Initialize("127.0.0.1:26000", options_), SUCCESS);

  llm::AutoCommResRuntimeMock::SetDevice(1);
  Hixl engine2;
  EXPECT_EQ(engine2.Initialize("127.0.0.1:26001", options_), SUCCESS);
  // set device 2
  llm::AutoCommResRuntimeMock::SetDevice(2);
  Hixl engine3;
  EXPECT_EQ(engine3.Initialize("127.0.0.1:26002", options_), SUCCESS);
  // set device 3
  llm::AutoCommResRuntimeMock::SetDevice(3);
  Hixl engine4;
  EXPECT_EQ(engine4.Initialize("127.0.0.1:26003", options_), SUCCESS);
  // set device 4
  llm::AutoCommResRuntimeMock::SetDevice(4);
  Hixl engine5;
  EXPECT_EQ(engine5.Initialize("127.0.0.1:26004", options_), SUCCESS);

  hixl::MemDesc mem{};
  // mock addr 1234
  mem.addr = 1234;
  // mock len 10
  mem.len = 10;
  MemHandle handle1 = nullptr;
  EXPECT_EQ(engine1.RegisterMem(mem, MEM_DEVICE, handle1), SUCCESS);

  MemHandle handle2 = nullptr;
  EXPECT_EQ(engine2.RegisterMem(mem, MEM_DEVICE, handle2), SUCCESS);

  int32_t src = 1;
  int32_t dst = 2;
  TransferOpDesc desc{reinterpret_cast<uintptr_t>(&src), reinterpret_cast<uintptr_t>(&dst), sizeof(int32_t)};
  EXPECT_EQ(engine1.Connect("127.0.0.1:26001"), SUCCESS);
  EXPECT_EQ(engine1.TransferSync("127.0.0.1:26001", READ, {desc}), SUCCESS);
  EXPECT_EQ(engine1.Connect("127.0.0.1:26002"), SUCCESS);
  EXPECT_EQ(engine1.Connect("127.0.0.1:26003"), SUCCESS);
  EXPECT_EQ(engine1.Connect("127.0.0.1:26004"), SUCCESS);
  // sleep 200 ms for eviction
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  EXPECT_EQ(engine1.Disconnect("127.0.0.1:26002"), NOT_CONNECTED);
  EXPECT_EQ(engine1.Disconnect("127.0.0.1:26003"), NOT_CONNECTED);
  EXPECT_EQ(engine1.Connect("127.0.0.1:26001"), ALREADY_CONNECTED);
  EXPECT_EQ(engine1.Disconnect("127.0.0.1:26001"), SUCCESS);
  EXPECT_EQ(engine1.DeregisterMem(handle1), SUCCESS);
  EXPECT_EQ(engine2.DeregisterMem(handle2), SUCCESS);
  engine1.Finalize();
  engine2.Finalize();
  engine3.Finalize();
  engine4.Finalize();
  engine5.Finalize();
}

TEST_F(EvictionTest, TestWaterline) {
  llm::AutoCommResRuntimeMock::SetDevice(0);
  Hixl engine1;
  options_.erase("GlobalResourceConfig");
  options_["adxl.max_channel"] = "a";
  EXPECT_EQ(engine1.Initialize("127.0.0.1:26000", options_), PARAM_INVALID);
  options_["adxl.max_channel"] = "0";
  EXPECT_EQ(engine1.Initialize("127.0.0.1:26000", options_), PARAM_INVALID);
  options_["adxl.max_channel"] = "8.5";
  EXPECT_EQ(engine1.Initialize("127.0.0.1:26000", options_), PARAM_INVALID);
  options_["adxl.max_channel"] = "-1";
  EXPECT_EQ(engine1.Initialize("127.0.0.1:26000", options_), PARAM_INVALID);
  options_["adxl.max_channel"] = "10";

  options_["adxl.high_waterline"] = "abc";
  EXPECT_EQ(engine1.Initialize("127.0.0.1:26000", options_), PARAM_INVALID);
  options_["adxl.high_waterline"] = "1";
  EXPECT_EQ(engine1.Initialize("127.0.0.1:26000", options_), PARAM_INVALID);
  options_["adxl.high_waterline"] = "0";
  EXPECT_EQ(engine1.Initialize("127.0.0.1:26000", options_), PARAM_INVALID);
  options_["adxl.high_waterline"] = "0.9";
  
  options_["adxl.low_waterline"] = "cba";
  EXPECT_EQ(engine1.Initialize("127.0.0.1:26000", options_), PARAM_INVALID);
  options_["adxl.low_waterline"] = "1";
  EXPECT_EQ(engine1.Initialize("127.0.0.1:26000", options_), PARAM_INVALID);
  options_["adxl.low_waterline"] = "0";
  EXPECT_EQ(engine1.Initialize("127.0.0.1:26000", options_), PARAM_INVALID);
  options_["adxl.low_waterline"] = "0.6";

  options_["adxl.high_waterline"] = "0.5";
  EXPECT_EQ(engine1.Initialize("127.0.0.1:26000", options_), PARAM_INVALID);
  options_["adxl.high_waterline"] = "0.6";
  EXPECT_EQ(engine1.Initialize("127.0.0.1:26000", options_), PARAM_INVALID);

  options_["adxl.high_waterline"] = "0.9";
  options_["adxl.low_waterline"] = "0.6";
  EXPECT_EQ(engine1.Initialize("127.0.0.1:26000", options_), SUCCESS);
}
}