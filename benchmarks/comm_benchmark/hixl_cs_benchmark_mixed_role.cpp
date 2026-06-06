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
constexpr int32_t kExpectedArgCnt = 10;
constexpr uint32_t kArgIndexDeviceId = 1;
constexpr uint32_t kArgIndexListenEngine = 2;      // 本节点server监听地址 IP:Port
constexpr uint32_t kArgIndexConnectEngine = 3;      // 对端server监听地址 IP:Port (本节点client连接此地址)
constexpr uint32_t kArgIndexTcpPort = 4;           // TCP控制通道端口
constexpr uint32_t kArgIndexTransferMode = 5;
constexpr uint32_t kArgIndexTransferOp = 6;
constexpr uint32_t kArgIndexMyRole = 7;            // "client" or "server"
constexpr uint32_t kArgIndexLocalEp = 8;           // 本节点endpoint JSON
constexpr uint32_t kArgIndexRemoteEp = 9;          // 对端endpoint JSON

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
  std::string listen_engine;      // 本节点server监听地址 (IP:Port)
  std::string connect_engine;     // 对端server监听地址 (IP:Port)
  uint16_t tcp_port;
  std::string transfer_mode;
  std::string transfer_op;
  std::string my_role;            // "client" or "server"
  std::string local_ep;           // 本节点endpoint JSON
  std::string remote_ep;          // 对端endpoint JSON
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

// 从地址字符串解析IP和端口
bool ParseEngineAddr(const std::string &engine, std::string &ip, uint16_t &port) {
  size_t colon_pos = engine.find(':');
  if (colon_pos == std::string::npos) {
    return false;
  }
  ip = engine.substr(0, colon_pos);
  try {
    port = static_cast<uint16_t>(std::stoi(engine.substr(colon_pos + 1)));
  } catch (...) {
    return false;
  }
  return true;
}

