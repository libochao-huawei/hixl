/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software and/or redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <cstdio>
#include <chrono>
#include <thread>
#include <iostream>
#include <vector>
#include <arpa/inet.h>
#include <map>
#include <cstring>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include "nlohmann/json.hpp"
#include "common/tcp_client_server.h"
#include "acl/acl.h"
#include "cs/hixl_cs.h"
#include "hixl/common/hixl_log.h"
#include "hixl/hixl_types.h"

using json = nlohmann::json;

void from_json(const json &j, EndpointLocType &l) {
  std::string s = j.get<std::string>();
  if (s == "host") {
    l = ENDPOINT_LOC_TYPE_HOST;
  } else {
    l = ENDPOINT_LOC_TYPE_DEVICE;
  }
}

void from_json(const json &j, CommProtocol &p) {
  std::string s = j.get<std::string>();
  if (s == "hccs") {
    p = COMM_PROTOCOL_HCCS;
  } else if (s == "roce") {
    p = COMM_PROTOCOL_ROCE;
  } else if (s == "UB_CTP") {
    p = COMM_PROTOCOL_UBC_CTP;
  } else if (s == "UB_TP") {
    p = COMM_PROTOCOL_UBC_TP;
  } else {
    p = COMM_PROTOCOL_RESERVED;
  }
}

void from_json(const json &j, EndpointDesc &info) {
  j.at("location").get_to(info.loc.locType);
  j.at("protocol").get_to(info.protocol);
  std::string addr;
  j.at("addr").get_to(addr);
  if (info.protocol == COMM_PROTOCOL_ROCE) {
    if (inet_pton(AF_INET, addr.c_str(), &info.commAddr.addr) == 1) {
      info.commAddr.type = COMM_ADDR_TYPE_IP_V4;
    } else if (inet_pton(AF_INET6, addr.c_str(), &info.commAddr.addr6) == 1) {
      info.commAddr.type = COMM_ADDR_TYPE_IP_V6;
    } else {
      info.commAddr.type = COMM_ADDR_TYPE_RESERVED;
    }
  }
}

