/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN " "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
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
#include "nlohmann/json.hpp"
#include "common/tcp_client_server.h"
#include "acl/acl.h"
#include "hixl/common/hixl_cs.h"
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
constexpr int32_t kClientConnectTimeoutMs = 60000;
constexpr int32_t kExpectedArgCnt = 10;
constexpr uint32_t kArgIndexDeviceId = 1;
constexpr uint32_t kArgIndexLocalEngine = 2;
constexpr uint32_t kArgIndexRemoteEngine = 3;
constexpr uint32_t kArgIndexTcpPort = 4;
constexpr uint32_t kArgIndexTransferMode = 5;
constexpr uint32_t kArgIndexTransferOp = 6;
constexpr uint32_t kArgIndexTestType = 7;
constexpr uint32_t kArgIndexLocalCommRes = 8;
constexpr uint32_t kArgIndexRemoteCommRes = 9;
constexpr int32_t kPortMaxValue = 65535;
constexpr int32_t kBackLog = 1024;
constexpr const char *kServerMemTagName = "server_mem";
constexpr const char *kClientMemTagName = "client_mem";

constexpr uint64_t k1GB = 1ULL * 1024 * 1024 * 1024;
constexpr uint64_t k2GB = 2ULL * 1024 * 1024 * 1024;
constexpr uint64_t k8GB = 8ULL * 1024 * 1024 * 1024;
constexpr uint64_t k32GB = 32ULL * 1024 * 1024 * 1024;
constexpr uint64_t k128MB = 128ULL * 1024 * 1024;

constexpr int32_t kTestTypeLargeData = 1;
constexpr int32_t kTestTypeMultiBlock = 2;

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
  int32_t test_type;
  std::string local_comm_res;
  std::string remote_comm_res;
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

void PrintThroughput(uint64_t size_bytes, int64_t time_us) {
  double time_second = static_cast<double>(time_us) / 1000 / 1000;
  double throughput_gb = static_cast<double>(size_bytes) / 1024 / 1024 / 1024 / time_second;
  double throughput_gbps = throughput_gb * 8;
  (void)printf("[INFO] Size: %.2f GB, Time: %.3f s, Throughput: %.3f GB/s (%.3f Gbps)\n",
               static_cast<double>(size_bytes) / 1024 / 1024 / 1024, time_second, throughput_gb, throughput_gbps);
}

