/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "depends/ascendcl/src/ascendcl_stub.h"
#include "engine/fabric_mem_engine.h"
#include "fabric_mem/virtual_memory_manager.h"
#include "hixl/hixl_types.h"

namespace hixl {
namespace {
constexpr int32_t kPortRangeStart = 28000;
constexpr int32_t kPortRangeEnd = 28999;
constexpr size_t kTransferSize = 1024U;
constexpr size_t k1GB = 1024UL * 1024UL * 1024UL;
constexpr uint8_t kDataPattern = 0xAAU;
constexpr uint8_t kInitPattern = 0U;
constexpr int32_t kDeviceId0 = 0;
constexpr int32_t kDeviceId1 = 1;
const char *const kEnableFabricMem = "1";
const char *const kEngine1Ip = "127.0.0.1";
constexpr int32_t kTimeoutMs = 1000;

bool IsTcpPortAvailable(int32_t port) {
  if (port <= 0 || port > 65535) {
    return false;
  }
  const int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    return false;
  }
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = htons(static_cast<uint16_t>(port));
  const bool available = (bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == 0);
  (void)close(fd);
  return available;
}

int32_t AllocateFabricMemTestPort() {
  static std::atomic<uint32_t> port_seq{0U};
  for (uint32_t attempt = 0U; attempt < static_cast<uint32_t>(kPortRangeEnd - kPortRangeStart + 1); ++attempt) {
    const uint32_t offset = port_seq.fetch_add(1U, std::memory_order_relaxed);
    const int32_t port = kPortRangeStart + static_cast<int32_t>(offset % (kPortRangeEnd - kPortRangeStart + 1));
    if (IsTcpPortAvailable(port)) {
      return port;
    }
  }
  const int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    return -1;
  }
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = 0;
  if (bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
    (void)close(fd);
    return -1;
  }
  socklen_t addr_len = sizeof(addr);
  if (getsockname(fd, reinterpret_cast<sockaddr *>(&addr), &addr_len) != 0) {
    (void)close(fd);
    return -1;
  }
  const int32_t port = ntohs(addr.sin_port);
  (void)close(fd);
  return port;
}

class FabricMemRuntimeMock : public llm::AclRuntimeStub {
 public:
  static void Install() {
    llm::AclRuntimeStub::SetInstance(std::make_shared<FabricMemRuntimeMock>());
  }

  static void Reset() {
    llm::AclRuntimeStub::Reset();
  }

  static void SetDevice(int32_t device_id) {
    device_id_ = device_id;
  }

  aclError aclrtGetDevice(int32_t *device_id) override {
    *device_id = device_id_;
    return ACL_ERROR_NONE;
  }

  const char *aclrtGetSocName() override {
    return "Ascend910_9391";
  }

 private:
  static inline int32_t device_id_{0};
};
}  // namespace

class FabricMemSTest : public ::testing::Test {
 protected:
  void SetUp() override {
    FabricMemRuntimeMock::Install();
  }

  void TearDown() override {
    FabricMemRuntimeMock::Reset();
  }
};