namespace {
constexpr int32_t kWaitTransTime = 20;
constexpr int32_t kClientConnectTimeoutMs = 5000;
constexpr int32_t kExpectedArgCnt = 10;
constexpr uint32_t kArgIndexDeviceId = 1;
constexpr uint32_t kArgIndexLocalEngine = 2;
constexpr uint32_t kArgIndexRemoteEngine = 3;
constexpr uint32_t kArgIndexTcpPort = 4;
constexpr uint32_t kArgIndexTransferMode = 5;
constexpr uint32_t kArgIndexTransferOp = 6;
constexpr uint32_t kArgIndexLocalCommRes = 7;
constexpr uint32_t kArgIndexRemoteCommRes = 8;
constexpr uint32_t kArgIndexTestCase = 9;

constexpr uint32_t kTransferMemSize = 134217728;  // 128M
constexpr uint32_t kBaseBlockSize = 262144;       // 0.25M
constexpr uint32_t kExecuteRepeatNum = 5;
constexpr int32_t kPortMaxValue = 65535;
constexpr int32_t kBackLog = 1024;
constexpr const char *kServerMemTagName = "server_mem";
constexpr const char *kClientMemTagName = "client_mem";

constexpr int32_t kScene5TransferCount = 3;     // 场景5: 传输次数
constexpr int32_t kScene6TransferCount = 5000;   // 场景6: 传输5000次
constexpr int32_t kScene7DestroyDelayMs = 500;  // 场景7: 传输后销毁延迟(ms)

#define CHECK_ACL_RETURN(x)                                                           \
  do {                                                                                \
    aclError __ret = x;                                                               \
    if (__ret != ACL_ERROR_NONE) {                                                    \
      std::cerr << __FILE__ << ":" << __LINE__ << " aclError:" << __ret << std::endl; \
      return __ret;                                                                   \
    }                                                                                 \
  } while (0)

struct Args {
  int32_t device_id;
  std::string local_engine;
  std::string remote_engine;
  uint16_t tcp_port;
  std::string transfer_mode;
  std::string transfer_op;
  std::string local_comm_res;
  std::string remote_comm_res;
  int32_t test_case;  // 5, 6, 7, 8
};

int32_t InitEndPointInfo(const std::string &comm_res, EndpointDesc &ep) {
  try {
    ep = json::parse(comm_res).get<EndpointDesc>();
  } catch (const std::exception &e) {
    (void)printf("Failed to parse json:%s\n", e.what());
    return -1;
  }
  return 0;
}

// 场景5: 传输一次后，server解注册内存，再次传输测试
// 完整流程: client传输 -> server解注册内存 -> client再次传输(预期失败)
int32_t Scene5ClientTest(HixlClientHandle client_handle, uint8_t *local_addr, const std::string &transfer_op) {
  (void)printf("[SCENE5] Client: First transfer.\n");

  HcommMem *remote_mem_list = nullptr;
  char **mem_tag_list = nullptr;
  uint32_t list_num = 0U;
  auto ret = HixlCSClientGetRemoteMem(client_handle, &remote_mem_list, &mem_tag_list, &list_num, kClientConnectTimeoutMs);
  if (ret != HIXL_SUCCESS) {
    (void)printf("[SCENE5 ERROR] HixlCSClientGetRemoteMem failed, ret = %u\n", ret);
    return -1;
  }

  std::map<std::string, HcommMem> server_mems;
  for (uint32_t i = 0; i < list_num; ++i) {
    server_mems[mem_tag_list[i]] = remote_mem_list[i];
  }
  uint8_t *remote_addr = static_cast<uint8_t *>(server_mems[kServerMemTagName].addr);

  // 第一次传输
  auto block_size = kBaseBlockSize;
  auto trans_num = kTransferMemSize / block_size;
  std::vector<HixlOneSideOpDesc> desc_list(trans_num);
  for (uint32_t j = 0; j < trans_num; j++) {
    desc_list[j].local_buf = local_addr + j * block_size;
    desc_list[j].remote_buf = remote_addr + j * block_size;
    desc_list[j].len = block_size;
  }

  void *complete_handle = nullptr;
  if (transfer_op == "write") {
    ret = HixlCSClientBatchPutAsync(client_handle, trans_num, desc_list.data(), &complete_handle);
  } else {
    ret = HixlCSClientBatchGetAsync(client_handle, trans_num, desc_list.data(), &complete_handle);
  }
  if (ret != HIXL_SUCCESS) {
    (void)printf("[SCENE5 ERROR] First transfer failed, ret = %u\n", ret);
    return -1;
  }

  HixlCompleteStatus status = HIXL_COMPLETE_STATUS_WAITING;
  while (true) {
    ret = HixlCSClientQueryCompleteStatus(client_handle, complete_handle, &status);
    if (status == HIXL_COMPLETE_STATUS_COMPLETED) {
      break;
    } else if (status == HIXL_COMPLETE_STATUS_WAITING) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      continue;
    }
    if (ret != HIXL_SUCCESS) {
      (void)printf("[SCENE5 ERROR] First transfer query failed, ret = %u\n", ret);
      return -1;
    }
  }
  (void)printf("[SCENE5] First transfer completed successfully.\n");

  // 等待server解注册内存后，再次尝试传输
  (void)printf("[SCENE5] Client: Waiting for server to unregister memory...\n");
  std::this_thread::sleep_for(std::chrono::seconds(2));

  // 尝试再次传输 - 预期会失败
  (void)printf("[SCENE5] Client: Attempting second transfer after server unregister...\n");
  complete_handle = nullptr;
  if (transfer_op == "write") {
    ret = HixlCSClientBatchPutAsync(client_handle, trans_num, desc_list.data(), &complete_handle);
  } else {
    ret = HixlCSClientBatchGetAsync(client_handle, trans_num, desc_list.data(), &complete_handle);
  }

  if (ret != HIXL_SUCCESS) {
    (void)printf("[SCENE5] EXPECTED: Second transfer after unregister failed, ret = %u\n", ret);
    (void)printf("[SCENE5] PASS: Server correctly rejects transfer on unregistered memory.\n");
    return 0;
  }