int32_t TransferLargeData(HixlClientHandle client_handle, uint8_t *local_addr,
                           const std::vector<uint64_t> &test_sizes, const std::string &transfer_op,
                           uint64_t block_size) {
  HcommMem *remote_mem_list = nullptr;
  char **mem_tag_list = nullptr;
  uint32_t list_num = 0U;
  auto ret = HixlCSClientGetRemoteMem(client_handle, &remote_mem_list, &mem_tag_list, &list_num, kClientConnectTimeoutMs);
  if (ret != HIXL_SUCCESS) {
    (void)printf("[ERROR] HixlCSClientGetRemoteMem failed, ret = %u\n", ret);
    return -1;
  }
  std::map<std::string, HcommMem> server_mems;
  for (uint32_t i = 0; i < list_num; ++i) {
    server_mems[mem_tag_list[i]] = remote_mem_list[i];
  }
  uint8_t *remote_addr = static_cast<uint8_t *>(server_mems[kServerMemTagName].addr);

  for (uint64_t transfer_size : test_sizes) {
    uint32_t num_blocks = static_cast<uint32_t>((transfer_size + block_size - 1) / block_size);
    uint64_t actual_block_size = transfer_size / num_blocks;

    (void)printf("[INFO] TransferLargeData: size=%lu bytes, num_blocks=%u, block_size=%lu bytes\n",
                 transfer_size, num_blocks, actual_block_size);

    std::vector<HixlOneSideOpDesc> desc_list(num_blocks);
    for (uint32_t j = 0; j < num_blocks; j++) {
      desc_list[j].local_buf = local_addr + j * actual_block_size;
      desc_list[j].remote_buf = remote_addr + j * actual_block_size;
      desc_list[j].len = actual_block_size;
    }

    CompleteHandle *complete_handle = new CompleteHandle();
    const auto start = std::chrono::steady_clock::now();

    if (transfer_op == "write") {
      HIXL_LOGI("HixlCSClientBatchPutAsync start, num_blocks is:%u", num_blocks);
      ret = HixlCSClientBatchPutAsync(client_handle, num_blocks, desc_list.data(), complete_handle);
    } else {
      HIXL_LOGI("HixlCSClientBatchGetAsync start, num_blocks is:%u", num_blocks);
      ret = HixlCSClientBatchGetAsync(client_handle, num_blocks, desc_list.data(), complete_handle);
    }
    if (ret != HIXL_SUCCESS) {
      (void)printf("[ERROR] HixlCSClientBatchPutAsync/HixlCSClientBatchGetAsync failed, ret = %u\n", ret);
      delete complete_handle;
      return -1;
    }

    HixlCompleteStatus status = HIXL_COMPLETE_STATUS_WAITING;
    while (true) {
      ret = HixlCSClientQueryCompleteStatus(client_handle, complete_handle, &status);
      if (status == HIXL_COMPLETE_STATUS_COMPLETED) {
        break;
      } else if (status == HIXL_COMPLETE_STATUS_WAITING) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        continue;
      }
      if (ret != HIXL_SUCCESS) {
        (void)printf("[ERROR] HixlCSClientQueryCompleteStatus failed, ret = %u\n", ret);
        delete complete_handle;
        return -1;
      }
    }

    auto time_cost = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
    PrintThroughput(transfer_size, time_cost);
    delete complete_handle;
  }
  return 0;
}

int32_t TransferMultiBlock(HixlClientHandle client_handle, uint8_t *local_addr,
                            uint64_t total_size, const std::string &transfer_op,
                            const std::vector<uint32_t> &block_counts) {
  HcommMem *remote_mem_list = nullptr;
  char **mem_tag_list = nullptr;
  uint32_t list_num = 0U;
  auto ret = HixlCSClientGetRemoteMem(client_handle, &remote_mem_list, &mem_tag_list, &list_num, kClientConnectTimeoutMs);
  if (ret != HIXL_SUCCESS) {
    (void)printf("[ERROR] HixlCSClientGetRemoteMem failed, ret = %u\n", ret);
    return -1;
  }
  std::map<std::string, HcommMem> server_mems;
  for (uint32_t i = 0; i < list_num; ++i) {
    server_mems[mem_tag_list[i]] = remote_mem_list[i];
  }
  uint8_t *remote_addr = static_cast<uint8_t *>(server_mems[kServerMemTagName].addr);

  for (uint32_t num_blocks : block_counts) {
    uint64_t block_size = total_size / num_blocks;

    (void)printf("[INFO] MultiBlock Test: total_size=%lu bytes, num_blocks=%u, block_size=%lu bytes\n",
                 total_size, num_blocks, block_size);

    std::vector<HixlOneSideOpDesc> desc_list(num_blocks);
    for (uint32_t j = 0; j < num_blocks; j++) {
      desc_list[j].local_buf = local_addr + j * block_size;
      desc_list[j].remote_buf = remote_addr + j * block_size;
      desc_list[j].len = block_size;
    }

    CompleteHandle *complete_handle = new CompleteHandle();
    const auto start = std::chrono::steady_clock::now();

    if (transfer_op == "write") {
      ret = HixlCSClientBatchPutAsync(client_handle, num_blocks, desc_list.data(), complete_handle);
    } else {
      ret = HixlCSClientBatchGetAsync(client_handle, num_blocks, desc_list.data(), complete_handle);
    }
    if (ret != HIXL_SUCCESS) {
      (void)printf("[ERROR] Batch operation failed, ret = %u\n", ret);
      delete complete_handle;
      return -1;
    }

    HixlCompleteStatus status = HIXL_COMPLETE_STATUS_WAITING;
    while (true) {
      ret = HixlCSClientQueryCompleteStatus(client_handle, complete_handle, &status);
      if (status == HIXL_COMPLETE_STATUS_COMPLETED) {
        break;
      } else if (status == HIXL_COMPLETE_STATUS_WAITING) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        continue;
      }
      if (ret != HIXL_SUCCESS) {
        (void)printf("[ERROR] HixlCSClientQueryCompleteStatus failed, ret = %u\n", ret);
        delete complete_handle;
        return -1;
      }
    }

    auto time_cost = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
    PrintThroughput(total_size, time_cost);
    delete complete_handle;
  }
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

