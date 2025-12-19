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

class ChannelPoolSystemTest : public ::testing::Test {
 protected:
  void SetUp() override {
    SetMockRtGetDeviceWay(1);
    llm::MockMmpaForHcclApi::Install();
    llm::AutoCommResRuntimeMock::Install();
    llm::HcclAdapter::GetInstance().Initialize();
    options_["GlobalResourceConfig"] = 
      "../tests/cpp/llm_datadist/global_resource_configs/evictor_config.json";
  }

  void TearDown() override {
    llm::HcclAdapter::GetInstance().Finalize();
    llm::MockMmpaForHcclApi::Reset();
    llm::AutoCommResRuntimeMock::Reset();
    SetMockRtGetDeviceWay(0);
  }
  std::map<AscendString, AscendString> options_;
};

TEST_F(ChannelPoolSystemTest, ClientChannelPoolSystemTest) {
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

TEST_F(ChannelPoolSystemTest, ServerChannelPoolSystemTest) {
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

TEST_F(ChannelPoolSystemTest, ClientDisconnectHandling) {
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

TEST_F(ChannelPoolSystemTest, TestEvictionWithTransfer) {
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

  hixl::MemDesc mem_{};
  // mock addr 1234
  mem_.addr = 1234;
  // mock len 10
  mem_.len = 10;
  MemHandle handle1 = nullptr;
  EXPECT_EQ(engine1.RegisterMem(mem_, MEM_DEVICE, handle1), SUCCESS);

  MemHandle handle2 = nullptr;
  EXPECT_EQ(engine2.RegisterMem(mem_, MEM_DEVICE, handle2), SUCCESS);

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

TEST_F(ChannelPoolSystemTest, TestWaterline) {
  llm::AutoCommResRuntimeMock::SetDevice(0);
  Hixl engine1;
  options_["GlobalResourceConfig"] = "../tests/cpp/llm_datadist/global_resource_configs/not_exists.json";
  EXPECT_EQ(engine1.Initialize("127.0.0.1:26000", options_), PARAM_INVALID);

  options_["GlobalResourceConfig"] = "../tests/cpp/llm_datadist/global_resource_configs/invalid_diff_le_1.json";
  EXPECT_EQ(engine1.Initialize("127.0.0.1:26000", options_), PARAM_INVALID);
  
  options_["GlobalResourceConfig"] = "../tests/cpp/llm_datadist/global_resource_configs/invalid_low_ge_high.json";
  EXPECT_EQ(engine1.Initialize("127.0.0.1:26000", options_), PARAM_INVALID);

  options_["GlobalResourceConfig"] = "../tests/cpp/llm_datadist/global_resource_configs/invalid_max_channel_0.json";
  EXPECT_EQ(engine1.Initialize("127.0.0.1:26000", options_), PARAM_INVALID);

  options_["GlobalResourceConfig"] = "../tests/cpp/llm_datadist/global_resource_configs/invalid_max_channel_exceed.json";
  EXPECT_EQ(engine1.Initialize("127.0.0.1:26000", options_), PARAM_INVALID);

  options_["GlobalResourceConfig"] = "../tests/cpp/llm_datadist/global_resource_configs/invalid_only_max_channel.json";
  EXPECT_EQ(engine1.Initialize("127.0.0.1:26000", options_), PARAM_INVALID);

  options_["GlobalResourceConfig"] = "../tests/cpp/llm_datadist/global_resource_configs/invalid_waterline_ge_1.json";
  EXPECT_EQ(engine1.Initialize("127.0.0.1:26000", options_), PARAM_INVALID);

  options_["GlobalResourceConfig"] = "../tests/cpp/llm_datadist/global_resource_configs/invalid_waterline_le_0.json";
  EXPECT_EQ(engine1.Initialize("127.0.0.1:26000", options_), PARAM_INVALID);

  options_["GlobalResourceConfig"] = "../tests/cpp/llm_datadist/global_resource_configs/invalid_waterline_nan.json";
  EXPECT_EQ(engine1.Initialize("127.0.0.1:26000", options_), PARAM_INVALID);

  options_["GlobalResourceConfig"] = "../tests/cpp/llm_datadist/global_resource_configs/invalid_max_channel_nan.json";
  EXPECT_EQ(engine1.Initialize("127.0.0.1:26000", options_), PARAM_INVALID);

  options_["GlobalResourceConfig"] = "../tests/cpp/llm_datadist/global_resource_configs/evictor_config.json";
  EXPECT_EQ(engine1.Initialize("127.0.0.1:26000", options_), SUCCESS);
}

TEST_F(ChannelPoolSystemTest, ClientDisconnectHandlingAsync) {
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

  // set src context to 1
  int32_t src = 1;
  // set dst context to 2
  int32_t dst = 2;
  TransferOpDesc desc{reinterpret_cast<uintptr_t>(&src), reinterpret_cast<uintptr_t>(&dst), sizeof(int32_t)};
  TransferReq req = nullptr;
  EXPECT_EQ(engine1.TransferAsync("127.0.0.1:26001", READ, {desc}, {}, req), SUCCESS);
  // after transfer, src set to 2
  EXPECT_EQ(src, 2);
  src = 1;
  EXPECT_EQ(engine1.TransferAsync("127.0.0.1:26001", WRITE, {desc}, {}, req), SUCCESS);
  EXPECT_EQ(dst, 1);

  EXPECT_EQ(engine1.Disconnect("127.0.0.1:26001"), SUCCESS);
  EXPECT_EQ(engine1.DeregisterMem(handle1), SUCCESS);
  EXPECT_EQ(engine2.DeregisterMem(handle2), SUCCESS);
  engine1.Finalize();
  engine2.Finalize();
}

TEST_F(ChannelPoolSystemTest, ConcurrentTransferSyncAndEviction) {
  llm::AutoCommResRuntimeMock::SetDevice(0);
  Hixl client;
  EXPECT_EQ(client.Initialize("127.0.0.1:30000", options_), SUCCESS);

  llm::AutoCommResRuntimeMock::SetDevice(1);
  Hixl server1;
  EXPECT_EQ(server1.Initialize("127.0.0.1:30001", options_), SUCCESS);
  // set mock device 2
  llm::AutoCommResRuntimeMock::SetDevice(2);
  Hixl server2;
  EXPECT_EQ(server2.Initialize("127.0.0.1:30002", options_), SUCCESS);
  // set mock device 3
  llm::AutoCommResRuntimeMock::SetDevice(3);
  Hixl server3;
  EXPECT_EQ(server3.Initialize("127.0.0.1:30003", options_), SUCCESS);
  // set mock device 4
  llm::AutoCommResRuntimeMock::SetDevice(4);
  Hixl server4;
  EXPECT_EQ(server4.Initialize("127.0.0.1:30004", options_), SUCCESS);

  EXPECT_EQ(client.Connect("127.0.0.1:30003"), SUCCESS);
  EXPECT_EQ(client.Connect("127.0.0.1:30004"), SUCCESS);

  // Register memory for transfer
  hixl::MemDesc mem{};
  // mock mem addr 1234
  mem.addr = 1234;
  // mock mem len 10
  mem.len = 10;
  MemHandle client_handle = nullptr;
  EXPECT_EQ(client.RegisterMem(mem, MEM_DEVICE, client_handle), SUCCESS);

  MemHandle server1_handle = nullptr;
  MemHandle server2_handle = nullptr;
  EXPECT_EQ(server1.RegisterMem(mem, MEM_DEVICE, server1_handle), SUCCESS);
  EXPECT_EQ(server2.RegisterMem(mem, MEM_DEVICE, server2_handle), SUCCESS);
  // set src content 1
  int32_t src = 1;
  // set dst content 2
  int32_t dst = 2;
  TransferOpDesc desc{reinterpret_cast<uintptr_t>(&src), reinterpret_cast<uintptr_t>(&dst), sizeof(int32_t)};

  auto transfer_func = [&client, &desc](const AscendString& server_addr) {
    client.TransferSync(server_addr, READ, {desc});
    // sleep 5000 ms to simulate long transfer task
    std::this_thread::sleep_for(std::chrono::microseconds(5000));
  };

  // Start concurrent transfers on channels 1 and 2
  std::thread transfer_thread1(transfer_func, "127.0.0.1:30001");
  std::thread transfer_thread2(transfer_func, "127.0.0.1:30002");

  // Sleep 200 ms to allow transfers to start and eviction to happen
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  // Verify that channels with ongoing transfers are not evicted
  // Channels 1 and 2 should still be connected because they have ongoing transfers
  // Channels 3 and 4 should be evicted since they have no transfers
  EXPECT_EQ(client.Disconnect("127.0.0.1:30003"), NOT_CONNECTED);
  EXPECT_EQ(client.Disconnect("127.0.0.1:30004"), NOT_CONNECTED);

  // Verify that channels with ongoing transfers are still connected
  EXPECT_EQ(client.Connect("127.0.0.1:30001"), ALREADY_CONNECTED);
  EXPECT_EQ(client.Connect("127.0.0.1:30002"), ALREADY_CONNECTED);

  if (transfer_thread1.joinable()) {
    transfer_thread1.join();
  }
  if (transfer_thread2.joinable()) {
    transfer_thread2.join();
  }

  EXPECT_EQ(client.DeregisterMem(client_handle), SUCCESS);
  EXPECT_EQ(server1.DeregisterMem(server1_handle), SUCCESS);
  EXPECT_EQ(server2.DeregisterMem(server2_handle), SUCCESS);

  client.Finalize();
  server1.Finalize();
  server2.Finalize();
  server3.Finalize();
  server4.Finalize();
}

TEST_F(ChannelPoolSystemTest, ConcurrentTransferAsyncAndEviction) {
  llm::AutoCommResRuntimeMock::SetDevice(0);
  Hixl client;
  EXPECT_EQ(client.Initialize("127.0.0.1:40000", options_), SUCCESS);

  llm::AutoCommResRuntimeMock::SetDevice(1);
  Hixl server1;
  EXPECT_EQ(server1.Initialize("127.0.0.1:40001", options_), SUCCESS);
  // mock device 2
  llm::AutoCommResRuntimeMock::SetDevice(2);
  Hixl server2;
  EXPECT_EQ(server2.Initialize("127.0.0.1:40002", options_), SUCCESS);
  // mock device 3
  llm::AutoCommResRuntimeMock::SetDevice(3);
  Hixl server3;
  EXPECT_EQ(server3.Initialize("127.0.0.1:40003", options_), SUCCESS);
  // mock device 4
  llm::AutoCommResRuntimeMock::SetDevice(4);
  Hixl server4;
  EXPECT_EQ(server4.Initialize("127.0.0.1:40004", options_), SUCCESS);

  EXPECT_EQ(client.Connect("127.0.0.1:40003"), SUCCESS);
  EXPECT_EQ(client.Connect("127.0.0.1:40004"), SUCCESS);

  hixl::MemDesc mem{};
  // mock mem addr 1134
  mem.addr = 1134;
  // mock mem len 11
  mem.len = 11;
  MemHandle client_handle = nullptr;
  EXPECT_EQ(client.RegisterMem(mem, MEM_DEVICE, client_handle), SUCCESS);

  MemHandle server1_handle = nullptr;
  MemHandle server2_handle = nullptr;
  EXPECT_EQ(server1.RegisterMem(mem, MEM_DEVICE, server1_handle), SUCCESS);
  EXPECT_EQ(server2.RegisterMem(mem, MEM_DEVICE, server2_handle), SUCCESS);
  // set src content 1
  int32_t src = 1;
  // set dst content 12
  int32_t dst = 12;
  TransferOpDesc desc{reinterpret_cast<uintptr_t>(&src), reinterpret_cast<uintptr_t>(&dst), sizeof(int32_t)};

  auto transfer_async_func = [&client, &desc](const AscendString& server_addr) {
    TransferReq req = nullptr;
    client.TransferAsync(server_addr, READ, {desc}, {}, req);
  };

  std::thread transfer_thread1(transfer_async_func, "127.0.0.1:40001");
  std::thread transfer_thread2(transfer_async_func, "127.0.0.1:40002");

  // Sleep 200 ms to allow transfers to start and eviction to happen
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  // Verify that channels with ongoing transfers are not evicted
  // Channels 1 and 2 should still be connected because they have ongoing transfers
  // Channels 3 and 4 should be evicted since they have no transfers
  EXPECT_EQ(client.Disconnect("127.0.0.1:40003"), NOT_CONNECTED);
  EXPECT_EQ(client.Disconnect("127.0.0.1:40004"), NOT_CONNECTED); 
  // Verify that channels with ongoing transfers are still connected
  EXPECT_EQ(client.Connect("127.0.0.1:40001"), ALREADY_CONNECTED);
  EXPECT_EQ(client.Connect("127.0.0.1:40002"), ALREADY_CONNECTED);

  if (transfer_thread1.joinable()) {
    transfer_thread1.join();
  }
  if (transfer_thread2.joinable()) {
    transfer_thread2.join();
  }

  EXPECT_EQ(client.DeregisterMem(client_handle), SUCCESS);
  EXPECT_EQ(server1.DeregisterMem(server1_handle), SUCCESS);
  EXPECT_EQ(server2.DeregisterMem(server2_handle), SUCCESS);

  client.Finalize();
  server1.Finalize();
  server2.Finalize();
  server3.Finalize();
  server4.Finalize();
}

// Test multiple concurrent TransferSync calls, ensure none return NOT_CONNECTED or ALREADY_CONNECTED
TEST_F(ChannelPoolSystemTest, ConcurrentTransfersWithoutConnectStatusErrors) {
  llm::AutoCommResRuntimeMock::SetDevice(0);
  Hixl client;
  EXPECT_EQ(client.Initialize("127.0.0.1:50000", options_), SUCCESS);

  llm::AutoCommResRuntimeMock::SetDevice(1);
  Hixl server;
  EXPECT_EQ(server.Initialize("127.0.0.1:50001", options_), SUCCESS);

  // Register memory for transfer
  hixl::MemDesc mem{};
  mem.addr = 1234; // mock memory address 1234
  mem.len = 1024; // mock memory length 1024
  MemHandle client_handle = nullptr;
  EXPECT_EQ(client.RegisterMem(mem, MEM_DEVICE, client_handle), SUCCESS);

  MemHandle server_handle = nullptr;
  EXPECT_EQ(server.RegisterMem(mem, MEM_DEVICE, server_handle), SUCCESS);

  // Atomic variables to track test results
  std::atomic<int> complete_count{0};
  std::atomic<int> not_connected_count{0};
  std::atomic<int> already_connected_count{0};
  // make 30 concurrent threads
  const int total_threads = 30;
  // set timeout to 1000 ms
  const int32_t timeout = 1000;

  // Create a vector of threads
  std::vector<std::thread> threads;
  threads.reserve(total_threads);

  // Prepare transfer data
  int32_t src = 1;
  // mock dst content 2
  int32_t dst = 2;
  TransferOpDesc desc{reinterpret_cast<uintptr_t>(&src), 
                     reinterpret_cast<uintptr_t>(&dst), 
                     sizeof(int32_t)};

  // Launch multiple concurrent threads, all calling TransferSync
  // This simulates the real-world scenario where multiple transfers
  // are initiated and all trigger ConnectWhenTransfer internally
  for (int i = 0; i < total_threads; ++i) {
    threads.emplace_back([&client, &desc, timeout, &complete_count, 
                         &not_connected_count, &already_connected_count]() {
      // Add random delay (max 10 ms) to increase the chance of race conditions
      std::this_thread::sleep_for(std::chrono::microseconds(std::rand() % 10));
      
      // This will trigger ConnectWhenTransfer internally because we haven't connected yet
      Status ret = client.TransferSync("127.0.0.1:50001", READ, {desc}, timeout);
      
      // Count the results
      complete_count++;
      
      if (ret == NOT_CONNECTED) {
        not_connected_count++;
      } else if (ret == ALREADY_CONNECTED) {
        already_connected_count++;
      }
    });
  }

  // Join all threads. If deadlock occurs, this will hang indefinitely
  for (auto& thread : threads) {
    if (thread.joinable()) {
      thread.join();
    }
  }

  // Verify all threads completed
  EXPECT_EQ(complete_count.load(), total_threads) << 
    "Not all concurrent transfer calls completed. " <<
    "Completed: " << complete_count.load() << ", Total: " << total_threads;

  // Verify no Transfer returned NOT_CONNECTED or ALREADY_CONNECTED status
  EXPECT_EQ(not_connected_count.load(), 0) << 
    "Transfer calls should never return NOT_CONNECTED when GlobalResourceConfig is properly set";
  
  EXPECT_EQ(already_connected_count.load(), 0) << 
    "Transfer calls should never return ALREADY_CONNECTED status";

  // Cleanup
  EXPECT_EQ(client.DeregisterMem(client_handle), SUCCESS);
  EXPECT_EQ(server.DeregisterMem(server_handle), SUCCESS);
  client.Finalize();
  server.Finalize();
  
  SUCCEED() << "No NOT_CONNECTED or ALREADY_CONNECTED status returned from " << 
              total_threads << " concurrent TransferSync calls";
}

// Test multiple concurrent TransferAsync calls, ensure none return NOT_CONNECTED or ALREADY_CONNECTED
TEST_F(ChannelPoolSystemTest, ConcurrentAsyncTransfersWithoutConnectStatusErrors) {
  llm::AutoCommResRuntimeMock::SetDevice(0);
  Hixl client;
  EXPECT_EQ(client.Initialize("127.0.0.1:51000", options_), SUCCESS);

  llm::AutoCommResRuntimeMock::SetDevice(1);
  Hixl server;
  EXPECT_EQ(server.Initialize("127.0.0.1:51001", options_), SUCCESS);

  // Register memory for transfer
  hixl::MemDesc mem{};
  mem.addr = 5678; // mock memory address 5678
  mem.len = 2048; // mock memory length 2048
  MemHandle client_handle = nullptr;
  EXPECT_EQ(client.RegisterMem(mem, MEM_DEVICE, client_handle), SUCCESS);

  MemHandle server_handle = nullptr;
  EXPECT_EQ(server.RegisterMem(mem, MEM_DEVICE, server_handle), SUCCESS);

  // Atomic variables to track test results
  std::atomic<int> complete_count{0};
  std::atomic<int> not_connected_count{0};
  std::atomic<int> already_connected_count{0};
  // make 25 concurrent threads
  const int total_threads = 25;

  // Create a vector of threads
  std::vector<std::thread> threads;
  threads.reserve(total_threads);

  // Launch multiple concurrent threads, all calling TransferAsync
  for (int i = 0; i < total_threads; ++i) {
    threads.emplace_back([&client, &complete_count, 
                         &not_connected_count, &already_connected_count]() {
      // Add random delay (max 10 ms) to increase race conditions
      std::this_thread::sleep_for(std::chrono::microseconds(std::rand() % 10));
      
      // Prepare dummy transfer data
      // mock src content 1
      int32_t src = 1;
      // mock dst content 2
      int32_t dst = 2;
      TransferOpDesc desc{reinterpret_cast<uintptr_t>(&src), 
                         reinterpret_cast<uintptr_t>(&dst), 
                         sizeof(int32_t)};
      
      // This will trigger ConnectWhenTransfer internally
      TransferReq req = nullptr;
      Status ret = client.TransferAsync("127.0.0.1:51001", READ, {desc}, {}, req);
      
      complete_count++;
      
      if (ret == NOT_CONNECTED) {
        not_connected_count++;
      } else if (ret == ALREADY_CONNECTED) {
        already_connected_count++;
      }
    });
  }

  // Join all threads
  for (auto& thread : threads) {
    if (thread.joinable()) {
      thread.join();
    }
  }

  EXPECT_EQ(complete_count.load(), total_threads) << 
    "Not all concurrent TransferAsync calls completed";
  
  // Verify no Transfer returned NOT_CONNECTED or ALREADY_CONNECTED status
  EXPECT_EQ(not_connected_count.load(), 0) << 
    "TransferAsync calls should never return NOT_CONNECTED when GlobalResourceConfig is properly set";
  
  EXPECT_EQ(already_connected_count.load(), 0) << 
    "TransferAsync calls should never return ALREADY_CONNECTED status";
  EXPECT_EQ(client.DeregisterMem(client_handle), SUCCESS);
  EXPECT_EQ(server.DeregisterMem(server_handle), SUCCESS);
  client.Finalize();
  server.Finalize();
  
  SUCCEED() << "No NOT_CONNECTED or ALREADY_CONNECTED status returned from " <<
              total_threads << " concurrent TransferAsync calls";
}

}