  // 如果传输没有立即失败，查询状态
  status = HIXL_COMPLETE_STATUS_WAITING;
  int32_t wait_count = 0;
  while (wait_count < 5000) {  // 最多等待5秒
    ret = HixlCSClientQueryCompleteStatus(client_handle, complete_handle, &status);
    if (status == HIXL_COMPLETE_STATUS_COMPLETED) {
      (void)printf("[SCENE5 WARNING] Second transfer succeeded unexpectedly after unregister!\n");
      return -1;
    } else if (status == HIXL_COMPLETE_STATUS_FAILED || ret != HIXL_SUCCESS) {
      (void)printf("[SCENE5] EXPECTED: Second transfer failed after unregister, ret = %u\n", ret);
      (void)printf("[SCENE5] PASS: Server correctly rejects transfer on unregistered memory.\n");
      return 0;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    wait_count++;
  }

  (void)printf("[SCENE5 ERROR] Second transfer timeout after unregister\n");
  return -1;
}

// 场景6: 同一个client-server建链，传输5000次，测试资源上限
int32_t Scene6Test(HixlClientHandle client_handle, uint8_t *local_addr,
                   const std::string &transfer_op) {
  (void)printf("[SCENE6] Start: Transfer 5000 times on single connection.\n");

  HcommMem *remote_mem_list = nullptr;
  char **mem_tag_list = nullptr;
  uint32_t list_num = 0U;
  auto ret = HixlCSClientGetRemoteMem(client_handle, &remote_mem_list, &mem_tag_list, &list_num, kClientConnectTimeoutMs);
  if (ret != HIXL_SUCCESS) {
    (void)printf("[SCENE6 ERROR] HixlCSClientGetRemoteMem failed, ret = %u\n", ret);
    return -1;
  }

  std::map<std::string, HcommMem> server_mems;
  for (uint32_t i = 0; i < list_num; ++i) {
    server_mems[mem_tag_list[i]] = remote_mem_list[i];
  }
  uint8_t *remote_addr = static_cast<uint8_t *>(server_mems[kServerMemTagName].addr);

  auto block_size = kBaseBlockSize;
  auto trans_num = kTransferMemSize / block_size;

  int32_t success_count = 0;
  int32_t fail_count = 0;

  for (int32_t i = 0; i < kScene6TransferCount; i++) {
    std::vector<HixlOneSideOpDesc> desc_list(trans_num);
    for (uint32_t j = 0; j < trans_num; j++) {
      desc_list[j].local_buf = local_addr + j * block_size;
      desc_list[j].remote_buf = remote_addr + j * block_size;
      desc_list[j].len = block_size;
    }

    void *complete_handle = nullptr;
    if (transfer_op == "write") {
      ret = HixlCSClientBatchPutAsync(client_handle, trans_num, desc_list.data(), &complete_handle);
    } else {
      ret = HixlCSClientBatchGetAsync(client_handle, trans_num, desc_list.data(), &complete_handle);
    }

    if (ret != HIXL_SUCCESS) {
      fail_count++;
      (void)printf("[SCENE6] Transfer %d failed, ret = %u\n", i + 1, ret);
      continue;
    }

    HixlCompleteStatus status = HIXL_COMPLETE_STATUS_WAITING;
    while (true) {
      ret = HixlCSClientQueryCompleteStatus(client_handle, complete_handle, &status);
      if (status == HIXL_COMPLETE_STATUS_COMPLETED) {
        break;
      } else if (status == HIXL_COMPLETE_STATUS_WAITING) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        continue;
      }
      if (ret != HIXL_SUCCESS) {
        fail_count++;
        break;
      }
    }

    if (status == HIXL_COMPLETE_STATUS_COMPLETED) {
      success_count++;
    } else {
      fail_count++;
    }

    if ((i + 1) % 500 == 0) {
      (void)printf("[SCENE6] Progress: %d/%d, success: %d, fail: %d\n",
                   i + 1, kScene6TransferCount, success_count, fail_count);
    }
  }

  (void)printf("[SCENE6] Transfer test completed. Success: %d, Failed: %d\n", success_count, fail_count);

  if (fail_count > 0) {
    (void)printf("[SCENE6] Some transfers failed - may have hit resource limit.\n");
  } else {
    (void)printf("[SCENE6] All 5000 transfers succeeded - no resource limit reached.\n");
  }

  return 0;
}