int32_t RunClientLargeData(const Args &args) {
  (void)printf("[INFO] Large Data Test Client Start\n");

  TCPServer tcp_server;
  if (!tcp_server.StartServer(args.tcp_port)) {
    (void)printf("[ERROR] Failed to start TCP server.\n");
    return -1;
  }
  (void)printf("[INFO] TCP server started.\n");
  if (!tcp_server.AcceptConnection()) {
    return -1;
  }

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
                                .src_endpoint = &local_ep,
                                .dst_endpoint = &remote_ep};
  HixlClientConfig client_config{};
  auto ret = HixlCSClientCreate(&client_desc, &client_config, &client_handle);
  if (ret != HIXL_SUCCESS) {
    (void)printf("[ERROR] HixlCSClientCreate failed, ret = %u\n", ret);
    return -1;
  }

  std::vector<uint64_t> test_sizes = {k2GB, k8GB, k32GB};
  uint64_t max_size = k32GB;
  uint64_t block_size = k128MB;

  MemHandle mem_handle = nullptr;
  HcommMem mem{};
  bool is_host = (args.transfer_mode == "h2d" || args.transfer_mode == "h2h");

  if (is_host) {
    void *tmp = malloc(max_size);
    mem.addr = tmp;
    mem.type = HCCL_MEM_TYPE_HOST;
    mem.size = max_size;
    if (tmp == nullptr) {
      HIXL_LOGE(hixl::RESOURCE_EXHAUSTED, "Client host addr malloc failed.");
      return hixl::RESOURCE_EXHAUSTED;
    }
  } else {
    aclError acl_ret = aclrtMalloc(&mem.addr, max_size, ACL_MEM_MALLOC_HUGE_ONLY);
    mem.type = HCCL_MEM_TYPE_DEVICE;
    mem.size = max_size;
    if (acl_ret != ACL_ERROR_NONE) {
      (void)printf("[ERROR] aclrtMalloc failed, ret = %d\n", acl_ret);
      ClientFinalize(client_handle, {mem_handle});
      return -1;
    }
  }

  if (args.transfer_op == "write") {
    memset(mem.addr, 1, max_size);
  }

  ret = HixlCSClientRegMem(client_handle, kClientMemTagName, &mem, &mem_handle);
  if (ret != HIXL_SUCCESS) {
    (void)printf("[ERROR] HixlCSClientRegMem failed, ret = %u\n", ret);
    ClientFinalize(client_handle, {mem_handle});
    return -1;
  }
  (void)printf("[INFO] Client memory registered, size: %lu bytes\n", max_size);

  ret = HixlCSClientConnect(client_handle, kClientConnectTimeoutMs);
  if (ret != HIXL_SUCCESS) {
    ClientFinalize(client_handle, {});
    (void)printf("[ERROR] HixlCSClientConnect failed, ret = %u\n", ret);
    return -1;
  }

  if (TransferLargeData(client_handle, static_cast<uint8_t *>(mem.addr), test_sizes,
                        args.transfer_op, block_size) != 0) {
    ClientFinalize(client_handle, {mem_handle});
    return -1;
  }

  ClientFinalize(client_handle, {mem_handle});

  (void)tcp_server.SendTaskStatus();
  tcp_server.DisConnectClient();
  tcp_server.StopServer();
  (void)printf("[INFO] Large Data Test Client End\n");
  return 0;
}

