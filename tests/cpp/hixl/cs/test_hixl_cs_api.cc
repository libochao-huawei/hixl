/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <vector>
#include <cstdlib>
#include <thread>
#include <chrono>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "hixl_cs_client.h"
#include "common/hixl_cs.h"
#include "hixl/hixl_types.h"
#include "hixl_test.h"
#include "common/ctrl_msg.h"
#include "dlog_pub.h"

using namespace std;
using namespace ::testing;
using ::testing::Invoke;
using ::testing::Mock;

namespace hixl {
static constexpr uint32_t kPort = 16000;
static constexpr uint32_t kEpAddrId0 = 1U;
static constexpr uint32_t kEpAddrId1 = 2U;
static constexpr uint32_t kMemNum = 100U;
static constexpr uint32_t kBackLog = 1024U;
static constexpr int32_t kNUm1 = 1;
static constexpr int32_t kNUm2 = 2;
static std::vector<int32_t> kHostMems(kMemNum, kNUm1);
static std::vector<int32_t> kDeviceMems(kMemNum, kNUm2);
static constexpr uint32_t kListNum = 0U;
static constexpr uint32_t kTimeoutMs = 1000;
static constexpr uint32_t kSleepMs = 10;
static constexpr uint32_t kTimes = 5;
static constexpr int32_t kCtrlMsgType = 1024;
class HixlCSTest : public ::testing::Test {
 protected:
  // 在测试类中设置一些准备工作，如果需要的话
  void SetUp() override {
    EndPointInfo ep0{};
    ep0.protocol = COMM_PROTOCOL_UB_CTP;
    ep0.addr.type = COMM_ADDR_TYPE_ID;
    ep0.addr.id = kEpAddrId0;
    EndPointInfo ep1{};
    ep1.protocol = COMM_PROTOCOL_UB_CTP;
    ep1.addr.type = COMM_ADDR_TYPE_ID;
    ep1.addr.id = kEpAddrId1;

    default_eps.emplace_back(ep0);
    default_eps.emplace_back(ep1);
  }
  // 在测试类中进行清理工作，如果需要的话
  void TearDown() override {
  }

 private:
  std::vector<EndPointInfo> default_eps;

  static void CleanupClient(HixlClientHandle client_handle,
                          MemHandle mem1,
                          MemHandle mem2) {
    if (client_handle == nullptr) {
      return;
    }
    if (mem1 != nullptr) {
      (void)HixlCSClientUnregMem(client_handle, mem1);
    }
    if (mem2 != nullptr) {
      (void)HixlCSClientUnregMem(client_handle, mem2);
    }
    (void)HixlCSClientDestroy(client_handle);
  }

  static Status RegTwoMems(HixlClientHandle client_handle,
                           MemHandle &mem_handle1,
                           MemHandle &mem_handle2) {
    HcclMem mem{};
    mem.size = sizeof(int32_t);
    mem.addr = &kDeviceMems[0];
    auto ret = HixlCSClientRegMem(client_handle, nullptr, &mem, &mem_handle1);
    if (ret != SUCCESS) return ret;

    HcclMem mem2{};
    mem2.type = HCCL_MEM_TYPE_HOST;
    mem2.size = sizeof(int32_t);
    mem2.addr = &kHostMems[0];
    ret = HixlCSClientRegMem(client_handle, "b", &mem2, &mem_handle2);
    return ret;
  }

  static bool FetchTagsOnce(HixlClientHandle client_handle,
                          std::vector<std::string> &tags,
                          uint32_t &list_num) {
    HcclMem *remote_mem_list = nullptr;
    char **mem_tag_list = nullptr;
    list_num = kListNum;

    const Status ret = HixlCSClientGetRemoteMem(client_handle,
                                               &remote_mem_list, &mem_tag_list,
                                               &list_num, kTimeoutMs);
    if (ret != SUCCESS) {
      return false;
    }
    if (remote_mem_list == nullptr || mem_tag_list == nullptr || list_num == 0U) {
      return false;
    }

    tags.clear();
    tags.reserve(list_num);
    for (uint32_t i = 0; i < list_num; ++i) {
      tags.emplace_back(mem_tag_list[i] != nullptr ? mem_tag_list[i] : "");
    }
    return true;
  }