// 场景7: 传输过程中销毁client，测试server能否自动回收链路资源
int32_t Scene7Test(HixlClientHandle client_handle, uint8_t *local_addr,
                   HixlServerHandle server_handle, MemHandle mem_handle,
                   const std::string &transfer_op, bool is_client) {
  (void)server_handle;  // suppress unused warning
  (void)mem_handle;    // suppress unused warning
  (void)printf("[SCENE7] Start: Destroy client during transfer, check server resource recovery.\n");

  if (is_client) {
    // Client侧：发起传输，然后销毁client
    HcommMem *remote_mem_list = nullptr;
    char **mem_tag_list = nullptr;
    uint32_t list_num = 0U;
    auto ret = HixlCSClientGetRemoteMem(client_handle, &remote_mem_list, &mem_tag_list, &list_num, kClientConnectTimeoutMs);
    if (ret != HIXL_SUCCESS) {
      (void)printf("[SCENE7 ERROR] HixlCSClientGetRemoteMem failed, ret = %u\n", ret);
      return -1;
    }

    std::map<std::string, HcommMem> server_mems;
    for (uint32_t i = 0; i < list_num; ++i) {
      server_mems[mem_tag_list[i]] = remote_mem_list[i];
    }
    uint8_t *remote_addr = static_cast<uint8_t *>(server_mems[kServerMemTagName].addr);

    auto block_size = kBaseBlockSize * 4;  // 更大的block加快传输
    auto trans_num = kTransferMemSize / block_size;
    std::vector<HixlOneSideOpDesc> desc_list(trans_num);
    for (uint32_t j = 0; j < trans_num; j++) {
      desc_list[j].local_buf = local_addr + j * block_size;
      desc_list[j].remote_buf = remote_addr + j * block_size;
      desc_list[j].len = block_size;
    }

    void *complete_handle = nullptr;
    if (transfer_op == "write") {
      ret = HixlCSClientBatchPutAsync(client_handle, trans_num, desc_list.data(), &complete_handle);
    } else {
      ret = HixlCSClientBatchGetAsync(client_handle, trans_num, desc_list.data(), &complete_handle);
    }

    if (ret != HIXL_SUCCESS) {
      (void)printf("[SCENE7] Transfer init failed, ret = %u\n", ret);
      return -1;
    }

    (void)printf("[SCENE7] Transfer started, waiting %d ms before destroy...\n", kScene7DestroyDelayMs);
    std::this_thread::sleep_for(std::chrono::milliseconds(kScene7DestroyDelayMs));

    // 销毁client
    (void)printf("[SCENE7] Destroying client...\n");
    HixlCSClientDestroy(client_handle);
    (void)printf("[SCENE7] Client destroyed.\n");

    return 0;
  } else {
    // Server侧：等待一段时间后，检查是否有异常client连接
    (void)printf("[SCENE7] Server waiting for client to destroy...\n");
    std::this_thread::sleep_for(std::chrono::seconds(3));
    (void)printf("[SCENE7] Server still running. Client resources should be cleaned up automatically.\n");
    return 0;
  }
}