TEST_F(FabricMemSTest, TestHixlFabricMem) {
  const int32_t port = AllocateFabricMemTestPort();
  ASSERT_GT(port, 0);
  const std::string engine2_ip = std::string(kEngine1Ip) + ":" + std::to_string(port);

  FabricMemRuntimeMock::SetDevice(kDeviceId0);
  FabricMemEngine engine1{AscendString(kEngine1Ip)};
  std::map<AscendString, AscendString> options1;
  options1[OPTION_ENABLE_USE_FABRIC_MEM] = kEnableFabricMem;
  EXPECT_EQ(engine1.Initialize(options1), SUCCESS);

  FabricMemRuntimeMock::SetDevice(kDeviceId1);
  FabricMemEngine engine2{AscendString(engine2_ip.c_str())};
  std::map<AscendString, AscendString> options2;
  options2[OPTION_ENABLE_USE_FABRIC_MEM] = kEnableFabricMem;
  EXPECT_EQ(engine2.Initialize(options2), SUCCESS);

  const size_t size = kTransferSize;
  std::vector<uint8_t> src(size, kDataPattern);
  std::vector<uint8_t> dst(size, kInitPattern);

  MemDesc src_mem{};
  src_mem.addr = reinterpret_cast<uintptr_t>(src.data());
  src_mem.len = size;
  MemHandle src_handle = nullptr;
  EXPECT_EQ(engine1.RegisterMem(src_mem, MEM_DEVICE, src_handle), SUCCESS);

  MemDesc dst_mem{};
  dst_mem.addr = reinterpret_cast<uintptr_t>(dst.data());
  dst_mem.len = size;
  MemHandle dst_handle = nullptr;
  EXPECT_EQ(engine1.RegisterMem(dst_mem, MEM_DEVICE, dst_handle), SUCCESS);

  std::vector<uint8_t> remote_buf(size, kInitPattern);
  MemDesc remote_mem{};
  remote_mem.addr = reinterpret_cast<uintptr_t>(remote_buf.data());
  remote_mem.len = size;
  MemHandle remote_handle = nullptr;
  EXPECT_EQ(engine2.RegisterMem(remote_mem, MEM_DEVICE, remote_handle), SUCCESS);

  const AscendString remote_engine(engine2_ip.c_str());
  EXPECT_EQ(engine1.Connect(remote_engine, kTimeoutMs), SUCCESS);

  TransferOpDesc desc{reinterpret_cast<uintptr_t>(src.data()), reinterpret_cast<uintptr_t>(remote_buf.data()), size};
  EXPECT_EQ(engine1.TransferSync(remote_engine, WRITE, {desc}, kTimeoutMs), SUCCESS);

  TransferOpDesc read_desc{reinterpret_cast<uintptr_t>(dst.data()), reinterpret_cast<uintptr_t>(remote_buf.data()),
                           size};
  EXPECT_EQ(engine1.TransferSync(remote_engine, READ, {read_desc}, kTimeoutMs), SUCCESS);

  for (size_t i = 0U; i < size; ++i) {
    EXPECT_EQ(dst[i], kDataPattern) << "Verification failed at index " << i;
  }

  EXPECT_EQ(engine1.Disconnect(remote_engine, kTimeoutMs), SUCCESS);
  EXPECT_EQ(engine1.DeregisterMem(src_handle), SUCCESS);
  EXPECT_EQ(engine1.DeregisterMem(dst_handle), SUCCESS);
  EXPECT_EQ(engine2.DeregisterMem(remote_handle), SUCCESS);
  engine1.Finalize();
  engine2.Finalize();
}