int32_t RunClientMultiBlock(const Args &args) {
  (void)printf("[INFO] MultiBlock Test Client Start\n");

  TCPServer tcp_server;
  if (!tcp_server.StartServer(args.tcp_port)) {
    (void)printf("[ERROR] Failed to start TCP server.\n");
    return -1;
  }
  (void)printf("[INFO] TCP server started.\n");
  if (!tcp_server.AcceptConnection()) {
    return -1;
  }

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
                                .src_endpoint = &local_ep,
                                .dst_endpoint = &remote_ep};
  HixlClientConfig client_config{};
  auto ret = HixlCSClientCreate(&client_desc, &client_config, &client_handle);
  if (ret != HIXL_SUCCESS) {
    (void)printf("[ERROR] HixlCSClientCreate failed, ret = %u\n", ret);
    return -1;
  }

  uint64_t transfer_size = k2GB;
  std::vector<uint32_t> block_counts = {40, 200, 1000};

  MemHandle mem_handle = nullptr;
  HcommMem mem{};
  bool is_host = (args.transfer_mode == "h2d" || args.transfer_mode == "h2h");

  if (is_host) {
    void *tmp = malloc(transfer_size);
    mem.addr = tmp;
    mem.type = HCCL_MEM_TYPE_HOST;
    mem.size = transfer_size;
    if (tmp == nullptr) {
      HIXL_LOGE(hixl::RESOURCE_EXHAUSTED, "Client host addr malloc failed.");
      return hixl::RESOURCE_EXHAUSTED;
    }
  } else {
    aclError acl_ret = aclrtMalloc(&mem.addr, transfer_size, ACL_MEM_MALLOC_HUGE_ONLY);
    mem.type = HCCL_MEM_TYPE_DEVICE;
    mem.size = transfer_size;
    if (acl_ret != ACL_ERROR_NONE) {
      (void)printf("[ERROR] aclrtMalloc failed, ret = %d\n", acl_ret);
      ClientFinalize(client_handle, {mem_handle});
      return -1;
    }
  }

  if (args.transfer_op == "write") {
    memset(mem.addr, 1, transfer_size);
  }

  ret = HixlCSClientRegMem(client_handle, kClientMemTagName, &mem, &mem_handle);
  if (ret != HIXL_SUCCESS) {
    (void)printf("[ERROR] HixlCSClientRegMem failed, ret = %u\n", ret);
    ClientFinalize(client_handle, {mem_handle});
    return -1;
  }
  (void)printf("[INFO] Client memory registered, size: %lu bytes\n", transfer_size);

  ret = HixlCSClientConnect(client_handle, kClientConnectTimeoutMs);
  if (ret != HIXL_SUCCESS) {
    ClientFinalize(client_handle, {});
    (void)printf("[ERROR] HixlCSClientConnect failed, ret = %u\n", ret);
    return -1;
  }

  if (TransferMultiBlock(client_handle, static_cast<uint8_t *>(mem.addr), transfer_size,
                         args.transfer_op, block_counts) != 0) {
    ClientFinalize(client_handle, {mem_handle});
    return -1;
  }

  ClientFinalize(client_handle, {mem_handle});

  (void)tcp_server.SendTaskStatus();
  tcp_server.DisConnectClient();
  tcp_server.StopServer();
  (void)printf("[INFO] MultiBlock Test Client End\n");
  return 0;
}