// 场景8: 传输过程中kill掉client进程，测试server能否自动回收链路资源
// 这个场景需要fork子进程来模拟client，被kill后父进程检测
int32_t Scene8Test(const Args &args, bool is_client) {
  (void)printf("[SCENE8] Start: Kill client process during transfer, check server resource recovery.\n");

  if (!is_client) {
    // Server端不需要特殊处理，server会一直等待
    (void)printf("[SCENE8] Server mode - will wait for client crash detection.\n");
    std::this_thread::sleep_for(std::chrono::seconds(10));
    (void)printf("[SCENE8] Server test complete.\n");
    return 0;
  }

  // Client端：fork子进程执行传输，父进程等待后kill子进程
  pid_t pid = fork();
  if (pid < 0) {
    (void)printf("[SCENE8 ERROR] Fork failed\n");
    return -1;
  }

  if (pid == 0) {
    // 子进程：执行传输
    (void)printf("[SCENE8] Child process started, running transfer...\n");

    EndpointDesc local_ep;
    EndpointDesc remote_ep;
    if (InitEndPointInfo(args.local_comm_res, local_ep) != 0 ||
        InitEndPointInfo(args.remote_comm_res, remote_ep) != 0) {
      exit(-1);
    }

    HixlClientHandle client_handle = nullptr;
    std::string ip = args.remote_engine.substr(0U, args.remote_engine.find(':'));
    int32_t port = std::stoi(args.remote_engine.substr(args.remote_engine.find(':') + 1U));

    HixlClientDesc client_desc = {.server_ip = ip.c_str(),
                                  .server_port = static_cast<uint32_t>(port),
                                  .local_endpoint = &local_ep,
                                  .remote_endpoint = &remote_ep};
    HixlClientConfig client_config{};
    auto ret = HixlCSClientCreate(&client_desc, &client_config, &client_handle);
    if (ret != HIXL_SUCCESS) {
      (void)printf("[SCENE8 ERROR] HixlCSClientCreate failed, ret = %u\n", ret);
      exit(-1);
    }

    // 注册内存
    MemHandle mem_handle = nullptr;
    HcommMem mem{};
    bool is_host = (args.transfer_mode == "h2d" || args.transfer_mode == "h2h");
    aclrtMemcpyKind copy_kind = ACL_MEMCPY_HOST_TO_HOST;
    aclError acl_ret = ACL_ERROR_NONE;
    (void)copy_kind;  // suppress unused warning
    (void)acl_ret;    // suppress unused warning

    if (is_host) {
      void *tmp = malloc(kTransferMemSize);
      mem.addr = tmp;
      copy_kind = ACL_MEMCPY_HOST_TO_HOST;
      mem.type = HCCL_MEM_TYPE_HOST;
      mem.size = kTransferMemSize;
    } else {
      acl_ret = aclrtMalloc(&mem.addr, kTransferMemSize, ACL_MEM_MALLOC_HUGE_ONLY);
      copy_kind = (args.transfer_op == "read") ? ACL_MEMCPY_DEVICE_TO_HOST : ACL_MEMCPY_HOST_TO_DEVICE;
      mem.type = HCCL_MEM_TYPE_DEVICE;
      mem.size = kTransferMemSize;
    }

    ret = HixlCSClientRegMem(client_handle, kClientMemTagName, &mem, &mem_handle);
    if (ret != HIXL_SUCCESS) {
      exit(-1);
    }

    // 建链
    ret = HixlCSClientConnect(client_handle, kClientConnectTimeoutMs);
    if (ret != HIXL_SUCCESS) {
      exit(-1);
    }

    // 获取server内存
    HcommMem *remote_mem_list = nullptr;
    char **mem_tag_list = nullptr;
    uint32_t list_num = 0U;
    ret = HixlCSClientGetRemoteMem(client_handle, &remote_mem_list, &mem_tag_list, &list_num, kClientConnectTimeoutMs);
    if (ret != HIXL_SUCCESS) {
      exit(-1);
    }

    std::map<std::string, HcommMem> server_mems;
    for (uint32_t i = 0; i < list_num; ++i) {
      server_mems[mem_tag_list[i]] = remote_mem_list[i];
    }
    uint8_t *remote_addr = static_cast<uint8_t *>(server_mems[kServerMemTagName].addr);

    auto block_size = kBaseBlockSize * 4;
    auto trans_num = kTransferMemSize / block_size;
    std::vector<HixlOneSideOpDesc> desc_list(trans_num);
    for (uint32_t j = 0; j < trans_num; j++) {
      desc_list[j].local_buf = static_cast<uint8_t *>(mem.addr) + j * block_size;
      desc_list[j].remote_buf = remote_addr + j * block_size;
      desc_list[j].len = block_size;
    }

    void *complete_handle = nullptr;
    if (args.transfer_op == "write") {
      ret = HixlCSClientBatchPutAsync(client_handle, trans_num, desc_list.data(), &complete_handle);
    } else {
      ret = HixlCSClientBatchGetAsync(client_handle, trans_num, desc_list.data(), &complete_handle);
    }

    if (ret != HIXL_SUCCESS) {
      exit(-1);
    }

    (void)printf("[SCENE8] Child transfer started, waiting for kill signal...\n");

    // 子进程进入无限等待，让父进程kill
    while (true) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
  }

  // 父进程：等待一段时间后kill子进程
  (void)printf("[SCENE8] Parent: Child pid = %d, waiting 1 second before kill...\n", pid);
  std::this_thread::sleep_for(std::chrono::seconds(1));

  (void)printf("[SCENE8] Parent: Killing child process %d\n", pid);
  kill(pid, SIGKILL);

  int status;
  waitpid(pid, &status, 0);
  (void)printf("[SCENE8] Parent: Child process killed.\n");

  return 0;
}

void ClientFinalize(HixlClientHandle client_handle, const std::vector<MemHandle> &handles) {
  for (auto handle : handles) {
    if (handle != nullptr) {
      HixlCSClientUnregMem(client_handle, handle);
    }
  }

  if (client_handle != nullptr) {
    HixlCSClientDestroy(client_handle);
  }
}

void ServerFinalize(HixlClientHandle server_handle, const std::vector<MemHandle> &handles) {
  for (auto handle : handles) {
    if (handle != nullptr) {
      HixlCSServerUnregMem(server_handle, handle);
    }
  }

  if (server_handle != nullptr) {
    HixlCSServerDestroy(server_handle);
  }
}