// Run as client thread
// connect_engine: 对端server的监听地址 (IP:Port)
// local_ep: 本节点的endpoint
// remote_ep: 对端节点的endpoint
int32_t RunClientThread(const std::string &connect_engine,
                        const std::string &local_ep_str,
                        const std::string &remote_ep_str,
                        const std::string &transfer_mode,
                        const std::string &transfer_op,
                        const char *thread_name) {
  (void)printf("[INFO] %s: Client thread start\n", thread_name);

  // 1. Initialize endpoint info
  EndpointDesc local_ep;
  EndpointDesc remote_ep;
  if (InitEndPointInfo(local_ep_str, local_ep) != 0 ||
      InitEndPointInfo(remote_ep_str, remote_ep) != 0) {
    (void)printf("[ERROR] %s: Initialize EndPoint list failed\n", thread_name);
    return -1;
  }

  // 2. Create client - 连接对端server
  HixlClientHandle client_handle = nullptr;
  std::string server_ip;
  uint16_t server_port;
  if (!ParseEngineAddr(connect_engine, server_ip, server_port)) {
    (void)printf("[ERROR] %s: Invalid connect_engine format: %s\n", thread_name, connect_engine.c_str());
    return -1;
  }

  HixlClientDesc client_desc = {.local_endpoint = &local_ep,
                                .remote_endpoint = &remote_ep,
                                .server_ip = server_ip.c_str(),
                                .server_port = static_cast<uint32_t>(server_port),
                                .tc = 0U,
                                .sl = 0U};
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
  bool is_host = (transfer_mode == "h2d" || transfer_mode == "h2h");

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
    if (transfer_op == "read") {
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
  if (transfer_op == "write") {
    kClientTransferData = mem_alloc(transfer_op, true, copy_kind, mem.addr, kTransferMemSize);
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
                 transfer_op, thread_name, &result) != 0) {
    ClientFinalize(client_handle, {mem_handle});
    return -1;
  }

  // If read operation, verify data
  if (transfer_op == "read") {
    kClientTransferData = mem_alloc(transfer_op, true, copy_kind, mem.addr, kTransferMemSize);
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
// listen_engine: 本节点server监听地址 (IP:Port)
// local_ep_str: 本节点endpoint JSON
// remote_ep_str: 对端endpoint JSON (用于TCP信号通道)
// tcp_port: TCP控制通道端口
int32_t RunServerThread(const std::string &listen_engine,
                        const std::string &local_ep_str,
                        const std::string &remote_ep_str,
                        uint16_t tcp_port,
                        const std::string &transfer_mode,
                        const std::string &transfer_op,
                        const char *thread_name) {
  (void)printf("[INFO] %s: Server thread start\n", thread_name);

  // 1. Initialize endpoint info
  EndpointDesc ep;
  if (InitEndPointInfo(local_ep_str, ep) != 0) {
    (void)printf("[ERROR] %s: Initialize EndPoint list failed\n", thread_name);
    return -1;
  }

  // 解析本节点监听地址
  std::string listen_ip;
  uint16_t listen_port;
  if (!ParseEngineAddr(listen_engine, listen_ip, listen_port)) {
    (void)printf("[ERROR] %s: Invalid listen_engine format: %s\n", thread_name, listen_engine.c_str());
    return -1;
  }

  HixlServerHandle server_handle = nullptr;
  HixlServerConfig config{};
  HixlServerDesc server_desc = {.endpoint_list = &ep,
                                .server_ip = listen_ip.c_str(),
                                .server_port = static_cast<uint32_t>(listen_port),
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
  (void)printf("[INFO] %s: Server listening on %s:%d\n", thread_name, listen_ip.c_str(), listen_port);

  // 2. Register memory (128MB)
  uint32_t *kServerTransferData = nullptr;
  MemHandle mem_handle = nullptr;
  aclrtMemcpyKind copy_kind;
  CommMem mem{};
  bool is_host = (transfer_mode == "d2h" || transfer_mode == "h2h");

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
    if (transfer_op == "read") {
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
  if (transfer_op == "read") {
    kServerTransferData = mem_alloc(transfer_op, false, copy_kind, mem.addr, kTransferMemSize);
  }

  // 3. Wait for client to complete transfer (via TCP signal)
  // TCP用于等待对端client发送传输完成信号
  // remote_ep_str中包含对端地址信息，用于建立TCP连接
  TCPClient tcp_client;
  std::string remote_ip;
  uint16_t remote_tcp_port;
  if (!ParseEngineAddr(remote_ep_str, remote_ip, remote_tcp_port)) {
    // remote_ep_str格式可能是JSON而不是IP:Port，尝试解析JSON中的addr
    try {
      json ep_json = json::parse(remote_ep_str);
      remote_ip = ep_json.at("addr").get<std::string>();
      remote_tcp_port = tcp_port;  // 使用传入的tcp_port
    } catch (const std::exception &e) {
      (void)printf("[ERROR] %s: Failed to parse remote endpoint: %s\n", thread_name, e.what());
      ServerFinalize(server_handle, {mem_handle});
      return -1;
    }
  }

  // 如果remote_ip是本端地址，使用tcp_port作为监听端口
  if (remote_ip == listen_ip) {
    remote_tcp_port = tcp_port;
  }

  if (!tcp_client.ConnectToServer(remote_ip, remote_tcp_port)) {
    (void)printf("[ERROR] %s: Failed to connect to client via TCP at %s:%d\n",
                 thread_name, remote_ip.c_str(), remote_tcp_port);
    ServerFinalize(server_handle, {mem_handle});
    return -1;
  }
  (void)printf("[INFO] %s: Wait transfer begin\n", thread_name);
  if (tcp_client.ReceiveTaskStatus()) {
    (void)printf("[INFO] %s: Wait transfer end\n", thread_name);
  }
  tcp_client.Disconnect();

  // Copy data if write operation
  if (transfer_op == "write") {
    kServerTransferData = mem_alloc(transfer_op, false, copy_kind, mem.addr, kTransferMemSize);
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
  (void)printf("Usage: %s <device_id> <listen_engine> <connect_engine> <tcp_port> "
               "<transfer_mode> <transfer_op> <my_role> <local_ep> <remote_ep>\n",
               prog_name);
  (void)printf("  device_id: Device ID (e.g., 0)\n");
  (void)printf("  listen_engine: This node's server listen address (IP:Port)\n");
  (void)printf("  connect_engine: Remote server address to connect (IP:Port)\n");
  (void)printf("  tcp_port: TCP port for control channel\n");
  (void)printf("  transfer_mode: d2d, d2h, h2h, h2d\n");
  (void)printf("  transfer_op: write or read\n");
  (void)printf("  my_role: 'client' or 'server' - this node's primary role\n");
  (void)printf("  local_ep: Local endpoint JSON ({\"location\":\"host\",\"protocol\":\"roce\",\"addr\":\"IP\"})\n");
  (void)printf("  remote_ep: Remote endpoint JSON\n");
  (void)printf("\nDual-Active Example (Two nodes, Node A and Node B):\n");
  (void)printf("  Node A listens on 192.168.1.161:19998, connects to Node B at 192.168.1.163:19999\n");
  (void)printf("  Node B listens on 192.168.1.163:19999, connects to Node A at 192.168.1.161:19998\n");
  (void)printf("\nOn Node A (client role):\n");
  (void)printf("  %s 0 192.168.1.161:19998 192.168.1.163:19999 19997 h2h write client "
               "'{\"location\":\"host\",\"protocol\":\"roce\",\"addr\":\"192.168.1.161\"}' "
               "'{\"location\":\"host\",\"protocol\":\"roce\",\"addr\":\"192.168.1.163\"}'\n", prog_name);
  (void)printf("\nOn Node B (server role):\n");
  (void)printf("  %s 0 192.168.1.163:19999 192.168.1.161:19998 19998 h2h write server "
               "'{\"location\":\"host\",\"protocol\":\"roce\",\"addr\":\"192.168.1.163\"}' "
               "'{\"location\":\"host\",\"protocol\":\"roce\",\"addr\":\"192.168.1.161\"}'\n", prog_name);
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
  args.listen_engine = argv[kArgIndexListenEngine];
  args.connect_engine = argv[kArgIndexConnectEngine];
  tcp_port_str = argv[kArgIndexTcpPort];
  args.transfer_mode = argv[kArgIndexTransferMode];
  args.transfer_op = argv[kArgIndexTransferOp];
  args.my_role = argv[kArgIndexMyRole];
  args.local_ep = argv[kArgIndexLocalEp];
  args.remote_ep = argv[kArgIndexRemoteEp];

  (void)printf("[INFO] device_id = %s\n", device_id_str.c_str());
  (void)printf("[INFO] listen_engine = %s (this node's server listen addr)\n", args.listen_engine.c_str());
  (void)printf("[INFO] connect_engine = %s (remote server addr to connect)\n", args.connect_engine.c_str());
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
    (void)printf("[INFO]   Thread 0: CLIENT connecting to %s\n", args.connect_engine.c_str());
    (void)printf("[INFO]   Thread 1: SERVER listening on %s\n", args.listen_engine.c_str());

    threads.emplace_back([&args, &results]() {
      int32_t ret = RunClientThread(args.connect_engine, args.local_ep, args.remote_ep,
                                     args.transfer_mode, args.transfer_op, "Thread-Client");
      if (ret != 0) results.fetch_or(1);
    });

    threads.emplace_back([&args, &results]() {
      int32_t ret = RunServerThread(args.listen_engine, args.local_ep, args.remote_ep,
                                     args.tcp_port, args.transfer_mode, args.transfer_op,
                                     "Thread-Server");
      if (ret != 0) results.fetch_or(2);
    });
  } else if (args.my_role == "server") {
    // This node: Thread 0 = server, Thread 1 = client
    (void)printf("[INFO] Starting mixed role test:\n");
    (void)printf("[INFO]   Thread 0: SERVER listening on %s\n", args.listen_engine.c_str());
    (void)printf("[INFO]   Thread 1: CLIENT connecting to %s\n", args.connect_engine.c_str());

    threads.emplace_back([&args, &results]() {
      int32_t ret = RunServerThread(args.listen_engine, args.local_ep, args.remote_ep,
                                     args.tcp_port, args.transfer_mode, args.transfer_op,
                                     "Thread-Server");
      if (ret != 0) results.fetch_or(1);
    });

    threads.emplace_back([&args, &results]() {
      int32_t ret = RunClientThread(args.connect_engine, args.local_ep, args.remote_ep,
                                     args.transfer_mode, args.transfer_op, "Thread-Client");
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