int32_t RunServerLargeData(const Args &args) {
  (void)printf("[INFO] Large Data Test Server Start\n");

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

  uint64_t max_size = k32GB;
  MemHandle mem_handle = nullptr;
  HcommMem mem{};
  bool is_host = (args.transfer_mode == "d2h" || args.transfer_mode == "h2h");

  if (is_host) {
    void *tmp = malloc(max_size);
    mem.addr = tmp;
    mem.type = HCCL_MEM_TYPE_HOST;
    mem.size = max_size;
    if (tmp == nullptr) {
      HIXL_LOGE(hixl::RESOURCE_EXHAUSTED, "Server host addr malloc failed.");
      return hixl::RESOURCE_EXHAUSTED;
    }
  } else {
    aclError acl_ret = aclrtMalloc(&mem.addr, max_size, ACL_MEM_MALLOC_HUGE_ONLY);
    mem.type = HCCL_MEM_TYPE_DEVICE;
    mem.size = max_size;
    if (acl_ret != ACL_ERROR_NONE) {
      (void)printf("[ERROR] Server aclrtMalloc failed, ret = %d\n", acl_ret);
      ServerFinalize(server_handle, {mem_handle});
      return -1;
    }
  }

  ret = HixlCSServerRegMem(server_handle, kServerMemTagName, &mem, &mem_handle);
  if (ret != HIXL_SUCCESS) {
    (void)printf("[ERROR] HixlCSServerRegMem failed, ret = %u\n", ret);
    ServerFinalize(server_handle, {mem_handle});
    return -1;
  }
  (void)printf("[INFO] Server memory registered, size: %lu bytes\n", max_size);

  TCPClient tcp_client;
  if (!tcp_client.ConnectToServer(args.remote_engine, args.tcp_port)) {
    return -1;
  }
  (void)printf("[INFO] Wait transfer begin\n");
  if (tcp_client.ReceiveTaskStatus()) {
    (void)printf("[INFO] Wait transfer end\n");
  }
  tcp_client.Disconnect();

  ServerFinalize(server_handle, {mem_handle});
  (void)printf("[INFO] Large Data Test Server End\n");
  return 0;
}

int32_t RunServerMultiBlock(const Args &args) {
  (void)printf("[INFO] MultiBlock Test Server Start\n");

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

  uint64_t transfer_size = k2GB;
  MemHandle mem_handle = nullptr;
  HcommMem mem{};
  bool is_host = (args.transfer_mode == "d2h" || args.transfer_mode == "h2h");

  if (is_host) {
    void *tmp = malloc(transfer_size);
    mem.addr = tmp;
    mem.type = HCCL_MEM_TYPE_HOST;
    mem.size = transfer_size;
    if (tmp == nullptr) {
      HIXL_LOGE(hixl::RESOURCE_EXHAUSTED, "Server host addr malloc failed.");
      return hixl::RESOURCE_EXHAUSTED;
    }
  } else {
    aclError acl_ret = aclrtMalloc(&mem.addr, transfer_size, ACL_MEM_MALLOC_HUGE_ONLY);
    mem.type = HCCL_MEM_TYPE_DEVICE;
    mem.size = transfer_size;
    if (acl_ret != ACL_ERROR_NONE) {
      (void)printf("[ERROR] Server aclrtMalloc failed, ret = %d\n", acl_ret);
      ServerFinalize(server_handle, {mem_handle});
      return -1;
    }
  }

  ret = HixlCSServerRegMem(server_handle, kServerMemTagName, &mem, &mem_handle);
  if (ret != HIXL_SUCCESS) {
    (void)printf("[ERROR] HixlCSServerRegMem failed, ret = %u\n", ret);
    ServerFinalize(server_handle, {mem_handle});
    return -1;
  }
  (void)printf("[INFO] Server memory registered, size: %lu bytes\n", transfer_size);

  TCPClient tcp_client;
  if (!tcp_client.ConnectToServer(args.remote_engine, args.tcp_port)) {
    return -1;
  }
  (void)printf("[INFO] Wait transfer begin\n");
  if (tcp_client.ReceiveTaskStatus()) {
    (void)printf("[INFO] Wait transfer end\n");
  }
  tcp_client.Disconnect();

  ServerFinalize(server_handle, {mem_handle});
  (void)printf("[INFO] MultiBlock Test Server End\n");
  return 0;
}

}  // namespace