int32_t RunClient(const Args &args) {
  (void)printf("[INFO] client start, test case: %d\n", args.test_case);

  // 通过TCP接收Server侧的内存地址
  TCPServer tcp_server;
  if (!tcp_server.StartServer(args.tcp_port)) {
    (void)printf("[ERROR] Failed to start TCP server.\n");
    return -1;
  }
  (void)printf("[INFO] TCP server started.\n");
  if (!tcp_server.AcceptConnection()) {
    return -1;
  }

  // 1. 初始化
  EndpointDesc local_ep;
  EndpointDesc remote_ep;
  if (InitEndPointInfo(args.local_comm_res, local_ep) != 0 || InitEndPointInfo(args.remote_comm_res, remote_ep) != 0) {
    (void)printf("[ERROR] Initialize EndPoint list failed\n");
    return -1;
  }
  HixlClientHandle client_handle = nullptr;
  std::string ip = args.remote_engine.substr(0U, args.remote_engine.find(':'));
  int32_t port = std::stoi(args.remote_engine.substr(args.remote_engine.find(':') + 1U));
  HixlClientDesc client_desc = {.server_ip = ip.c_str(),
                                .server_port = static_cast<uint32_t>(port),
                                .local_endpoint = &local_ep,
                                .remote_endpoint = &remote_ep};
  HixlClientConfig client_config{};
  auto ret = HixlCSClientCreate(&client_desc, &client_config, &client_handle);
  if (ret != HIXL_SUCCESS) {
    (void)printf("[ERROR] HixlCSClientCreate failed, ret = %u\n", ret);
    return -1;
  }

  // 2. 注册内存地址
  MemHandle mem_handle = nullptr;
  HcommMem mem{};
  aclrtMemcpyKind copy_kind = ACL_MEMCPY_HOST_TO_HOST;
  aclError acl_ret = ACL_ERROR_NONE;
  bool is_host = (args.transfer_mode == "h2d" || args.transfer_mode == "h2h");
  (void)copy_kind;  // suppress unused warning
  (void)acl_ret;    // suppress unused warning
  if (is_host) {
    void *tmp = nullptr;
    tmp = malloc(kTransferMemSize);
    mem.addr = tmp;
    copy_kind = ACL_MEMCPY_HOST_TO_HOST;
    mem.type = HCCL_MEM_TYPE_HOST;
    mem.size = kTransferMemSize;
    if (tmp == nullptr) {
      HIXL_LOGE(hixl::RESOURCE_EXHAUSTED, "Client host addr malloc failed.");
      return hixl::RESOURCE_EXHAUSTED;
    }
  } else {
    acl_ret = aclrtMalloc(&mem.addr, kTransferMemSize, ACL_MEM_MALLOC_HUGE_ONLY);
    if (args.transfer_op == "read") {
      copy_kind = ACL_MEMCPY_DEVICE_TO_HOST;
    } else {
      copy_kind = ACL_MEMCPY_HOST_TO_DEVICE;
    }
    mem.type = HCCL_MEM_TYPE_DEVICE;
    mem.size = kTransferMemSize;
    if (acl_ret != ACL_ERROR_NONE) {
      (void)printf("[ERROR] aclrtMalloc failed, ret = %d\n", acl_ret);
      ClientFinalize(client_handle, {mem_handle});
      return -1;
    }
  }
  ret = HixlCSClientRegMem(client_handle, kClientMemTagName, &mem, &mem_handle);
  if (ret != HIXL_SUCCESS) {
    (void)printf("[ERROR] HixlCSClientRegMem failed, ret = %u\n", ret);
    ClientFinalize(client_handle, {mem_handle});
    return -1;
  }
  (void)printf("[INFO] The client memory has been registered.\n");

  // 3. 建链
  ret = HixlCSClientConnect(client_handle, kClientConnectTimeoutMs);
  if (ret != HIXL_SUCCESS) {
    ClientFinalize(client_handle, {});
    (void)printf("[ERROR] HixlCSClientConnect failed, ret = %u\n", ret);
    return -1;
  }

  // 根据test_case执行不同的测试
  int32_t test_ret = 0;
  switch (args.test_case) {
    case 5: {
      // 场景5: 第一次传输 -> server解注册内存 -> 再次传输(预期失败)
      (void)printf("[INFO] Scene5: First transfer, then server unregister, then second transfer...\n");
      test_ret = Scene5ClientTest(client_handle, static_cast<uint8_t *>(mem.addr), args.transfer_op);
      break;
    }
    case 6:
      test_ret = Scene6Test(client_handle, static_cast<uint8_t *>(mem.addr), args.transfer_op);
      break;
    case 7:
      // 场景7需要server_handle，client单独无法测试完整场景
      (void)printf("[INFO] Scene7: Transfer then destroy client...\n");
      test_ret = Scene7Test(client_handle, static_cast<uint8_t *>(mem.addr), nullptr, mem_handle,
                            args.transfer_op, true);
      // 场景7中client已被销毁，不会执行到这里
      break;
    case 8:
      test_ret = Scene8Test(args, true);
      break;
    default:
      (void)printf("[ERROR] Unknown test case: %d\n", args.test_case);
      test_ret = -1;
      break;
  }

  // 场景5: 传输完成后保持连接，让server有时间解注册内存
  if (args.test_case == 5) {
    // 通知server第一次传输完成，但不断开连接
    (void)tcp_server.SendTaskStatus();
    // Scene5ClientTest中已经完成了第二次传输测试
    // 现在清理资源并断开连接
    ClientFinalize(client_handle, {mem_handle});
    tcp_server.DisConnectClient();
    tcp_server.StopServer();
    (void)printf("[INFO] Scene5 client test completed.\n");
    return test_ret;
  }

  // 其他场景正常清理
  ClientFinalize(client_handle, {mem_handle});

  // 通过TCP通知Server侧已传输完成
  (void)tcp_server.SendTaskStatus();
  tcp_server.DisConnectClient();
  tcp_server.StopServer();
  (void)printf("[INFO] Client test end, result: %d\n", test_ret);
  return test_ret;
}