  void ClientTask(const std::string &server_ip, uint16_t server_port, Status &task_status) {
    task_status = FAILED;

    HixlClientHandle client_handle = nullptr;
    auto ret = HixlCSClientCreate(server_ip.c_str(), server_port, &default_eps[0], &default_eps[1], &client_handle);
    if (ret != SUCCESS) {
      std::cerr << "Client create failed!" << std::endl;
      return;
    }

    MemHandle mem_handle1 = nullptr;
    MemHandle mem_handle2 = nullptr;

    ret = RegTwoMems(client_handle, mem_handle1, mem_handle2);
    if (ret != SUCCESS) {
      std::cerr << "Client reg mem failed!" << std::endl;
      CleanupClient(client_handle, mem_handle1, mem_handle2);
      return;
    }

    ret = HixlCSClientConnectSync(client_handle, kTimeoutMs);
    if (ret != SUCCESS) {
      std::cerr << "Client connect failed!" << std::endl;
      CleanupClient(client_handle, mem_handle1, mem_handle2);
      return;
    }

    HcclMem *remote_mem_list = nullptr;
    char **mem_tag_list = nullptr;
    uint32_t list_num = 0;
    ret = HixlCSClientGetRemoteMem(client_handle, &remote_mem_list, &mem_tag_list, &list_num, kTimeoutMs);
    if (ret != SUCCESS) {
      std::cerr << "Client get remote mem failed!" << std::endl;
      CleanupClient(client_handle, mem_handle1, mem_handle2);
      return;
    }
    task_status = SUCCESS;
    CleanupClient(client_handle, mem_handle1, mem_handle2);
  }

  void ClientTaskGetRemoteMemWithoutConnect(const std::string &server_ip, uint16_t server_port,
                                            Status &task_status) {
    task_status = FAILED;
    HixlClientHandle client_handle = nullptr;

    auto ret = HixlCSClientCreate(server_ip.c_str(), server_port, &default_eps[0], &default_eps[1], &client_handle);
    if (ret != SUCCESS) {
      std::cerr << "Client create failed!" << std::endl;
      return;
    }

    // 不 Connect，直接 GetRemoteMem
    HcclMem* remote_mem_list = nullptr;
    char** mem_tag_list = nullptr;  // C API char*** -> 传 &mem_tag_list
    uint32_t list_num = 0;

    ret = HixlCSClientGetRemoteMem(client_handle, &remote_mem_list, &mem_tag_list, &list_num, kTimeoutMs);
    if (ret == SUCCESS) {
      std::cerr << "Unexpected SUCCESS: GetRemoteMem without Connect should fail!" << std::endl;
      HixlCSClientDestroy(client_handle);
      return;
    }

    task_status = SUCCESS;
    HixlCSClientDestroy(client_handle);
  }

  void ClientTaskConnectFailNoServer(const std::string &server_ip, uint16_t server_port,
                                     Status &task_status) {
    task_status = FAILED;
    HixlClientHandle client_handle = nullptr;

    auto ret = HixlCSClientCreate(server_ip.c_str(), server_port, &default_eps[0], &default_eps[1], &client_handle);
    if (ret != SUCCESS) {
      std::cerr << "Client create failed!" << std::endl;
      return;
    }

    // 没有 server listen 时，应失败/超时
    ret = HixlCSClientConnectSync(client_handle, kTimeoutMs);  // 50ms
    if (ret == SUCCESS) {
      std::cerr << "Unexpected SUCCESS: Connect without server should fail!" << std::endl;
      HixlCSClientDestroy(client_handle);
      return;
    }

    task_status = SUCCESS;
    HixlCSClientDestroy(client_handle);
  }