void PrintUsage(const char *prog_name) {
  (void)printf("Usage: %s <device_id> <local_engine> <remote_engine> <tcp_port> <transfer_mode> "
                "<transfer_op> <test_type> <local_comm_res> <remote_comm_res>\n", prog_name);
  (void)printf("  test_type: 1=LargeData(2G/8G/32G), 2=MultiBlock(40/200/1000 blocks)\n");
  (void)printf("  Example (LargeData): %s 0 127.0.0.1:19999 127.0.0.1:19998 19997 h2h write 1 h2h h2h\n", prog_name);
  (void)printf("  Example (MultiBlock): %s 0 127.0.0.1:19999 127.0.0.1:19998 19997 h2h write 2 h2h h2h\n", prog_name);
}

int32_t main(int32_t argc, char **argv) {
  bool is_client = false;
  Args args{};
  std::string device_id_str;
  std::string tcp_port_str;

  if (argc == kExpectedArgCnt) {
    device_id_str = argv[kArgIndexDeviceId];
    args.local_engine = argv[kArgIndexLocalEngine];
    args.remote_engine = argv[kArgIndexRemoteEngine];
    tcp_port_str = argv[kArgIndexTcpPort];
    args.transfer_mode = argv[kArgIndexTransferMode];
    args.transfer_op = argv[kArgIndexTransferOp];
    args.test_type = std::stoi(argv[kArgIndexTestType]);
    args.local_comm_res = argv[kArgIndexLocalCommRes];
    args.remote_comm_res = argv[kArgIndexRemoteCommRes];
    is_client = (args.remote_engine.find(':') != std::string::npos);
    (void)printf(
        "[INFO] device_id = %s, local_engine = %s, remote_engine = %s, tcp_port = %s, transfer_mode = %s, "
        "transfer_op = %s, test_type = %d, local_comm_res = %s, remote_comm_res = %s\n",
        device_id_str.c_str(), args.local_engine.c_str(), args.remote_engine.c_str(), tcp_port_str.c_str(),
        args.transfer_mode.c_str(), args.transfer_op.c_str(), args.test_type,
        args.local_comm_res.c_str(), args.remote_comm_res.c_str());
  } else {
    (void)printf(
        "[ERROR] Expect 9 args, but got %d\n", argc - 1);
    PrintUsage(argv[0]);
    return -1;
  }

  args.device_id = std::stoi(device_id_str);
  int32_t input_tcp_port = std::stoi(tcp_port_str);
  if (input_tcp_port < 0 || input_tcp_port > kPortMaxValue) {
    (void)printf("[ERROR] Invalid port: %d, should be in 0~65535\n", input_tcp_port);
    return -1;
  }
  args.tcp_port = static_cast<uint16_t>(input_tcp_port);

  if (args.test_type != kTestTypeLargeData && args.test_type != kTestTypeMultiBlock) {
    (void)printf("[ERROR] Invalid test_type: %d (use 1=LargeData, 2=MultiBlock)\n", args.test_type);
    return -1;
  }

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
  if (args.test_type == kTestTypeLargeData) {
    if (is_client) {
      ret = RunClientLargeData(args);
    } else {
      ret = RunServerLargeData(args);
    }
  } else if (args.test_type == kTestTypeMultiBlock) {
    if (is_client) {
      ret = RunClientMultiBlock(args);
    } else {
      ret = RunServerMultiBlock(args);
    }
  }

  CHECK_ACL_RETURN(aclrtResetDevice(args.device_id));
  return ret;
}