int32_t RunServer(const Args &args) {
  (void)printf("[INFO] server start, test case: %d\n", args.test_case);

  // 特殊处理场景8：server端不需要特殊逻辑
  if (args.test_case == 8) {
    return Scene8Test(args, false);
  }

  // 1. 初始化
  EndpointDesc ep;
  if (InitEndPointInfo(args.local_comm_res, ep) != 0) {
    (void)printf("[ERROR] Initialize EndPoint list failed\n");
    return -1;
  }

  HixlServerHandle server_handle = nullptr;
  std::string ip = args.local_engine.substr(0, args.local_engine.find(':'));
  int32_t port = std::stoi(args.local_engine.substr(args.local_engine.find(':') + 1));
  HixlServerConfig config{};
  HixlServerDesc server_desc = {.server_ip = ip.c_str(),
                                .server_port = static_cast<uint32_t>(port),
                                .endpoint_list = &ep,
                                .endpoint_list_num = 1U};
  auto ret = HixlCSServerCreate(&server_desc, &config, &server_handle);
  if (ret != HIXL_SUCCESS) {
    (void)printf("[ERROR] HixlCSServerCreate failed, ret = %u\n", ret);
    return -1;
  }

  ret = HixlCSServerListen(server_handle, kBackLog);
  if (ret != HIXL_SUCCESS) {
    ServerFinalize(server_handle, {});
    (void)printf("[ERROR] HixlCSServerListen failed, ret = %u\n", ret);
    return -1;
  }
  (void)printf("[INFO] Server listen success, %s:%d\n", ip.c_str(), port);

  // 2. 注册内存地址
  MemHandle mem_handle = nullptr;
  HcommMem mem{};
  bool is_host = (args.transfer_mode == "d2h" || args.transfer_mode == "h2h");
  aclError acl_ret = ACL_ERROR_NONE;
  if (is_host) {
    void *tmp = malloc(kTransferMemSize);
    mem.addr = tmp;
    mem.type = HCCL_MEM_TYPE_HOST;
    mem.size = kTransferMemSize;
    if (tmp == nullptr) {
      HIXL_LOGE(hixl::RESOURCE_EXHAUSTED, "Server host addr malloc failed.");
      return hixl::RESOURCE_EXHAUSTED;
    }
  } else {
    acl_ret = aclrtMalloc(&mem.addr, kTransferMemSize, ACL_MEM_MALLOC_HUGE_ONLY);
    mem.type = HCCL_MEM_TYPE_DEVICE;
    mem.size = kTransferMemSize;
    if (acl_ret != ACL_ERROR_NONE) {
      (void)printf("[ERROR]Server host addr aclrtMalloc failed, ret = %d\n", acl_ret);
      ServerFinalize(server_handle, {mem_handle});
      return -1;
    }
    HIXL_LOGI("Server host addr malloc success. addr is %p, size is %u", mem.addr, mem.size);
  }

  ret = HixlCSServerRegMem(server_handle, kServerMemTagName, &mem, &mem_handle);
  if (ret != HIXL_SUCCESS) {
    (void)printf("[ERROR] HixlCSServerRegMem failed, ret = %u\n", ret);
    ServerFinalize(server_handle, {mem_handle});
    return -1;
  }
  (void)printf("[INFO] The server memory has been registered.\n");

  // 等待client传输
  TCPClient tcp_client;
  if (!tcp_client.ConnectToServer(args.remote_engine, args.tcp_port)) {
    return -1;
  }
  (void)printf("[INFO] Wait transfer begin\n");
  if (tcp_client.ReceiveTaskStatus()) {
    (void)printf("[INFO] Wait transfer end\n");
  }
  tcp_client.Disconnect();

  // 根据test_case执行不同的测试
  int32_t test_ret = 0;
  if (args.test_case == 5) {
    // 场景5: 收到client第一次传输完成通知后，解注册内存
    // 然后等待client再次尝试传输
    (void)printf("[INFO] Scene5: First transfer received, now unregistering memory...\n");
    ret = HixlCSServerUnregMem(server_handle, mem_handle);
    if (ret != HIXL_SUCCESS) {
      (void)printf("[SCENE5 ERROR] HixlCSServerUnregMem failed, ret = %u\n", ret);
    } else {
      (void)printf("[SCENE5] Server memory unregistered, waiting for client second transfer...\n");
    }
    // 等待一段时间让client尝试第二次传输
    std::this_thread::sleep_for(std::chrono::seconds(3));
    (void)printf("[INFO] Scene5 test completed.\n");
  } else if (args.test_case == 7) {
    // 场景7: server等待client销毁，检查资源回收
    test_ret = Scene7Test(nullptr, nullptr, server_handle, mem_handle, args.transfer_op, false);
  } else {
    (void)printf("[INFO] Server waiting for client test to complete.\n");
  }

  // 5. 解注册，释放内存，析构
  ServerFinalize(server_handle, {mem_handle});
  (void)printf("[INFO] Server test end, result: %d\n", test_ret);
  return test_ret;
}
}  // namespace