  void ClientTaskGetRemoteMemMultiTimes(const std::string &server_ip, uint16_t server_port,
                                      uint32_t times, Status &task_status) {
    task_status = FAILED;
    HixlClientHandle client_handle = nullptr;
    MemHandle mem1 = nullptr;
    MemHandle mem2 = nullptr;
    Status ret = HixlCSClientCreate(server_ip.c_str(), server_port,
                                    &default_eps[0], &default_eps[1], &client_handle);
    if (ret != SUCCESS) {
      std::cerr << "Client create failed!" << std::endl;
      return;
    }
    ret = RegTwoMems(client_handle, mem1, mem2);
    if (ret != SUCCESS) {
      std::cerr << "Client reg mem failed!" << std::endl;
      CleanupClient(client_handle, mem1, mem2);
      return;
    }
    ret = HixlCSClientConnectSync(client_handle, kTimeoutMs);
    if (ret != SUCCESS) {
      std::cerr << "Client connect failed!" << std::endl;
      CleanupClient(client_handle, mem1, mem2);
      return;
    }
    bool has_baseline = false;
    std::vector<std::string> baseline_tags;
    uint32_t baseline_num = 0;
    for (uint32_t i = 0; i < times; ++i) {
      std::vector<std::string> tags;
      uint32_t list_num = 0;
      if (!FetchTagsOnce(client_handle, tags, list_num)) {
        std::cerr << "Client get remote mem failed at iter=" << i << std::endl;
        CleanupClient(client_handle, mem1, mem2);
        return;
      }
      if (!has_baseline) {
        baseline_tags = tags;
        baseline_num = list_num;
        has_baseline = true;
        continue;
      }
      if (list_num != baseline_num || tags != baseline_tags) {
        std::cerr << "GetRemoteMem tag/list changed at iter=" << i << std::endl;
        CleanupClient(client_handle, mem1, mem2);
        return;
      }
    }
    task_status = SUCCESS;
    CleanupClient(client_handle, mem1, mem2);
  }

  void ClientTaskMultiTimesGetRemoteMem(const std::string &server_ip, uint16_t server_port,
                                        uint32_t times, Status &task_status) {
    ClientTaskGetRemoteMemMultiTimes(server_ip, server_port, times, task_status);
  }

  HixlServerHandle CreateAndListenServer(std::vector<MemHandle> &mem_handles) {
    HixlServerConfig config{};
    HixlServerHandle server_handle = nullptr;
    auto ret = HixlCSServerCreate("127.0.0.1", kPort, &default_eps[0], default_eps.size(), &config, &server_handle);
    if (ret != SUCCESS) {
      return nullptr;
    }

    for (size_t i = 0; i < kHostMems.size(); ++i) {
      HcclMem mem{};
      mem.size = sizeof(int32_t);
      mem.addr = &kHostMems[i];
      mem.type = HCCL_MEM_TYPE_HOST;
      MemHandle mem_handle = nullptr;
      ret = HixlCSServerRegMem(server_handle, std::to_string(i).c_str(), &mem, &mem_handle);
      if (ret != SUCCESS) {
        return nullptr;
      }
      mem_handles.emplace_back(mem_handle);
    }

    for (size_t i = 0; i < kDeviceMems.size(); ++i) {
      HcclMem mem{};
      mem.size = sizeof(int32_t);
      mem.addr = &kDeviceMems[i];
      mem.type = HCCL_MEM_TYPE_DEVICE;
      MemHandle mem_handle = nullptr;
      ret = HixlCSServerRegMem(server_handle, std::to_string(i + kDeviceMems.size()).c_str(), &mem, &mem_handle);
      if (ret != SUCCESS) {
        return nullptr;
      }
      mem_handles.emplace_back(mem_handle);
    }

    ret = HixlCSServerListen(server_handle, kBackLog);
    EXPECT_EQ(ret, SUCCESS);
    std::this_thread::sleep_for(std::chrono::milliseconds(kSleepMs));
    return server_handle;
  }

  void DestroyServerAndUnreg(HixlServerHandle server_handle,
                             const std::vector<MemHandle> &mem_handles) {
    if (server_handle == nullptr) {
      return;
    }

    for (auto mem_handle : mem_handles) {
      EXPECT_EQ(HixlCSServerUnregMem(server_handle, mem_handle), SUCCESS);
    }
    EXPECT_EQ(HixlCSServerDestroy(server_handle), SUCCESS);
  }

