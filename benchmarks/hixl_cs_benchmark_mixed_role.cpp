/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details.
 * You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY, OR FIT FOR A PARTICULAR PURPOSE.
 * See the License for the full text of the License.
 */

#include <cstdio>
#include <chrono>
#include <thread>
#include <iostream>
#include <vector>
#include <atomic>
#include <arpa/inet.h>
#include <map>
#include <cstring>
#include <mutex>
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
constexpr int32_t kClientConnectTimeoutMs = 5000;
constexpr int32_t kExpectedArgCnt = 11;
constexpr uint32_t kArgIndexDeviceId = 1;
constexpr uint32_t kArgIndexLocalEngine = 2;
constexpr uint32_t kArgIndexRemoteEngine = 3;
constexpr uint32_t kArgIndexTcpPort = 4;
constexpr uint32_t kArgIndexTransferMode = 5;
constexpr uint32_t kArgIndexTransferOp = 6;
constexpr uint32_t kArgIndexMyRole = 7;          // "client" or "server"
constexpr uint32_t kArgIndexLocalCommRes = 8;
constexpr uint32_t kArgIndexRemoteCommRes = 9;
constexpr uint32_t kArgIndexRemote2Engine = 10;  // Remote engine for the opposite role
constexpr uint32_t kTransferMemSize = 134217728;  // 128M
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
  std::string local_engine;      // This node's engine address for server role
  std::string remote_engine;      // Remote engine address for client role
  std::string remote2_engine;     // Remote engine address for server role (on opposite node)
  uint16_t tcp_port;
  std::string transfer_mode;
  std::string transfer_op;
  std::string my_role;            // "client" or "server"
  std::string local_comm_res;
  std::string remote_comm_res;
};

struct TransferResult {
  int32_t thread_id;
  bool success;
  int64_t time_us;
  uint64_t transfer_size;
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

void PrintThroughput(uint64_t size_bytes, int64_t time_us, const char *thread_name) {
  double time_second = static_cast<double>(time_us) / 1000 / 1000;
  double throughput_gb = static_cast<double>(size_bytes) / 1024 / 1024 / 1024 / time_second;
  (void)printf("[INFO] %s: Size: %.2f MB, Time: %.3f s, Throughput: %.3f GB/s\n",
               thread_name,
               static_cast<double>(size_bytes) / 1024 / 1024,
               time_second,
               throughput_gb);
}

int32_t DoTransfer(HixlClientHandle client_handle, uint8_t *local_addr, uint8_t *remote_addr,
                   const std::string &transfer_op, const char *thread_name,
                   TransferResult *result) {
  HixlOneSideOpDesc desc = {
    .remote_buf = remote_addr,
    .local_buf = local_addr,
    .len = kTransferMemSize
  };

  void *complete_handle = nullptr;
  const auto start = std::chrono::steady_clock::now();

  HixlStatus ret;
  if (transfer_op == "write") {
    HIXL_LOGI("%s: HixlCSClientBatchPutAsync start", thread_name);
    ret = HixlCSClientBatchPutAsync(client_handle, 1, &desc, &complete_handle);
  } else {
    HIXL_LOGI("%s: HixlCSClientBatchGetAsync start", thread_name);
    ret = HixlCSClientBatchGetAsync(client_handle, 1, &desc, &complete_handle);
  }

  if (ret != HIXL_SUCCESS) {
    (void)printf("[ERROR] %s: Batch operation failed, ret = %u\n", thread_name, ret);
    result->success = false;
    return -1;
  }

  // Wait for completion
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
      (void)printf("[ERROR] %s: QueryCompleteStatus failed, ret = %u\n", thread_name, ret);
      result->success = false;
      return -1;
    }
  }

  auto time_cost = std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now() - start).count();

  result->thread_id = 0;
  result->success = true;
  result->time_us = time_cost;
  result->transfer_size = kTransferMemSize;

  PrintThroughput(kTransferMemSize, time_cost, thread_name);
  return 0;
}