int32_t main(int32_t argc, char **argv) {
  bool is_client = false;
  Args args{};
  std::string device_id_str;
  std::string tcp_port_str;
  std::string test_case_str;

  if (argc == kExpectedArgCnt) {
    device_id_str = argv[kArgIndexDeviceId];
    args.local_engine = argv[kArgIndexLocalEngine];
    args.remote_engine = argv[kArgIndexRemoteEngine];
    tcp_port_str = argv[kArgIndexTcpPort];
    args.transfer_mode = argv[kArgIndexTransferMode];
    args.transfer_op = argv[kArgIndexTransferOp];
    args.local_comm_res = argv[kArgIndexLocalCommRes];
    args.remote_comm_res = argv[kArgIndexRemoteCommRes];
    test_case_str = argv[kArgIndexTestCase];
    args.test_case = std::stoi(test_case_str);
    is_client = (args.remote_engine.find(':') != std::string::npos);
    (void)printf(
        "[INFO] device_id = %s, local_engine = %s, remote_engine = %s, tcp_port = %s, transfer_mode = %s, "
        "transfer_op = %s, local_comm_res = %s, remote_comm_res = %s, test_case = %s\n",
        device_id_str.c_str(), args.local_engine.c_str(), args.remote_engine.c_str(), tcp_port_str.c_str(),
        args.transfer_mode.c_str(), args.transfer_op.c_str(), args.local_comm_res.c_str(),
        args.remote_comm_res.c_str(), test_case_str.c_str());
  } else {
    (void)printf(
        "[ERROR] Expect 9 args(device_id, local_engine, remote_engine, tcp_port, transfer_mode, "
        "transfer_op, local_comm_res, remote_comm_res, test_case)\n");
    (void)printf("test_case: 5=Scene5(unreg mem then transfer), 6=Scene6(5000 transfers), "
                 "7=Scene7(destroy client during transfer), 8=Scene8(kill client process)\n");
    return -1;
  }

  if (args.test_case < 5 || args.test_case > 8) {
    (void)printf("[ERROR] Invalid test_case: %d, should be 5-8\n", args.test_case);
    return -1;
  }

  args.device_id = std::stoi(device_id_str);
  int32_t input_tcp_port = std::stoi(tcp_port_str);
  if (input_tcp_port < 0 || input_tcp_port > kPortMaxValue) {
    (void)printf("[ERROR] Invalid port: %d, should be in 0~65535\n", input_tcp_port);
    return -1;
  }
  args.tcp_port = static_cast<uint16_t>(input_tcp_port);
  CHECK_ACL_RETURN(aclrtSetDevice(args.device_id));

  if (args.transfer_mode != "d2d" && args.transfer_mode != "h2d" && args.transfer_mode != "d2h" &&
      args.transfer_mode != "h2h") {
    (void)printf("[ERROR] Invalid value for transfer_mode: %s\n", args.transfer_mode.c_str());
    return -1;
  }

  if (args.transfer_op != "write" && args.transfer_op != "read") {
    (void)printf("[ERROR] Invalid value for transfer_op: %s\n", args.transfer_op.c_str());
    return -1;
  }

  int32_t ret = 0;
  if (is_client) {
    ret = RunClient(args);
  } else {
    ret = RunServer(args);
  }
  CHECK_ACL_RETURN(aclrtResetDevice(args.device_id));
  return ret;
}
