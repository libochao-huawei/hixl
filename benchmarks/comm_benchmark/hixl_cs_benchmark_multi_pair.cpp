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
constexpr int32_t kClientConnectTimeoutMs = 60000;
constexpr int32_t kExpectedArgCnt = 9;
constexpr uint32_t kArgIndexDeviceId = 1;
constexpr uint32_t kArgIndexLocalEngine = 2;
constexpr uint32_t kArgIndexRemoteEngine = 3;
constexpr uint32_t kArgIndexTcpPort = 4;
constexpr uint32_t kArgIndexTransferMode = 5;
constexpr uint32_t kArgIndexTransferOp = 6;
constexpr uint32_t kArgIndexLocalCommRes = 7;
constexpr uint32_t kArgIndexRemoteCommRes = 8;

// 2GB transfer size
constexpr uint64_t kTransferMemSize = 2ULL * 1024 * 1024 * 1024;
// 128MB block size
constexpr uint64_t kBaseBlockSize = 128ULL * 1024 * 1024;
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

int32_t Transfer(HixlClientHandle client_handle, uint8_t *local_addr, const std::string &transfer_op) {
  CommMem *remote_mem_list = nullptr;
  char **mem_tag_list = nullptr;
  uint32_t list_num = 0U;
  auto ret =
      HixlCSClientGetRemoteMem(client_handle, &remote_mem_list, &mem_tag_list, &list_num, kClientConnectTimeoutMs);
  if (ret != HIXL_SUCCESS) {
    (void)printf("[ERROR] HixlCSClientGetRemoteMem failed, ret = %u\n", ret);
    return -1;
  }
  std::map<std::string, CommMem> server_mems;
  for (uint32_t i = 0; i < list_num; ++i) {
    server_mems[mem_tag_list[i]] = remote_mem_list[i];
  }
  uint8_t *remote_addr = static_cast<uint8_t *>(server_mems[kServerMemTagName].addr);

  // Single transfer with 2GB, using 128MB blocks
  auto block_size = kBaseBlockSize;
  auto trans_num = static_cast<uint32_t>(kTransferMemSize / block_size);

  (void)printf("[INFO] Transfer: size=%lu bytes, num_blocks=%u, block_size=%lu bytes\n",
               kTransferMemSize, trans_num, block_size);

  std::vector<HixlOneSideOpDesc> desc_list(trans_num);
  for (uint32_t j = 0; j < trans_num; j++) {
    desc_list[j].remote_buf = remote_addr + j * block_size;
    desc_list[j].local_buf = local_addr + j * block_size;
    desc_list[j].len = block_size;
  }

  void *complete_handle = nullptr;
  const auto start = std::chrono::steady_clock::now();

  if (transfer_op == "write") {
    HIXL_LOGI("HixlCSClientBatchPutAsync start, trans_num is:%u", trans_num);
    ret = HixlCSClientBatchPutAsync(client_handle, trans_num, desc_list.data(), &complete_handle);
  } else {
    HIXL_LOGI("HixlCSClientBatchGetAsync start, trans_num is:%u", trans_num);
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
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      continue;
    }
    if (ret != HIXL_SUCCESS) {
      (void)printf("[ERROR] HixlCSClientQueryCompleteStatus failed, ret = %u\n", ret);
      return -1;
    }
  }

  auto time_cost = std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now() - start).count();
  PrintThroughput(kTransferMemSize, time_cost);
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

  // Only initialize/verify a smaller portion for 2GB to save time
  uint64_t check_size = 64ULL * 1024 * 1024;  // Check first 64MB for verification

  if ((transfer_op == "write" && is_client) || (transfer_op == "read" && !is_client)) {
    for (uint32_t i = 0; i < check_size / sizeof(uint32_t); i++) {
      transfer_data[i] = 1;
    }
    ret = aclrtMemcpy(mem_addr, check_size, transfer_data, check_size, copy_kind);
    if (ret != ACL_ERROR_NONE) {
      (void)printf("[ERROR] %s transfer_data aclrtMemcpy failed, ret = %d\n", device.c_str(), ret);
    }
    HIXL_LOGI("The %s transfer_data have been copy to mem (first %lu MB).", device.c_str(), check_size / 1024 / 1024);
  }

  if ((transfer_op == "read" && is_client) || (transfer_op == "write" && !is_client)) {
    for (uint32_t i = 0; i < check_size / sizeof(uint32_t); i++) {
      transfer_data[i] = 0;
    }
    ret = aclrtMemcpy(transfer_data, check_size, mem_addr, check_size, copy_kind);
    if (ret != ACL_ERROR_NONE) {
      (void)printf("[ERROR] %s transfer_data aclrtMemcpy failed, ret = %d\n", device.c_str(), ret);
    }

    uint32_t error_num = 0;
    for (uint32_t i = 0; i < check_size / sizeof(uint32_t); i++) {
      if (transfer_data[i] != 1) {
        error_num++;
      }
    }
    HIXL_LOGI("The error count for this data transfer task (first %lu MB) is %u",
              check_size / 1024 / 1024, error_num);
  }
  return transfer_data;
}