int32_t GetRemoteMem(HixlClientHandle client_handle, uint8_t **remote_addr) {
  CommMem *remote_mem_list = nullptr;
  char **mem_tag_list = nullptr;
  uint32_t list_num = 0U;
  auto ret = HixlCSClientGetRemoteMem(client_handle, &remote_mem_list, &mem_tag_list,
                                       &list_num, kClientConnectTimeoutMs);
  if (ret != HIXL_SUCCESS) {
    (void)printf("[ERROR] HixlCSClientGetRemoteMem failed, ret = %u\n", ret);
    return -1;
  }
  std::map<std::string, CommMem> server_mems;
  for (uint32_t i = 0; i < list_num; ++i) {
    server_mems[mem_tag_list[i]] = remote_mem_list[i];
  }
  *remote_addr = static_cast<uint8_t *>(server_mems[kServerMemTagName].addr);
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

uint32_t *mem_alloc(const std::string &transfer_op, bool is_client, aclrtMemcpyKind copy_kind,
                    void *mem_addr, uint64_t mem_size) {
  void *tmp = nullptr;
  std::string device = is_client ? "client" : "server";

  auto ret = aclrtMallocHost(&tmp, mem_size);
  if (ret != ACL_ERROR_NONE) {
    (void)printf("[ERROR] %s transfer_data aclrtMallocHost failed, ret = %d\n", device.c_str(), ret);
    return nullptr;
  }
  uint32_t *transfer_data = static_cast<uint32_t *>(tmp);
  HIXL_LOGI("The %s transfer_data addr is : %p", device.c_str(), transfer_data);

  if ((transfer_op == "write" && is_client) || (transfer_op == "read" && !is_client)) {
    for (uint32_t i = 0; i < mem_size / sizeof(uint32_t); i++) {
      transfer_data[i] = 1;
    }
    ret = aclrtMemcpy(mem_addr, mem_size, transfer_data, mem_size, copy_kind);
    if (ret != ACL_ERROR_NONE) {
      (void)printf("[ERROR] %s transfer_data aclrtMalloc failed, ret = %d\n", device.c_str(), ret);
    }
  }

  if ((transfer_op == "read" && is_client) || (transfer_op == "write" && !is_client)) {
    for (uint32_t i = 0; i < mem_size / sizeof(uint32_t); i++) {
      transfer_data[i] = 0;
    }
    ret = aclrtMemcpy(transfer_data, mem_size, mem_addr, mem_size, copy_kind);
    if (ret != ACL_ERROR_NONE) {
      (void)printf("[ERROR] %s transfer_data memcpy failed, ret = %d\n", device.c_str(), ret);
    }

    uint32_t error_num = 0;
    for (uint32_t i = 0; i < mem_size / sizeof(uint32_t); i++) {
      if (transfer_data[i] != 1) {
        error_num++;
      }
    }
    HIXL_LOGI("The error count for this data transfer task is %u", error_num);
  }
  return transfer_data;
}

// Run as client thread
int32_t RunClientThread(const Args &args, const char *thread_name) {
  (void)printf("[INFO] %s: Client thread start\n", thread_name);

  // 1. Initialize endpoint info
  EndpointDesc local_ep;
  EndpointDesc remote_ep;
  if (InitEndPointInfo(args.local_comm_res, local_ep) != 0 ||
      InitEndPointInfo(args.remote_comm_res, remote_ep) != 0) {
    (void)printf("[ERROR] %s: Initialize EndPoint list failed\n", thread_name);
    return -1;
  }

  // 2. Create client
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
    (void)printf("[ERROR] %s: HixlCSClientCreate failed, ret = %u\n", thread_name, ret);
    return -1;
  }

  // 3. Register memory (128MB)
  uint32_t *kClientTransferData = nullptr;
  MemHandle mem_handle = nullptr;
  CommMem mem{};
  aclrtMemcpyKind copy_kind;
  bool is_host = (args.transfer_mode == "h2d" || args.transfer_mode == "h2h");

  if (is_host) {
    void *tmp = malloc(kTransferMemSize);
    mem.addr = tmp;
    copy_kind = ACL_MEMCPY_HOST_TO_HOST;
    mem.type = COMM_MEM_TYPE_HOST;
    mem.size = kTransferMemSize;
    if (tmp == nullptr) {
      HIXL_LOGE(hixl::RESOURCE_EXHAUSTED, "%s: host addr malloc failed.", thread_name);
      ClientFinalize(client_handle, {mem_handle});
      return hixl::RESOURCE_EXHAUSTED;
    }
  } else {
    aclError acl_ret = aclrtMalloc(&mem.addr, kTransferMemSize, ACL_MEM_MALLOC_HUGE_ONLY);
    if (args.transfer_op == "read") {
      copy_kind = ACL_MEMCPY_DEVICE_TO_HOST;
    } else {
      copy_kind = ACL_MEMCPY_HOST_TO_DEVICE;
    }
    mem.type = COMM_MEM_TYPE_DEVICE;
    mem.size = kTransferMemSize;
    if (acl_ret != ACL_ERROR_NONE) {
      (void)printf("[ERROR] %s: aclrtMalloc failed, ret = %d\n", thread_name, acl_ret);
      ClientFinalize(client_handle, {mem_handle});
      return -1;
    }
  }

  ret = HixlCSClientRegMem(client_handle, kClientMemTagName, &mem, &mem_handle);
  if (ret != HIXL_SUCCESS) {
    (void)printf("[ERROR] %s: HixlCSClientRegMem failed, ret = %u\n", thread_name, ret);
    ClientFinalize(client_handle, {mem_handle});
    return -1;
  }
  (void)printf("[INFO] %s: Client memory registered\n", thread_name);

  // Initialize memory data if write operation
  if (args.transfer_op == "write") {
    kClientTransferData = mem_alloc(args.transfer_op, true, copy_kind, mem.addr, kTransferMemSize);
  }

  // 4. Connect to server
  ret = HixlCSClientConnect(client_handle, kClientConnectTimeoutMs);
  if (ret != HIXL_SUCCESS) {
    ClientFinalize(client_handle, {});
    (void)printf("[ERROR] %s: HixlCSClientConnect failed, ret = %u\n", thread_name, ret);
    return -1;
  }
  (void)printf("[INFO] %s: Client connected to server\n", thread_name);

  // Get remote memory address
  uint8_t *remote_addr = nullptr;
  if (GetRemoteMem(client_handle, &remote_addr) != 0) {
    ClientFinalize(client_handle, {mem_handle});
    return -1;
  }
  (void)printf("[INFO] %s: Got remote memory address: %p\n", thread_name, remote_addr);

  // 5. Transfer
  TransferResult result;
  if (DoTransfer(client_handle, static_cast<uint8_t *>(mem.addr), remote_addr,
                 args.transfer_op, thread_name, &result) != 0) {
    ClientFinalize(client_handle, {mem_handle});
    return -1;
  }

  // If read operation, verify data
  if (args.transfer_op == "read") {
    kClientTransferData = mem_alloc(args.transfer_op, true, copy_kind, mem.addr, kTransferMemSize);
  }

  // 6. Cleanup
  ClientFinalize(client_handle, {mem_handle});
  if (kClientTransferData != nullptr) {
    auto free_ret = aclrtFreeHost(kClientTransferData);
    if (free_ret != ACL_ERROR_NONE) {
      HIXL_LOGI("%s: kClientTransferData rtFreeHost failed, ret=%d", thread_name, free_ret);
    }
  }

  (void)printf("[INFO] %s: Client thread end\n", thread_name);
  return 0;
}