TEST_F(FabricMemSTest, TestHixlFabricMemAutoConnect) {
  const int32_t port = AllocateFabricMemTestPort();
  ASSERT_GT(port, 0);
  const std::string engine2_ip = std::string(kEngine1Ip) + ":" + std::to_string(port);

  FabricMemRuntimeMock::SetDevice(kDeviceId0);
  FabricMemEngine engine1{AscendString(kEngine1Ip)};
  std::map<AscendString, AscendString> options1;
  options1[OPTION_ENABLE_USE_FABRIC_MEM] = kEnableFabricMem;
  options1[OPTION_AUTO_CONNECT] = "1";
  EXPECT_EQ(engine1.Initialize(options1), SUCCESS);

  FabricMemRuntimeMock::SetDevice(kDeviceId1);
  FabricMemEngine engine2{AscendString(engine2_ip.c_str())};
  std::map<AscendString, AscendString> options2;
  options2[OPTION_ENABLE_USE_FABRIC_MEM] = kEnableFabricMem;
  EXPECT_EQ(engine2.Initialize(options2), SUCCESS);

  const size_t size = kTransferSize;
  std::vector<uint8_t> src(size, kDataPattern);
  std::vector<uint8_t> dst(size, kInitPattern);

  MemDesc src_mem{};
  src_mem.addr = reinterpret_cast<uintptr_t>(src.data());
  src_mem.len = size;
  MemHandle src_handle = nullptr;
  EXPECT_EQ(engine1.RegisterMem(src_mem, MEM_DEVICE, src_handle), SUCCESS);

  MemDesc dst_mem{};
  dst_mem.addr = reinterpret_cast<uintptr_t>(dst.data());
  dst_mem.len = size;
  MemHandle dst_handle = nullptr;
  EXPECT_EQ(engine1.RegisterMem(dst_mem, MEM_DEVICE, dst_handle), SUCCESS);

  std::vector<uint8_t> remote_buf(size, kInitPattern);
  MemDesc remote_mem{};
  remote_mem.addr = reinterpret_cast<uintptr_t>(remote_buf.data());
  remote_mem.len = size;
  MemHandle remote_handle = nullptr;
  EXPECT_EQ(engine2.RegisterMem(remote_mem, MEM_DEVICE, remote_handle), SUCCESS);

  const AscendString remote_engine(engine2_ip.c_str());
  TransferOpDesc desc{reinterpret_cast<uintptr_t>(src.data()), reinterpret_cast<uintptr_t>(remote_buf.data()), size};
  EXPECT_EQ(engine1.TransferSync(remote_engine, WRITE, {desc}, kTimeoutMs), SUCCESS);

  TransferOpDesc read_desc{reinterpret_cast<uintptr_t>(dst.data()), reinterpret_cast<uintptr_t>(remote_buf.data()),
                           size};
  EXPECT_EQ(engine1.TransferSync(remote_engine, READ, {read_desc}, kTimeoutMs), SUCCESS);

  for (size_t i = 0U; i < size; ++i) {
    EXPECT_EQ(dst[i], kDataPattern) << "Sync verification failed at index " << i;
  }

  std::fill(dst.begin(), dst.end(), kInitPattern);

  TransferOpDesc async_write_desc{reinterpret_cast<uintptr_t>(src.data()),
                                  reinterpret_cast<uintptr_t>(remote_buf.data()), size};
  TransferReq write_req = nullptr;
  EXPECT_EQ(engine1.TransferAsync(remote_engine, WRITE, {async_write_desc}, {}, write_req), SUCCESS);
  EXPECT_NE(write_req, nullptr);

  TransferStatus status = TransferStatus::WAITING;
  while (status == TransferStatus::WAITING) {
    EXPECT_EQ(engine1.GetTransferStatus(write_req, status), SUCCESS);
  }
  EXPECT_EQ(status, TransferStatus::COMPLETED);

  TransferOpDesc async_read_desc{reinterpret_cast<uintptr_t>(dst.data()),
                                 reinterpret_cast<uintptr_t>(remote_buf.data()), size};
  TransferReq read_req = nullptr;
  EXPECT_EQ(engine1.TransferAsync(remote_engine, READ, {async_read_desc}, {}, read_req), SUCCESS);
  EXPECT_NE(read_req, nullptr);

  status = TransferStatus::WAITING;
  while (status == TransferStatus::WAITING) {
    EXPECT_EQ(engine1.GetTransferStatus(read_req, status), SUCCESS);
  }
  EXPECT_EQ(status, TransferStatus::COMPLETED);

  for (size_t i = 0U; i < size; ++i) {
    EXPECT_EQ(dst[i], kDataPattern) << "Async verification failed at index " << i;
  }

  EXPECT_EQ(engine1.Disconnect(remote_engine, kTimeoutMs), SUCCESS);
  EXPECT_EQ(engine1.DeregisterMem(src_handle), SUCCESS);
  EXPECT_EQ(engine1.DeregisterMem(dst_handle), SUCCESS);
  EXPECT_EQ(engine2.DeregisterMem(remote_handle), SUCCESS);
  engine1.Finalize();
  engine2.Finalize();
}

