/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
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
constexpr uint32_t kArgIndexNumPairs = 7;
constexpr uint32_t kArgIndexLocalCommRes = 8;
constexpr uint32_t kArgIndexRemoteCommRes = 9;
constexpr uint32_t kTransferMemSize = 134217728;  // 128M
constexpr uint32_t kBaseBlockSize = 262144;       // 0.25M
constexpr uint32_t kExecuteRepeatNum = 5;
constexpr int32_t kPortMaxValue = 65535;
constexpr int32_t kBackLog = 1024;
constexpr const char *kServerMemTagName = "server_mem";
constexpr const char *kClientMemTagName = "client_mem";

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
  int32_t num_pairs;
  std::string local_comm_res;
  std::string remote_comm_res;
};

struct ClientContext {
  HixlClientHandle client_handle;
  MemHandle mem_handle;
  HcommMem mem;
  uint8_t *local_addr;
  uint8_t *remote_addr;
  TCPServer tcp_server;
  uint32_t *transfer_data;
};

struct ServerContext {
  HixlServerHandle server_handle;
  MemHandle mem_handle;
  HcommMem mem;
  uint8_t *local_addr;
  TCPClient tcp_client;
  uint32_t *transfer_data;
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

int32_t Transfer(HixlClientHandle client_handle, uint8_t *local_addr,
                 const std::string &transfer_op) {
  HcommMem *remote_mem_list = nullptr;
  char **mem_tag_list = nullptr;
  uint32_t list_num = 0U;
  auto ret =
      HixlCSClientGetRemoteMem(client_handle, &remote_mem_list, &mem_tag_list, &list_num, kClientConnectTimeoutMs);
  if (ret != HIXL_SUCCESS) {
    (void)printf("[ERROR] HixlCSClientGetRemoteMem failed, ret = %u\n", ret);
    return -1;
  }
  std::map<std::string, HcommMem> server_mems;
  for (uint32_t i = 0; i < list_num; ++i) {
    server_mems[mem_tag_list[i]] = remote_mem_list[i];
  }
  uint8_t *server_addr = static_cast<uint8_t *>(server_mems[kServerMemTagName].addr);

  for (uint32_t i = 0; i <= kExecuteRepeatNum; i++) {
    auto block_size = kBaseBlockSize * (1U << i);
    auto trans_num = kTransferMemSize / block_size;
    std::vector<HixlOneSideOpDesc> desc_list(trans_num);
    std::vector<uint64_t> lens;
    for (uint32_t j = 0; j < trans_num; j++) {
      desc_list[j].local_buf = local_addr + j * block_size;
      desc_list[j].remote_buf = server_addr + j * block_size;
      desc_list[j].len = block_size;
    }
    void *complete_handle = nullptr;
    const auto start = std::chrono::steady_clock::now();
    if (transfer_op == "write") {
      HIXL_LOGI("HixlCSClientBatchPutAsync start, trans_num is:%u, remote_addrs is:%p, local_addrs is:%p, lens is:%u.",
                 trans_num, desc_list[0].remote_buf, desc_list[0].local_buf, desc_list[0].len);
      ret = HixlCSClientBatchPutAsync(client_handle, trans_num, desc_list.data(), &complete_handle);
    } else {
      HIXL_LOGI("HixlCSClientBatchGetAsync start, trans_num is:%u, local_addrs is:%p, remote_addrs is:%p, lens is:%u.",
                 trans_num, desc_list[0].local_buf, desc_list[0].remote_buf, desc_list[0].len);
      ret = HixlCSClientBatchGetAsync(client_handle, trans_num, desc_list.data(), &complete_handle);
    }
    if (ret != HIXL_SUCCESS) {
      (void)printf("[ERROR] HixlCSClientBatchPutAsync/HixlCSClientBatchGetAsync failed, ret = %u\n", ret);
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
        (void)printf("[ERROR] HixlCSClientQueryCompleteStatus failed, ret = %u\n", ret);
        return -1;
      }
    }
    auto time_cost =
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
    double time_second = static_cast<double>(time_cost) / 1000 / 1000;
    double throughput = static_cast<double>(kTransferMemSize) / 1024 / 1024 / 1024 / time_second;
    (void)printf(
        "[INFO] Transfer success, block size: %u Bytes, transfer num: %u, time cost: %ld us, throughput: %.3lf GB/s\n",
        block_size, trans_num, time_cost, throughput);
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

void ServerFinalize(HixlServerHandle server_handle, const std::vector<MemHandle> &handles) {
  for (auto handle : handles) {
    if (handle != nullptr) {
      HixlCSServerUnregMem(server_handle, handle);
    }
  }

  if (server_handle != nullptr) {
    HixlCSServerDestroy(server_handle);
  }
}

uint32_t *mem_alloc(const std::string &transfer_op, bool is_client, aclrtMemcpyKind copy_kind, HcommMem mem) {
  void *tmp = nullptr;
  std::string device;
  if (is_client) {
    device = "client";
  } else {
    device = "server";
  }
  auto ret = aclrtMallocHost(&tmp, kTransferMemSize);
  if (ret != ACL_ERROR_NONE) {
    (void)printf("[ERROR] %s transfer_data aclrtMalloc failed, ret = %d\n", device.c_str(), ret);
    ret = aclrtFreeHost(tmp);
  }
  uint32_t *transfer_data = static_cast<uint32_t *>(tmp);
  HIXL_LOGI("The %s transfer_data addr is : %p", device.c_str(), transfer_data);
  if ((transfer_op == "write" and is_client) || (transfer_op == "read" and not is_client)) {
    for (uint32_t i = 0; i < kTransferMemSize / sizeof(uint32_t); i++) {
      transfer_data[i] = 1;
    }
    ret = aclrtMemcpy(mem.addr, kTransferMemSize, transfer_data, kTransferMemSize, copy_kind);
    if (ret != ACL_ERROR_NONE) {
      (void)printf("[ERROR] %s transfer_data aclrtMemcpy failed, ret = %d\n", device.c_str(), ret);
    }
    HIXL_LOGI("The %s transfer_data have been copy to client_mem.", device.c_str());
  }
  if ((transfer_op == "read" and is_client) || (transfer_op == "write" and not is_client)) {
    for (uint32_t i = 0; i < kTransferMemSize / sizeof(uint32_t); i++) {
      transfer_data[i] = 0;
    }
    ret = aclrtMemcpy(transfer_data, kTransferMemSize, mem.addr, kTransferMemSize, copy_kind);
    if (ret != ACL_ERROR_NONE) {
      (void)printf("[ERROR] %s transfer_data aclrtMemcpy failed, ret = %d\n", device.c_str(), ret);
    }
    HIXL_LOGI("The client transfer_data have been copy to client_mem.");

    uint32_t error_num = 0;
    HIXL_LOGI("The num of this data transfer task is %u", kTransferMemSize / sizeof(uint32_t));
    for (uint32_t i = 0; i < kTransferMemSize / sizeof(uint32_t); i++) {
      if (transfer_data[i] != 1) {
        error_num++;
      }
    }
    HIXL_LOGI("The error count for this data transfer task is %u", error_num);
  }
  return transfer_data;
}

int32_t RunSingleClient(const Args &args, int32_t pair_index, uint16_t base_port) {
  (void)printf("[INFO] Client pair %d start\n", pair_index);

  uint16_t tcp_port = base_port + pair_index;

  // TCP server for receiving completion signal from server
  TCPServer tcp_server;
  if (!tcp_server.StartServer(tcp_port)) {
    (void)printf("[ERROR] Pair %d: Failed to start TCP server on port %d.\n", pair_index, tcp_port);
    return -1;
  }
  (void)printf("[INFO] Pair %d: TCP server started on port %d.\n", pair_index, tcp_port);
  if (!tcp_server.AcceptConnection()) {
    (void)printf("[ERROR] Pair %d: Failed to accept TCP connection.\n", pair_index);
    return -1;
  }

  // Calculate engine port for this pair
  std::string local_ip = args.local_engine.substr(0U, args.local_engine.find(':'));
  std::string remote_ip = args.remote_engine.substr(0U, args.remote_engine.find(':'));
  int32_t remote_port = std::stoi(args.remote_engine.substr(args.remote_engine.find(':') + 1U)) + pair_index;

  // 1. Initialize endpoint info
  EndpointDesc local_ep;
  EndpointDesc remote_ep;
  if (InitEndPointInfo(args.local_comm_res, local_ep) != 0 ||
      InitEndPointInfo(args.remote_comm_res, remote_ep) != 0) {
    (void)printf("[ERROR] Pair %d: Initialize EndPoint list failed\n", pair_index);
    return -1;
  }

  HixlClientHandle client_handle = nullptr;
  HixlClientDesc client_desc = {.server_ip = remote_ip.c_str(),
                                 .server_port = static_cast<uint32_t>(remote_port),
                                 .local_endpoint = &local_ep,
                                 .remote_endpoint = &remote_ep};
  HixlClientConfig client_config{};
  auto ret = HixlCSClientCreate(&client_desc, &client_config, &client_handle);
  if (ret != HIXL_SUCCESS) {
    (void)printf("[ERROR] Pair %d: HixlCSClientCreate failed, ret = %u\n", pair_index, ret);
    return -1;
  }

  // 2. Register memory
  uint32_t *kClientTransferData = nullptr;
  MemHandle mem_handle = nullptr;
  HcommMem mem{};
  aclrtMemcpyKind copy_kind;
  aclError acl_ret = ACL_ERROR_NONE;
  bool is_host = (args.transfer_mode == "h2d" || args.transfer_mode == "h2h");
  if (is_host) {
    void *tmp = nullptr;
    tmp = malloc(kTransferMemSize);
    mem.addr = tmp;
    copy_kind = ACL_MEMCPY_HOST_TO_HOST;
    mem.type = HCCL_MEM_TYPE_HOST;
    mem.size = kTransferMemSize;
    if (tmp == nullptr) {
      HIXL_LOGE(hixl::RESOURCE_EXHAUSTED, "Client host addr malloc failed.");
      ClientFinalize(client_handle, {mem_handle});
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
      (void)printf("[ERROR] Pair %d: aclrtMalloc failed, ret = %d\n", pair_index, acl_ret);
      ClientFinalize(client_handle, {mem_handle});
      return -1;
    }
  }
  ret = HixlCSClientRegMem(client_handle, kClientMemTagName, &mem, &mem_handle);
  if (ret != HIXL_SUCCESS) {
    (void)printf("[ERROR] Pair %d: HixlCSClientRegMem failed, ret = %u\n", pair_index, ret);
    ClientFinalize(client_handle, {mem_handle});
    return -1;
  }
  HIXL_LOGI("Pair %d: The client memory has been registered.", pair_index);
  (void)printf("[INFO] Pair %d: The client memory has been registered.\n", pair_index);

  // If write operation, copy initialized data to memory before transfer
  if (args.transfer_op == "write") {
    kClientTransferData = mem_alloc(args.transfer_op, true, copy_kind, mem);
  }

  // 3. Connect to server
  ret = HixlCSClientConnect(client_handle, kClientConnectTimeoutMs);
  if (ret != HIXL_SUCCESS) {
    ClientFinalize(client_handle, {});
    (void)printf("[ERROR] Pair %d: HixlCSClientConnect failed, ret = %u\n", pair_index, ret);
    return -1;
  }
  (void)printf("[INFO] Pair %d: Client connected to server %s:%d\n", pair_index, remote_ip.c_str(), remote_port);

  // 4. Transfer data
  if (Transfer(client_handle, static_cast<uint8_t *>(mem.addr), args.transfer_op) != 0) {
    ClientFinalize(client_handle, {mem_handle});
    return -1;
  }

  // 5. If read operation, copy data after transfer
  if (args.transfer_op == "read") {
    kClientTransferData = mem_alloc(args.transfer_op, true, copy_kind, mem);
  }

  // 6. Cleanup
  ClientFinalize(client_handle, {mem_handle});
  if (kClientTransferData != nullptr) {
    auto free_ret = aclrtFreeHost(kClientTransferData);
    if (free_ret != ACL_ERROR_NONE) {
      HIXL_LOGI("Pair %d: kClientTransferData rtFreeHost failed, ret=%d", pair_index, free_ret);
    }
  }

  // Notify server via TCP that transfer is complete
  (void)tcp_server.SendTaskStatus();
  tcp_server.DisConnectClient();
  tcp_server.StopServer();
  (void)printf("[INFO] Pair %d: Client end\n", pair_index);
  return 0;
}

int32_t RunClient(const Args &args) {
  (void)printf("[INFO] Multi-pair client start, num_pairs = %d\n", args.num_pairs);

  // Parse base port
  uint16_t base_port = args.tcp_port;

  // Run multiple clients in parallel using threads
  std::vector<std::thread> client_threads;
  std::vector<int32_t> results(args.num_pairs, 0);

  for (int32_t i = 0; i < args.num_pairs; i++) {
    client_threads.emplace_back([&args, &results, i, base_port]() {
      results[i] = RunSingleClient(args, i, base_port);
    });
  }

  // Wait for all clients to complete
  for (int32_t i = 0; i < args.num_pairs; i++) {
    client_threads[i].join();
    (void)printf("[INFO] Client pair %d finished with result: %d\n", i, results[i]);
  }

  // Check if any client failed
  for (int32_t i = 0; i < args.num_pairs; i++) {
    if (results[i] != 0) {
      (void)printf("[ERROR] Client pair %d failed\n", i);
      return -1;
    }
  }

  (void)printf("[INFO] All client pairs completed successfully\n");
  return 0;
}

int32_t RunSingleServer(const Args &args, int32_t pair_index, uint16_t base_port) {
  (void)printf("[INFO] Server pair %d start\n", pair_index);

  // Calculate ports for this pair
  uint16_t tcp_port = base_port + pair_index;

  // 1. Initialize endpoint info
  EndpointDesc ep;
  if (InitEndPointInfo(args.local_comm_res, ep) != 0) {
    (void)printf("[ERROR] Pair %d: Initialize EndPoint list failed\n", pair_index);
    return -1;
  }

  // Calculate engine port for this pair
  std::string local_ip = args.local_engine.substr(0U, args.local_engine.find(':'));
  int32_t local_port = std::stoi(args.local_engine.substr(args.local_engine.find(':') + 1U)) + pair_index;
  std::string remote_ip = args.remote_engine.substr(0U, args.remote_engine.find(':'));
  int32_t remote_port = std::stoi(args.remote_engine.substr(args.remote_engine.find(':') + 1U)) + pair_index;

  HixlServerHandle server_handle = nullptr;
  HixlServerConfig config{};
  HixlServerDesc server_desc = {.server_ip = local_ip.c_str(),
                                .server_port = static_cast<uint32_t>(local_port),
                                .endpoint_list = &ep,
                                .endpoint_list_num = 1U};
  auto ret = HixlCSServerCreate(&server_desc, &config, &server_handle);
  if (ret != HIXL_SUCCESS) {
    (void)printf("[ERROR] Pair %d: HixlCSServerCreate failed, ret = %u\n", pair_index, ret);
    return -1;
  }

  ret = HixlCSServerListen(server_handle, kBackLog);
  if (ret != HIXL_SUCCESS) {
    ServerFinalize(server_handle, {});
    (void)printf("[ERROR] Pair %d: HixlCSServerListen failed, ret = %u\n", pair_index, ret);
    return -1;
  }
  (void)printf("[INFO] Pair %d: Server listening on %s:%d\n", pair_index, local_ip.c_str(), local_port);

  // 2. Register memory
  uint32_t *kServerTransferData = nullptr;
  MemHandle mem_handle = nullptr;
  aclrtMemcpyKind copy_kind;
  HcommMem mem{};
  bool is_host = (args.transfer_mode == "d2h" || args.transfer_mode == "h2h");
  aclError acl_ret = ACL_ERROR_NONE;
  if (is_host) {
    void *tmp = nullptr;
    tmp = malloc(kTransferMemSize);
    mem.addr = tmp;
    copy_kind = ACL_MEMCPY_HOST_TO_HOST;
    mem.type = HCCL_MEM_TYPE_HOST;
    mem.size = kTransferMemSize;
    if (tmp == nullptr) {
      HIXL_LOGE(hixl::RESOURCE_EXHAUSTED, "Server host addr malloc failed.");
      ServerFinalize(server_handle, {mem_handle});
      return hixl::RESOURCE_EXHAUSTED;
    }
  } else {
    acl_ret = aclrtMalloc(&mem.addr, kTransferMemSize, ACL_MEM_MALLOC_HUGE_ONLY);
    if (args.transfer_op == "read") {
      copy_kind = ACL_MEMCPY_HOST_TO_DEVICE;
    } else {
      copy_kind = ACL_MEMCPY_DEVICE_TO_HOST;
    }
    mem.type = HCCL_MEM_TYPE_DEVICE;
    mem.size = kTransferMemSize;
    if (acl_ret != ACL_ERROR_NONE) {
      (void)printf("[ERROR] Pair %d: Server host addr aclrtMalloc failed, ret = %d\n", pair_index, acl_ret);
      ServerFinalize(server_handle, {mem_handle});
      return -1;
    }
    HIXL_LOGI("Pair %d: Server host addr malloc success. addr is %p, size is %u", pair_index, mem.addr, mem.size);
  }

  ret = HixlCSServerRegMem(server_handle, kServerMemTagName, &mem, &mem_handle);
  if (ret != HIXL_SUCCESS) {
    (void)printf("[ERROR] Pair %d: HixlCSServerRegMem failed, ret = %u\n", pair_index, ret);
    ServerFinalize(server_handle, {mem_handle});
    return -1;
  }
  HIXL_LOGI("Pair %d: The server memory has been registered.", pair_index);
  (void)printf("[INFO] Pair %d: The server memory has been registered.\n", pair_index);

  // 3. Allocate host memory and initialize if needed
  if (args.transfer_op == "read") {
    kServerTransferData = mem_alloc(args.transfer_op, false, copy_kind, mem);
  }

  // 4. Wait for client transfer to complete
  TCPClient tcp_client;
  std::string remote_engine_with_port = remote_ip + ":" + std::to_string(remote_port);
  if (!tcp_client.ConnectToServer(remote_ip, tcp_port)) {
    (void)printf("[ERROR] Pair %d: Failed to connect to client via TCP at %s:%d\n", pair_index, remote_ip.c_str(), tcp_port);
    ServerFinalize(server_handle, {mem_handle});
    return -1;
  }
  (void)printf("[INFO] Pair %d: Wait transfer begin\n", pair_index);
  if (tcp_client.ReceiveTaskStatus()) {
    (void)printf("[INFO] Pair %d: Wait transfer end\n", pair_index);
  }
  tcp_client.Disconnect();

  // 5. If write operation, copy data after transfer
  if (args.transfer_op == "write") {
    kServerTransferData = mem_alloc(args.transfer_op, false, copy_kind, mem);
  }

  // 6. Cleanup
  ServerFinalize(server_handle, {mem_handle});
  if (kServerTransferData != nullptr) {
    auto free_ret = aclrtFreeHost(kServerTransferData);
    if (free_ret != ACL_ERROR_NONE) {
      HIXL_LOGI("Pair %d: kServerTransferData rtFreeHost failed, ret=%d", pair_index, free_ret);
    }
  }
  (void)printf("[INFO] Pair %d: Server Sample end\n", pair_index);
  return 0;
}

int32_t RunServer(const Args &args) {
  (void)printf("[INFO] Multi-pair server start, num_pairs = %d\n", args.num_pairs);

  uint16_t base_port = args.tcp_port;

  // Run multiple servers in parallel using threads
  std::vector<std::thread> server_threads;
  std::vector<int32_t> results(args.num_pairs, 0);

  for (int32_t i = 0; i < args.num_pairs; i++) {
    server_threads.emplace_back([&args, &results, i, base_port]() {
      results[i] = RunSingleServer(args, i, base_port);
    });
  }

  // Wait for all servers to complete
  for (int32_t i = 0; i < args.num_pairs; i++) {
    server_threads[i].join();
    (void)printf("[INFO] Server pair %d finished with result: %d\n", i, results[i]);
  }

  // Check if any server failed
  for (int32_t i = 0; i < args.num_pairs; i++) {
    if (results[i] != 0) {
      (void)printf("[ERROR] Server pair %d failed\n", i);
      return -1;
    }
  }

  (void)printf("[INFO] All server pairs completed successfully\n");
  return 0;
}

}  // namespace

void PrintUsage(const char *prog_name) {
  (void)printf("Usage: %s <device_id> <local_engine> <remote_engine> <tcp_port> <transfer_mode> "
               "<transfer_op> <num_pairs> <local_comm_res> <remote_comm_res>\n",
               prog_name);
  (void)printf("  device_id: Device ID (e.g., 0)\n");
  (void)printf("  local_engine: Server engine address (e.g., 127.0.0.1:19999)\n");
  (void)printf("  remote_engine: Client connects to server address (e.g., 127.0.0.1:19998)\n");
  (void)printf("  tcp_port: Base TCP port for control channel (each pair uses tcp_port + pair_index)\n");
  (void)printf("  transfer_mode: d2d, d2h, h2h, h2d\n");
  (void)printf("  transfer_op: write or read\n");
  (void)printf("  num_pairs: Number of client-server pairs\n");
  (void)printf("  local_comm_res: Local endpoint JSON (e.g., '{\"location\":\"host\",\"protocol\":\"roce\",\"addr\":\"127.0.0.1\"}')\n");
  (void)printf("  remote_comm_res: Remote endpoint JSON\n");
  (void)printf("Example (server): %s 0 127.0.0.1:19999 127.0.0.1:19998 19997 h2h write 2 h2h h2h\n", prog_name);
  (void)printf("Example (client): %s 0 127.0.0.1:19999 127.0.0.1:19998 19997 h2h write 2 h2h h2h\n", prog_name);
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
    args.num_pairs = std::stoi(argv[kArgIndexNumPairs]);
    args.local_comm_res = argv[kArgIndexLocalCommRes];
    args.remote_comm_res = argv[kArgIndexRemoteCommRes];
    is_client = (args.remote_engine.find(':') != std::string::npos);
    (void)printf(
        "[INFO] device_id = %s, local_engine = %s, remote_engine = %s, tcp_port = %s, transfer_mode = %s, "
        "transfer_op = %s, num_pairs = %d, local_comm_res = %s, remote_comm_res = %s\n",
        device_id_str.c_str(), args.local_engine.c_str(), args.remote_engine.c_str(), tcp_port_str.c_str(),
        args.transfer_mode.c_str(), args.transfer_op.c_str(), args.num_pairs,
        args.local_comm_res.c_str(), args.remote_comm_res.c_str());
  } else {
    (void)printf(
        "[ERROR] Expect 9 args(device_id, local_engine, remote_engine, tcp_port, transfer_mode, "
        "transfer_op, num_pairs, local_comm_res, remote_comm_res), but got %d\n",
        argc - 1);
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

  if (args.num_pairs < 1 || args.num_pairs > 10) {
    (void)printf("[ERROR] Invalid num_pairs: %d (should be 1-10)\n", args.num_pairs);
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