// Run as server thread
int32_t RunServerThread(const Args &args, const char *thread_name) {
  (void)printf("[INFO] %s: Server thread start\n", thread_name);

  // 1. Initialize endpoint info
  EndpointDesc ep;
  if (InitEndPointInfo(args.local_comm_res, ep) != 0) {
    (void)printf("[ERROR] %s: Initialize EndPoint list failed\n", thread_name);
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
    (void)printf("[ERROR] %s: HixlCSServerCreate failed, ret = %u\n", thread_name, ret);
    return -1;
  }

  ret = HixlCSServerListen(server_handle, kBackLog);
  if (ret != HIXL_SUCCESS) {
    ServerFinalize(server_handle, {});
    (void)printf("[ERROR] %s: HixlCSServerListen failed, ret = %u\n", thread_name, ret);
    return -1;
  }
  (void)printf("[INFO] %s: Server listening on %s:%d\n", thread_name, ip.c_str(), port);

  // 2. Register memory (128MB)
  uint32_t *kServerTransferData = nullptr;
  MemHandle mem_handle = nullptr;
  aclrtMemcpyKind copy_kind;
  CommMem mem{};
  bool is_host = (args.transfer_mode == "d2h" || args.transfer_mode == "h2h");

  if (is_host) {
    void *tmp = malloc(kTransferMemSize);
    mem.addr = tmp;
    copy_kind = ACL_MEMCPY_HOST_TO_HOST;
    mem.type = COMM_MEM_TYPE_HOST;
    mem.size = kTransferMemSize;
    if (tmp == nullptr) {
      HIXL_LOGE(hixl::RESOURCE_EXHAUSTED, "%s: host addr malloc failed.", thread_name);
      ServerFinalize(server_handle, {mem_handle});
      return hixl::RESOURCE_EXHAUSTED;
    }
  } else {
    aclError acl_ret = aclrtMalloc(&mem.addr, kTransferMemSize, ACL_MEM_MALLOC_HUGE_ONLY);
    if (args.transfer_op == "read") {
      copy_kind = ACL_MEMCPY_HOST_TO_DEVICE;
    } else {
      copy_kind = ACL_MEMCPY_DEVICE_TO_HOST;
    }
    mem.type = COMM_MEM_TYPE_DEVICE;
    mem.size = kTransferMemSize;
    if (acl_ret != ACL_ERROR_NONE) {
      (void)printf("[ERROR] %s: Server aclrtMalloc failed, ret = %d\n", thread_name, acl_ret);
      ServerFinalize(server_handle, {mem_handle});
      return -1;
    }
  }

  ret = HixlCSServerRegMem(server_handle, kServerMemTagName, &mem, &mem_handle);
  if (ret != HIXL_SUCCESS) {
    (void)printf("[ERROR] %s: HixlCSServerRegMem failed, ret = %u\n", thread_name, ret);
    ServerFinalize(server_handle, {mem_handle});
    return -1;
  }
  (void)printf("[INFO] %s: Server memory registered\n", thread_name);

  // Initialize memory if read operation
  if (args.transfer_op == "read") {
    kServerTransferData = mem_alloc(args.transfer_op, false, copy_kind, mem.addr, kTransferMemSize);
  }

  // 3. Wait for client to complete transfer (via TCP signal)
  TCPClient tcp_client;
  std::string remote_ip = args.remote2_engine.substr(0U, args.remote2_engine.find(':'));
  uint16_t remote_tcp_port = args.tcp_port;  // Use same TCP port

  if (!tcp_client.ConnectToServer(remote_ip, remote_tcp_port)) {
    (void)printf("[ERROR] %s: Failed to connect to client via TCP\n", thread_name);
    ServerFinalize(server_handle, {mem_handle});
    return -1;
  }
  (void)printf("[INFO] %s: Wait transfer begin\n", thread_name);
  if (tcp_client.ReceiveTaskStatus()) {
    (void)printf("[INFO] %s: Wait transfer end\n", thread_name);
  }
  tcp_client.Disconnect();

  // Copy data if write operation
  if (args.transfer_op == "write") {
    kServerTransferData = mem_alloc(args.transfer_op, false, copy_kind, mem.addr, kTransferMemSize);
  }

  // 4. Cleanup
  ServerFinalize(server_handle, {mem_handle});
  if (kServerTransferData != nullptr) {
    auto free_ret = aclrtFreeHost(kServerTransferData);
    if (free_ret != ACL_ERROR_NONE) {
      HIXL_LOGI("%s: kServerTransferData rtFreeHost failed, ret=%d", thread_name, free_ret);
    }
  }

  (void)printf("[INFO] %s: Server thread end\n", thread_name);
  return 0;
}

}  // namespace