TEST_F(FabricMemSTest, TestHixlFabricMemMultiTargetParallel) {
  const int32_t port2 = AllocateFabricMemTestPort();
  ASSERT_GT(port2, 0);
  const int32_t port3 = AllocateFabricMemTestPort();
  ASSERT_GT(port3, 0);
  const std::string engine2_ip = std::string(kEngine1Ip) + ":" + std::to_string(port2);
  const std::string engine3_ip = std::string(kEngine1Ip) + ":" + std::to_string(port3);

  FabricMemRuntimeMock::SetDevice(kDeviceId0);
  FabricMemEngine engine1{AscendString(kEngine1Ip)};
  std::map<AscendString, AscendString> options1;
  options1[OPTION_ENABLE_USE_FABRIC_MEM] = kEnableFabricMem;
  EXPECT_EQ(engine1.Initialize(options1), SUCCESS);

  FabricMemRuntimeMock::SetDevice(kDeviceId1);
  FabricMemEngine engine2{AscendString(engine2_ip.c_str())};
  std::map<AscendString, AscendString> options2;
  options2[OPTION_ENABLE_USE_FABRIC_MEM] = kEnableFabricMem;
  EXPECT_EQ(engine2.Initialize(options2), SUCCESS);

  FabricMemRuntimeMock::SetDevice(kDeviceId0);
  FabricMemEngine engine3{AscendString(engine3_ip.c_str())};
  std::map<AscendString, AscendString> options3;
  options3[OPTION_ENABLE_USE_FABRIC_MEM] = kEnableFabricMem;
  EXPECT_EQ(engine3.Initialize(options3), SUCCESS);

  const size_t size = kTransferSize;
  std::vector<uint8_t> src(size, kDataPattern);
  MemDesc src_mem{};
  src_mem.addr = reinterpret_cast<uintptr_t>(src.data());
  src_mem.len = size;
  MemHandle src_handle = nullptr;
  EXPECT_EQ(engine1.RegisterMem(src_mem, MEM_DEVICE, src_handle), SUCCESS);

  std::vector<uint8_t> remote_buf2(size, kInitPattern);
  MemDesc remote_mem2{};
  remote_mem2.addr = reinterpret_cast<uintptr_t>(remote_buf2.data());
  remote_mem2.len = size;
  MemHandle remote_handle2 = nullptr;
  EXPECT_EQ(engine2.RegisterMem(remote_mem2, MEM_DEVICE, remote_handle2), SUCCESS);

  std::vector<uint8_t> remote_buf3(size, kInitPattern);
  MemDesc remote_mem3{};
  remote_mem3.addr = reinterpret_cast<uintptr_t>(remote_buf3.data());
  remote_mem3.len = size;
  MemHandle remote_handle3 = nullptr;
  EXPECT_EQ(engine3.RegisterMem(remote_mem3, MEM_DEVICE, remote_handle3), SUCCESS);

  const AscendString remote2(engine2_ip.c_str());
  const AscendString remote3(engine3_ip.c_str());
  EXPECT_EQ(engine1.Connect(remote2, kTimeoutMs), SUCCESS);
  EXPECT_EQ(engine1.Connect(remote3, kTimeoutMs), SUCCESS);

  TransferOpDesc desc2{reinterpret_cast<uintptr_t>(src.data()), reinterpret_cast<uintptr_t>(remote_buf2.data()), size};
  TransferOpDesc desc3{reinterpret_cast<uintptr_t>(src.data()), reinterpret_cast<uintptr_t>(remote_buf3.data()), size};

  std::thread t1([&]() { EXPECT_EQ(engine1.TransferSync(remote2, WRITE, {desc2}, kTimeoutMs), SUCCESS); });
  std::thread t2([&]() { EXPECT_EQ(engine1.TransferSync(remote3, WRITE, {desc3}, kTimeoutMs), SUCCESS); });
  t1.join();
  t2.join();

  EXPECT_EQ(engine1.Disconnect(remote2, kTimeoutMs), SUCCESS);
  EXPECT_EQ(engine1.Disconnect(remote3, kTimeoutMs), SUCCESS);
  EXPECT_EQ(engine1.DeregisterMem(src_handle), SUCCESS);
  EXPECT_EQ(engine2.DeregisterMem(remote_handle2), SUCCESS);
  EXPECT_EQ(engine3.DeregisterMem(remote_handle3), SUCCESS);
  engine1.Finalize();
  engine2.Finalize();
  engine3.Finalize();
}