int32_t RunClient(const Args &args) {
  (void)printf("[INFO] Client start, transfer size: %lu bytes (2GB)\n", kTransferMemSize);

  // Start TCP server to receive completion signal from server
  TCPServer tcp_server;
  if (!tcp_server.StartServer(args.tcp_port)) {
    (void)printf("[ERROR] Failed to start TCP server on port %d.\n", args.tcp_port);
    return -1;
  }
  (void)printf("[INFO] TCP server started on port %d.\n", args.tcp_port);
  if (!tcp_server.AcceptConnection()) {
    return -1;
  }

  // 1. Initialize endpoint info
  EndpointDesc local_ep;
  EndpointDesc remote_ep;
  if (InitEndPointInfo(args.local_comm_res, local_ep) != 0 ||
      InitEndPointInfo(args.remote_comm_res, remote_ep) != 0) {
    (void)printf("[ERROR] Initialize EndPoint list failed\n");
    return -1;
  }

  HixlClientHandle client_handle = nullptr;
  std::string ip = args.remote_engine.substr(0U, args.remote_engine.find(':'));
  int32_t port = std::stoi(args.remote_engine.substr(args.remote_engine.find(':') + 1U));
  HixlClientDesc client_desc = {.local_endpoint = &local_ep,
                                .remote_endpoint = &remote_ep,
                                .server_ip = ip.c_str(),
                                .server_port = static_cast<uint32_t>(port),
                                .tc = 0U,
                                .sl = 0U};
  HixlClientConfig client_config{};
  auto ret = HixlCSClientCreate(&client_desc, &client_config, &client_handle);
  if (ret != HIXL_SUCCESS) {
    (void)printf("[ERROR] HixlCSClientCreate failed, ret = %u\n", ret);
    return -1;
  }

  // 2. Register 2GB memory
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
      HIXL_LOGE(hixl::RESOURCE_EXHAUSTED, "Client host addr malloc failed.");
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
      (void)printf("[ERROR] aclrtMalloc failed, ret = %d\n", acl_ret);
      ClientFinalize(client_handle, {mem_handle});
      return -1;
    }
  }
  (void)printf("[INFO] Client memory allocated, size: %lu bytes\n", kTransferMemSize);

  ret = HixlCSClientRegMem(client_handle, kClientMemTagName, &mem, &mem_handle);
  if (ret != HIXL_SUCCESS) {
    (void)printf("[ERROR] HixlCSClientRegMem failed, ret = %u\n", ret);
    ClientFinalize(client_handle, {mem_handle});
    return -1;
  }
  (void)printf("[INFO] Client memory registered.\n");

  // Initialize memory data if write operation
  if (args.transfer_op == "write") {
    kClientTransferData = mem_alloc(args.transfer_op, true, copy_kind, mem.addr, kTransferMemSize);
  }

  // 3. Connect to server
  ret = HixlCSClientConnect(client_handle, kClientConnectTimeoutMs);
  if (ret != HIXL_SUCCESS) {
    ClientFinalize(client_handle, {});
    (void)printf("[ERROR] HixlCSClientConnect failed, ret = %u\n", ret);
    return -1;
  }
  (void)printf("[INFO] Client connected to server %s:%d\n", ip.c_str(), port);

  // 4. Transfer data (2GB with 128MB blocks)
  if (Transfer(client_handle, static_cast<uint8_t *>(mem.addr), args.transfer_op) != 0) {
    ClientFinalize(client_handle, {mem_handle});
    return -1;
  }

  // 5. Verify data if read operation
  if (args.transfer_op == "read") {
    kClientTransferData = mem_alloc(args.transfer_op, true, copy_kind, mem.addr, kTransferMemSize);
  }

  // 6. Cleanup
  ClientFinalize(client_handle, {mem_handle});
  if (kClientTransferData != nullptr) {
    auto free_ret = aclrtFreeHost(kClientTransferData);
    if (free_ret != ACL_ERROR_NONE) {
      HIXL_LOGI("kClientTransferData rtFreeHost failed, ret=%d", free_ret);
    }
  }

  // Notify server via TCP
  (void)tcp_server.SendTaskStatus();
  tcp_server.DisConnectClient();
  tcp_server.StopServer();

  (void)printf("[INFO] Client end\n");
  return 0;
}