void PrintUsage(const char *prog_name) {
  (void)printf("Usage: %s <device_id> <local_engine> <remote_engine> <tcp_port> <transfer_mode> "
               "<transfer_op> <my_role> <local_comm_res> <remote_comm_res> <remote2_engine>\n",
               prog_name);
  (void)printf("  device_id: Device ID (e.g., 0)\n");
  (void)printf("  local_engine: This node's server engine address (e.g., 192.168.1.161:19999)\n");
  (void)printf("  remote_engine: Remote server engine address for client role (e.g., 192.168.1.163:19999)\n");
  (void)printf("  tcp_port: TCP port for control channel (used by server role to wait for client signal)\n");
  (void)printf("  transfer_mode: d2d, d2h, h2h, h2d\n");
  (void)printf("  transfer_op: write or read\n");
  (void)printf("  my_role: 'client' or 'server' - this node's primary role\n");
  (void)printf("  local_comm_res: Local endpoint JSON\n");
  (void)printf("  remote_comm_res: Remote endpoint JSON\n");
  (void)printf("  remote2_engine: Remote server engine address for server role (e.g., 192.168.1.163:19998)\n");
  (void)printf("\nExample:\n");
  (void)printf("  On 161 (client role):\n");
  (void)printf("    %s 0 192.168.1.161:19998 192.168.1.163:19999 19997 h2d write client h2d h2d 192.168.1.163:19998\n", prog_name);
  (void)printf("  On 163 (server role):\n");
  (void)printf("    %s 0 192.168.1.163:19999 192.168.1.161:19998 19998 h2d write server h2d h2d 192.168.1.161:19999\n", prog_name);
}