  void RunConcurrentClientsMultiTimes(uint32_t client_num,
                                      uint32_t times_per_client,
                                      std::vector<uint32_t> &all_task_ret) {
    std::vector<std::thread> threads;
    threads.reserve(client_num);

    for (uint32_t i = 0; i < client_num; ++i) {
      threads.emplace_back(std::bind(&HixlCSTest::ClientTaskMultiTimesGetRemoteMem,
                                    this, "127.0.0.1", kPort, times_per_client, std::ref(all_task_ret[i])));
    }
    for (auto &t : threads) {
      if (t.joinable()) t.join();
    }
  }
};


TEST_F(HixlCSTest, TestHixlCSMultiClient2Server) {
  std::vector<MemHandle> mem_handles;
  HixlServerHandle server_handle = CreateAndListenServer(mem_handles);
  ASSERT_NE(server_handle, nullptr);

  const uint32_t kConcurrentClientNum = 10;
  std::vector<uint32_t> all_task_ret(kConcurrentClientNum, 1);
  std::vector<std::thread> client_threads;
  for (uint32_t i = 0; i < kConcurrentClientNum; ++i) {
    client_threads.emplace_back(std::bind(&HixlCSTest::ClientTask,
                                         this,
                                         "127.0.0.1", kPort,
                                         std::ref(all_task_ret[i])));
  }
  for (auto& t : client_threads) {
    if (t.joinable()) {
      t.join();
    }
  }
  for (uint32_t i = 0; i < kConcurrentClientNum; ++i) {
    EXPECT_EQ(all_task_ret[i], 0);
  }
  DestroyServerAndUnreg(server_handle, mem_handles);
}

TEST_F(HixlCSTest, TestHixlCSClientBatchPut) {
  HixlServerConfig config{};
  HixlServerHandle server_handle = nullptr;
  auto ret = HixlCSServerCreate("127.0.0.1", kPort, &default_eps[0], default_eps.size(), &config, &server_handle);
  EXPECT_EQ(ret, SUCCESS);
  HcclMem mem = MakeMem(&kHostMems[0], uint64_t{10}, HCCL_MEM_TYPE_HOST);  // 取kHostMems[0]的地址，内存大小为10
  std::array<MemHandle, 3> mem_handles{nullptr, nullptr, nullptr};
  ret = HixlCSServerRegMem(server_handle, nullptr, &mem, &mem_handles[0]);
  EXPECT_EQ(ret, SUCCESS);
  ret = HixlCSServerListen(server_handle, kBackLog);
  EXPECT_EQ(ret, SUCCESS);
  std::this_thread::sleep_for(std::chrono::milliseconds(kSleepMs));  // wait server liste
  HixlClientHandle client_handle = nullptr;
  ret = HixlCSClientCreate("127.0.0.1", kPort, &default_eps[0], &default_eps[1], &client_handle);
  EXPECT_EQ(ret, SUCCESS);
  // 设置用于传输的内存块信息
  HcclMem mem2 = MakeMem(&kHostMems[0] + size_t(1), uint64_t{3}, HCCL_MEM_TYPE_HOST);  // 取kHostMems[0]的地址，之后向右偏移3位，内存大小设置为3
  HcclMem mem3 = MakeMem(&kHostMems[0], uint64_t{10}, HCCL_MEM_TYPE_HOST);  // 取kHostMems[0]的地址，内存大小为10
  ret = HixlCSClientRegMem(client_handle, nullptr, &mem3, &mem_handles[2]);
  EXPECT_EQ(ret, SUCCESS);
  // 注册已经分配过的内存，预期报错
  ret = HixlCSClientRegMem(client_handle, "b", &mem2, &mem_handles[1]);
  EXPECT_EQ(ret, PARAM_INVALID);
  ret = HixlCSClientConnectSync(client_handle, kTimeoutMs);
  EXPECT_EQ(ret, SUCCESS);
  HcclMem *remote_mem_list = nullptr;
  char **mem_tag_list = nullptr;
  uint32_t list_num = 0;
  ret = HixlCSClientGetRemoteMem(client_handle, &remote_mem_list, &mem_tag_list, &list_num, kTimeoutMs);
  EXPECT_EQ(ret, SUCCESS);
  const void *local_mem_list[2] = {mem3.addr, mem2.addr};
  void *remote_addr_list[2] = {mem3.addr, mem2.addr};
  uint64_t len_list[2] = {mem2.size, mem2.size};
  void *complete_handle = nullptr;
  ret = HixlCSClientBatchPut(client_handle, uint32_t{2}, remote_addr_list, local_mem_list, len_list, &complete_handle);  // 内存数组长度为2
  EXPECT_EQ(ret, SUCCESS);
  int32_t status = -1;  // 指定检查的初始状态值
  int32_t *status_out = &status;
  Status st = HixlCSClientQueryCompleteStatus(client_handle, complete_handle, status_out);
  EXPECT_EQ(st, SUCCESS);
  EXPECT_EQ(*status_out, COMPLETED);
  ret = HixlCSClientUnregMem(client_handle, mem_handles[2]);
  EXPECT_EQ(ret, SUCCESS);
  ret = HixlCSClientDestroy(client_handle);
  EXPECT_EQ(ret, SUCCESS);
  ret = HixlCSServerUnregMem(server_handle, mem_handles[0]);
  EXPECT_EQ(ret, SUCCESS);
  ret = HixlCSServerDestroy(server_handle);
  EXPECT_EQ(ret, SUCCESS);
}

TEST_F(HixlCSTest, TestClientGetRemoteMemWithoutConnectFail) {
  HixlServerConfig config{};
  HixlServerHandle server_handle = nullptr;
  auto ret = HixlCSServerCreate("127.0.0.1", kPort, &default_eps[0], default_eps.size(), &config, &server_handle);
  ASSERT_EQ(ret, SUCCESS);

  // server 只需 listen
  ret = HixlCSServerListen(server_handle, kBackLog);
  ASSERT_EQ(ret, SUCCESS);
  std::this_thread::sleep_for(std::chrono::milliseconds(kSleepMs));

  uint32_t result = 1;
  ClientTaskGetRemoteMemWithoutConnect("127.0.0.1", kPort, result);
  EXPECT_EQ(result, 0);

  ret = HixlCSServerDestroy(server_handle);
  EXPECT_EQ(ret, SUCCESS);
}

TEST_F(HixlCSTest, TestClient_ConnectFail_NoServer) {
  // 不创建 server，不 listen，直接连 16000，期望失败
  uint32_t result = 1;
  ClientTaskConnectFailNoServer("127.0.0.1", kPort, result);
  EXPECT_EQ(result, 0);
}

TEST_F(HixlCSTest, TestClientGetRemoteMemMultiTimesSuccess) {
  HixlServerConfig config{};
  HixlServerHandle server_handle = nullptr;
  auto ret = HixlCSServerCreate("127.0.0.1", kPort, &default_eps[0], default_eps.size(), &config, &server_handle);
  ASSERT_EQ(ret, SUCCESS);

  // 注册 mem（确保 GetRemoteMem 返回内容完整）
  HcclMem mem{};
  mem.size = sizeof(int32_t);
  mem.addr = &kDeviceMems[0];
  MemHandle mem_handle = nullptr;
  ret = HixlCSServerRegMem(server_handle, nullptr, &mem, &mem_handle);
  ASSERT_EQ(ret, SUCCESS);

  HcclMem mem2{};
  mem2.type = HCCL_MEM_TYPE_HOST;
  mem2.size = sizeof(int32_t);
  mem2.addr = &kHostMems[0];
  MemHandle mem_handle2 = nullptr;
  ret = HixlCSServerRegMem(server_handle, "a", &mem2, &mem_handle2);
  ASSERT_EQ(ret, SUCCESS);

  ret = HixlCSServerListen(server_handle, kBackLog);
  ASSERT_EQ(ret, SUCCESS);
  std::this_thread::sleep_for(std::chrono::milliseconds(kSleepMs));

  uint32_t result = 1;
  ClientTaskGetRemoteMemMultiTimes("127.0.0.1", kPort, kTimes, result);
  EXPECT_EQ(result, 0);

  ret = HixlCSServerUnregMem(server_handle, mem_handle);
  EXPECT_EQ(ret, SUCCESS);
  ret = HixlCSServerUnregMem(server_handle, mem_handle2);
  EXPECT_EQ(ret, SUCCESS);
  ret = HixlCSServerDestroy(server_handle);
  EXPECT_EQ(ret, SUCCESS);
}

TEST_F(HixlCSTest, TestMultiClientMultiTimesGetRemoteMem) {
  std::vector<MemHandle> mem_handles;
  HixlServerHandle server_handle = CreateAndListenServer(mem_handles);
  ASSERT_NE(server_handle, nullptr);

  const uint32_t kConcurrentClientNum = 10;
  const uint32_t kTimesPerClient = 3;

  std::vector<uint32_t> all_task_ret(kConcurrentClientNum, 1);
  RunConcurrentClientsMultiTimes(kConcurrentClientNum, kTimesPerClient, all_task_ret);

  for (uint32_t i = 0; i < kConcurrentClientNum; ++i) {
    EXPECT_EQ(all_task_ret[i], 0);
  }

  DestroyServerAndUnreg(server_handle, mem_handles);
}

}  // namespace hixl