int32_t RunServer(const Args &args) {
  (void)printf("[INFO] Server start, transfer size: %lu bytes (2GB)\n", kTransferMemSize);

  // 1. Initialize endpoint info
  EndpointDesc ep;
  if (InitEndPointInfo(args.local_comm_res, ep) != 0) {
    (void)printf("[ERROR] Initialize EndPoint list failed\n");
    return -1;
  }

  HixlServerHandle server_handle = nullptr;
  std::string ip = args.local_engine.substr(0, args.local_engine.find(':'));
  int32_t port = std::stoi(args.local_engine.substr(args.local_engine.find(':') + 1));
  HixlServerConfig config{};
  HixlServerDesc server_desc = {.endpoint_list = &ep,
                                .server_ip = ip.c_str(),
                                .server_port = static_cast<uint32_t>(port),
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
  (void)printf("[INFO] Server listening on %s:%d\n", ip.c_str(), port);

  // 2. Register 2GB memory
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
      HIXL_LOGE(hixl::RESOURCE_EXHAUSTED, "Server host addr malloc failed.");
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
      (void)printf("[ERROR] Server aclrtMalloc failed, ret = %d\n", acl_ret);
      ServerFinalize(server_handle, {mem_handle});
      return -1;
    }
  }
  (void)printf("[INFO] Server memory allocated, size: %lu bytes\n", kTransferMemSize);

  ret = HixlCSServerRegMem(server_handle, kServerMemTagName, &mem, &mem_handle);
  if (ret != HIXL_SUCCESS) {
    (void)printf("[ERROR] HixlCSServerRegMem failed, ret = %u\n", ret);
    ServerFinalize(server_handle, {mem_handle});
    return -1;
  }
  (void)printf("[INFO] Server memory registered.\n");

  // Initialize memory if read operation
  if (args.transfer_op == "read") {
    kServerTransferData = mem_alloc(args.transfer_op, false, copy_kind, mem.addr, kTransferMemSize);
  }

  // 3. Wait for client to complete transfer
  TCPClient tcp_client;
  std::string remote_ip = args.remote_engine.substr(0U, args.remote_engine.find(':'));
  if (!tcp_client.ConnectToServer(remote_ip, args.tcp_port)) {
    (void)printf("[ERROR] Failed to connect to client via TCP at %s:%d\n", remote_ip.c_str(), args.tcp_port);
    ServerFinalize(server_handle, {mem_handle});
    return -1;
  }
  (void)printf("[INFO] Wait transfer begin\n");
  if (tcp_client.ReceiveTaskStatus()) {
    (void)printf("[INFO] Wait transfer end\n");
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
      HIXL_LOGI("kServerTransferData rtFreeHost failed, ret=%d", free_ret);
    }
  }

  (void)printf("[INFO] Server end\n");
  return 0;
}

}  // namespace

void PrintUsage(const char *prog_name) {
  (void)printf("Usage: %s <device_id> <local_engine> <remote_engine> <tcp_port> <transfer_mode> "
               "<transfer_op> <local_comm_res> <remote_comm_res>\n",
               prog_name);
  (void)printf("  device_id: Device ID (e.g., 0)\n");
  (void)printf("  local_engine: Server engine address (e.g., 127.0.0.1:19999)\n");
  (void)printf("  remote_engine: Client connects to server address (e.g., 127.0.0.1:19998)\n");
  (void)printf("  tcp_port: TCP port for control channel (different instances should use different ports)\n");
  (void)printf("  transfer_mode: d2d, d2h, h2h, h2d\n");
  (void)printf("  transfer_op: write or read\n");
  (void)printf("  local_comm_res: Local endpoint JSON\n");
  (void)printf("  remote_comm_res: Remote endpoint JSON\n");
  (void)printf("\nNote: Each instance uses unique ports (tcp_port for TCP control, engine port from local_engine/remote_engine).\n");
  (void)printf("To run multiple instances in parallel, use different ports for each instance.\n");
  (void)printf("\nExample (server instance 1): %s 0 127.0.0.1:19999 127.0.0.1:19998 19997 h2h write h2h h2h\n", prog_name);
  (void)printf("Example (server instance 2): %s 0 127.0.0.1:19989 127.0.0.1:19988 19987 h2h write h2h h2h\n", prog_name);
  (void)printf("Example (client instance 1): %s 0 127.0.0.1:19999 127.0.0.1:19998 19997 h2h write h2h h2h\n", prog_name);
  (void)printf("Example (client instance 2): %s 0 127.0.0.1:19989 127.0.0.1:19988 19987 h2h write h2h h2h\n", prog_name);
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
    args.local_comm_res = argv[kArgIndexLocalCommRes];
    args.remote_comm_res = argv[kArgIndexRemoteCommRes];
    is_client = (args.remote_engine.find(':') != std::string::npos);
    (void)printf(
        "[INFO] device_id = %s, local_engine = %s, remote_engine = %s, tcp_port = %s, transfer_mode = %s, "
        "transfer_op = %s, local_comm_res = %s, remote_comm_res = %s\n",
        device_id_str.c_str(), args.local_engine.c_str(), args.remote_engine.c_str(), tcp_port_str.c_str(),
        args.transfer_mode.c_str(), args.transfer_op.c_str(),
        args.local_comm_res.c_str(), args.remote_comm_res.c_str());
  } else {
    (void)printf(
        "[ERROR] Expect 8 args(device_id, local_engine, remote_engine, tcp_port, transfer_mode, "
        "transfer_op, local_comm_res, remote_comm_res), but got %d\n",
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

  int32_t ret = 0;
  if (is_client) {
    ret = RunClient(args);
  } else {
    ret = RunServer(args);
  }

  CHECK_ACL_RETURN(aclrtResetDevice(args.device_id));
  return ret;
}