int32_t main(int32_t argc, char **argv) {
  Args args{};
  std::string device_id_str;
  std::string tcp_port_str;

  if (argc != kExpectedArgCnt) {
    (void)printf("[ERROR] Expect %d args, but got %d\n", kExpectedArgCnt, argc - 1);
    PrintUsage(argv[0]);
    return -1;
  }

  device_id_str = argv[kArgIndexDeviceId];
  args.local_engine = argv[kArgIndexLocalEngine];
  args.remote_engine = argv[kArgIndexRemoteEngine];
  tcp_port_str = argv[kArgIndexTcpPort];
  args.transfer_mode = argv[kArgIndexTransferMode];
  args.transfer_op = argv[kArgIndexTransferOp];
  args.my_role = argv[kArgIndexMyRole];
  args.local_comm_res = argv[kArgIndexLocalCommRes];
  args.remote_comm_res = argv[kArgIndexRemoteCommRes];
  args.remote2_engine = argv[kArgIndexRemote2Engine];

  (void)printf("[INFO] device_id = %s\n", device_id_str.c_str());
  (void)printf("[INFO] local_engine = %s (for server role)\n", args.local_engine.c_str());
  (void)printf("[INFO] remote_engine = %s (for client role)\n", args.remote_engine.c_str());
  (void)printf("[INFO] remote2_engine = %s (for server role to wait client signal)\n", args.remote2_engine.c_str());
  (void)printf("[INFO] tcp_port = %s\n", tcp_port_str.c_str());
  (void)printf("[INFO] transfer_mode = %s\n", args.transfer_mode.c_str());
  (void)printf("[INFO] transfer_op = %s\n", args.transfer_op.c_str());
  (void)printf("[INFO] my_role = %s\n", args.my_role.c_str());

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

  // Start threads based on role
  // Each node runs TWO threads:
  // - Thread 1: based on my_role (either client or server)
  // - Thread 2: opposite of my_role

  std::vector<std::thread> threads;
  std::atomic<int32_t> results{0};

  if (args.my_role == "client") {
    // This node: Thread 0 = client, Thread 1 = server
    (void)printf("[INFO] Starting mixed role test:\n");
    (void)printf("[INFO]   Thread 0: CLIENT connecting to %s\n", args.remote_engine.c_str());
    (void)printf("[INFO]   Thread 1: SERVER listening on %s\n", args.local_engine.c_str());

    threads.emplace_back([&args, &results]() {
      int32_t ret = RunClientThread(args, "Thread-Client");
      if (ret != 0) results.fetch_or(1);
    });

    threads.emplace_back([&args, &results]() {
      int32_t ret = RunServerThread(args, "Thread-Server");
      if (ret != 0) results.fetch_or(2);
    });
  } else if (args.my_role == "server") {
    // This node: Thread 0 = server, Thread 1 = client
    (void)printf("[INFO] Starting mixed role test:\n");
    (void)printf("[INFO]   Thread 0: SERVER listening on %s\n", args.local_engine.c_str());
    (void)printf("[INFO]   Thread 1: CLIENT connecting to %s\n", args.remote_engine.c_str());

    threads.emplace_back([&args, &results]() {
      int32_t ret = RunServerThread(args, "Thread-Server");
      if (ret != 0) results.fetch_or(1);
    });

    threads.emplace_back([&args, &results]() {
      int32_t ret = RunClientThread(args, "Thread-Client");
      if (ret != 0) results.fetch_or(2);
    });
  } else {
    (void)printf("[ERROR] Invalid my_role: %s (must be 'client' or 'server')\n", args.my_role.c_str());
    return -1;
  }

  for (auto &t : threads) {
    t.join();
  }

  int32_t final_result = results.load();
  (void)printf("[INFO] All threads completed. Result: %d\n", final_result);

  CHECK_ACL_RETURN(aclrtResetDevice(args.device_id));
  return (final_result == 0) ? 0 : -1;
}