TEST_F(FabricMemSTest, TestHixlFabricMemConcurrentAsync) {
  const int32_t port = AllocateFabricMemTestPort();
  ASSERT_GT(port, 0);
  const std::string engine2_ip = std::string(kEngine1Ip) + ":" + std::to_string(port);

  FabricMemRuntimeMock::SetDevice(kDeviceId0);
  FabricMemEngine engine1{AscendString(kEngine1Ip)};
  std::map<AscendString, AscendString> options1;
  options1[OPTION_ENABLE_USE_FABRIC_MEM] = kEnableFabricMem;
  options1[OPTION_AUTO_CONNECT] = "1";
  EXPECT_EQ(engine1.Initialize(options1), SUCCESS);

  FabricMemRuntimeMock::SetDevice(kDeviceId1);
  FabricMemEngine engine2{AscendString(engine2_ip.c_str())};
  std::map<AscendString, AscendString> options2;
  options2[OPTION_ENABLE_USE_FABRIC_MEM] = kEnableFabricMem;
  EXPECT_EQ(engine2.Initialize(options2), SUCCESS);

  const size_t size = kTransferSize;
  std::vector<uint8_t> src(size, kDataPattern);
  MemDesc src_mem{};
  src_mem.addr = reinterpret_cast<uintptr_t>(src.data());
  src_mem.len = size;
  MemHandle src_handle = nullptr;
  EXPECT_EQ(engine1.RegisterMem(src_mem, MEM_DEVICE, src_handle), SUCCESS);

  std::vector<uint8_t> remote_buf(size, kInitPattern);
  MemDesc remote_mem{};
  remote_mem.addr = reinterpret_cast<uintptr_t>(remote_buf.data());
  remote_mem.len = size;
  MemHandle remote_handle = nullptr;
  EXPECT_EQ(engine2.RegisterMem(remote_mem, MEM_DEVICE, remote_handle), SUCCESS);

  const AscendString remote_engine(engine2_ip.c_str());
  constexpr int kRequestCount = 10;
  TransferReq req_list[kRequestCount];
  std::vector<std::thread> async_threads;

  for (int i = 0; i < kRequestCount; i++) {
    async_threads.emplace_back([&, i]() {
      TransferOpDesc desc{reinterpret_cast<uintptr_t>(src.data()), reinterpret_cast<uintptr_t>(remote_buf.data()),
                          size};
      EXPECT_EQ(engine1.TransferAsync(remote_engine, WRITE, {desc}, {}, req_list[i]), SUCCESS);
      EXPECT_NE(req_list[i], nullptr);
    });
  }
  for (auto &t : async_threads) {
    t.join();
  }

  std::atomic<int> completed{0};
  std::atomic<bool> stop{false};
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  std::vector<std::thread> poll_threads;
  for (int i = 0; i < kRequestCount; i++) {
    poll_threads.emplace_back([&, i]() {
      TransferStatus status = TransferStatus::WAITING;
      while (!stop.load() && status == TransferStatus::WAITING) {
        engine1.GetTransferStatus(req_list[i], status);
        if (status == TransferStatus::COMPLETED) {
          completed.fetch_add(1);
          break;
        } else if (status == TransferStatus::FAILED) {
          break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
    });
  }
  while (std::chrono::steady_clock::now() < deadline) {
    if (completed.load() == kRequestCount) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  stop = true;
  for (auto &t : poll_threads) {
    if (t.joinable()) {
      t.join();
    }
  }
  EXPECT_EQ(completed.load(), kRequestCount);

  EXPECT_EQ(engine1.Disconnect(remote_engine, kTimeoutMs), SUCCESS);
  EXPECT_EQ(engine1.DeregisterMem(src_handle), SUCCESS);
  EXPECT_EQ(engine2.DeregisterMem(remote_handle), SUCCESS);
  engine1.Finalize();
  engine2.Finalize();
}

TEST_F(FabricMemSTest, TestHixlFabricMemDisconnectDuringAsync) {
  const int32_t port = AllocateFabricMemTestPort();
  ASSERT_GT(port, 0);
  const std::string engine2_ip = std::string(kEngine1Ip) + ":" + std::to_string(port);

  FabricMemRuntimeMock::SetDevice(kDeviceId0);
  FabricMemEngine engine1{AscendString(kEngine1Ip)};
  std::map<AscendString, AscendString> options1;
  options1[OPTION_ENABLE_USE_FABRIC_MEM] = kEnableFabricMem;
  EXPECT_EQ(engine1.Initialize(options1), SUCCESS);

  FabricMemRuntimeMock::SetDevice(kDeviceId1);
  FabricMemEngine engine2{AscendString(engine2_ip.c_str())};
  std::map<AscendString, AscendString> options2;
  options2[OPTION_ENABLE_USE_FABRIC_MEM] = kEnableFabricMem;
  EXPECT_EQ(engine2.Initialize(options2), SUCCESS);

  const size_t size = kTransferSize;
  std::vector<uint8_t> src(size, kDataPattern);
  MemDesc src_mem{};
  src_mem.addr = reinterpret_cast<uintptr_t>(src.data());
  src_mem.len = size;
  MemHandle src_handle = nullptr;
  EXPECT_EQ(engine1.RegisterMem(src_mem, MEM_DEVICE, src_handle), SUCCESS);

  std::vector<uint8_t> remote_buf(size, kInitPattern);
  MemDesc remote_mem{};
  remote_mem.addr = reinterpret_cast<uintptr_t>(remote_buf.data());
  remote_mem.len = size;
  MemHandle remote_handle = nullptr;
  EXPECT_EQ(engine2.RegisterMem(remote_mem, MEM_DEVICE, remote_handle), SUCCESS);

  const AscendString remote_engine(engine2_ip.c_str());
  EXPECT_EQ(engine1.Connect(remote_engine, kTimeoutMs), SUCCESS);

  TransferOpDesc desc{reinterpret_cast<uintptr_t>(src.data()), reinterpret_cast<uintptr_t>(remote_buf.data()), size};
  TransferReq req = nullptr;
  EXPECT_EQ(engine1.TransferAsync(remote_engine, WRITE, {desc}, {}, req), SUCCESS);
  EXPECT_NE(req, nullptr);

  EXPECT_EQ(engine1.Disconnect(remote_engine, kTimeoutMs), SUCCESS);

  TransferStatus status = TransferStatus::WAITING;
  Status get_status_ret = engine1.GetTransferStatus(req, status);
  // After Disconnect, ReleasePendingAsyncLeasesLocked erases the request from req_map_,
  // so GetTransferStatus may return PARAM_INVALID (request not found) in addition to
  // NOT_CONNECTED, COMPLETED, or FAILED.
  EXPECT_TRUE(get_status_ret == NOT_CONNECTED || get_status_ret == PARAM_INVALID ||
              status == TransferStatus::COMPLETED || status == TransferStatus::FAILED);

  EXPECT_EQ(engine1.DeregisterMem(src_handle), SUCCESS);
  EXPECT_EQ(engine2.DeregisterMem(remote_handle), SUCCESS);
  engine1.Finalize();
  engine2.Finalize();
}

}  // namespace hixl
