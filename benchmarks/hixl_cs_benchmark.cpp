/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <cstdio>
#include <thread>
#include <iostream>
#include <vector>
#include <arpa/inet.h>
#include <map>
#include "nlohmann/json.hpp"
#include "common/tcp_client_server.h"
#include "acl/acl.h"
#include "hixl/common/hixl_cs.h"

using json = nlohmann::json;

void from_json(const json &j, EndPointLocation &l) {
  std::string s = j.get<std::string>();
  if (s == "host") {
    l = END_POINT_LOCATION_HOST;
  } else {
    l = END_POINT_LOCATION_DEVICE;
  }
}

void from_json(const json &j, CommProtocol &p) {
  std::string s = j.get<std::string>();
  if (s == "hccs") {
    p = COMM_PROTOCOL_HCCS;
  } else if (s == "roce") {
    p = COMM_PROTOCOL_ROCE;
  } else if (s == "UB_CTP") {
    p = COMM_PROTOCOL_UB_CTP;
  } else if (s == "UB_TP") {
    p = COMM_PROTOCOL_UB_TP;
  } else {
    p = COMM_PROTOCOL_RESERVED;
  }
}

void from_json(const json &j, EndPointDesc &info) {
  j.at("location").get_to(info.loc.locType);
  j.at("protocol").get_to(info.protocol);
  std::string addr;
  j.at("addr").get_to(addr);
  if (info.protocol == COMM_PROTOCOL_ROCE) {
    if (inet_pton(AF_INET, addr.c_str(), &info.addr.addr) == 1) {
      info.addr.type = COMM_ADDR_TYPE_IP_V4;
    } else if (inet_pton(AF_INET6, addr.c_str(), &info.addr.addr6) == 1) {
      info.addr.type = COMM_ADDR_TYPE_IP_V6;
    } else {
      info.addr.type = COMM_ADDR_TYPE_RESERVED;
    }
  }
}

namespace {
constexpr int32_t kWaitTransTime = 20;
constexpr int32_t kClientConnectTimeoutMs = 5000;
constexpr int32_t kExpectedArgCnt = 9;
constexpr uint32_t kArgIndexDeviceId = 1;
constexpr uint32_t kArgIndexLocalEngine = 2;
constexpr uint32_t kArgIndexRemoteEngine = 3;
constexpr uint32_t kArgIndexTcpPort = 4;
constexpr uint32_t kArgIndexTransferMode = 5;
constexpr uint32_t kArgIndexTransferOp = 6;
constexpr uint32_t kArgIndexLocalCommRes = 7;
constexpr uint32_t kArgIndexRemoteCommRes = 8;
constexpr uint32_t kTransferMemSize = 134217728;  // 128M
constexpr uint32_t kBaseBlockSize = 262144;       // 0.25M
constexpr uint32_t kExecuteRepeatNum = 5;
constexpr int32_t kPortMaxValue = 65535;
constexpr int32_t kBackLog = 1024;
constexpr const char *kServerMemTagName = "server_mem";
constexpr const int32_t kStatus = 0;

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

int32_t InitEndPointInfo(const std::string &comm_res, EndPointDesc &ep) {
  try {
    ep = json::parse(comm_res).get<EndPointDesc>();
  } catch (const std::exception &e) {
    (void)printf("Failed to parse json:%s\n", e.what());
    return -1;
  }
  return 0;
}

int32_t Transfer(HixlClientHandle client_handle, uint8_t *local_addr, const std::string &transfer_op) {
  HcclMem *remote_mem_list = nullptr;
  char **mem_tag_list = nullptr;
  uint32_t list_num = 0U;
  auto ret =
      HixlCSClientGetRemoteMem(client_handle, &remote_mem_list, &mem_tag_list, &list_num, kClientConnectTimeoutMs);
  if (ret != HIXL_SUCCESS) {
    (void)printf("[ERROR] HixlCSClientGetRemoteMem failed, ret = %u\n", ret);
    return -1;
  }
  std::map<std::string, HcclMem> server_mems;
  for (uint32_t i = 0; i < list_num; ++i) {
    server_mems[mem_tag_list[i]] = remote_mem_list[i];
  }
  uint8_t *remote_addr = static_cast<uint8_t *>(server_mems[kServerMemTagName].addr);

  for (uint32_t i = 0; i <= kExecuteRepeatNum; i++) {
    auto block_size = kBaseBlockSize * (1U << i);
    auto trans_num = kTransferMemSize / block_size;
    std::vector<const void *> local_addrs;
    std::vector<void *> remote_addrs;
    std::vector<uint64_t> lens;
    for (uint32_t j = 0; j < trans_num; j++) {
      local_addrs.emplace_back(local_addr + j * block_size);
      remote_addrs.emplace_back(remote_addr + j * block_size);
      lens.emplace_back(block_size);
    }
    void *complete_handle = nullptr;
    const auto start = std::chrono::steady_clock::now();
    if (transfer_op == "write") {
      ret =
          HixlCSClientBatchput(client_handle, trans_num, &remote_addrs[0], &local_addrs[0], &lens[0], &complete_handle);
    } else {
      ret =
          HixlCSClientBatchget(client_handle, trans_num, &remote_addrs[0], &local_addrs[0], &lens[0], &complete_handle);
    }
    if (ret != HIXL_SUCCESS) {
      (void)printf("[ERROR] HixlCSClientBatchput/HixlCSClientBatchget failed, ret = %u\n", ret);
      return -1;
    }
    int32_t status = kStatus;
    while (true) {
      ret = HixlCSClientQueryCompleteStatus(client_handle, complete_handle, &status);
      if (ret == HIXL_SUCCESS) {
        break;
      } else if (ret == HIXL_NOT_READY) {
        continue;
      } else {
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
  (void)printf("[INFO] client start\n");

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
  EndPointDesc local_ep;
  EndPointDesc remote_ep;
  if (InitEndPointInfo(args.local_comm_res, local_ep) != 0 || InitEndPointInfo(args.remote_comm_res, remote_ep) != 0) {
    (void)printf("[ERROR] Initialize EndPoint list failed\n");
    return -1;
  }
  HixlClientHandle client_handle = nullptr;
  std::string ip = args.remote_engine.substr(0U, args.remote_engine.find(':'));
  int32_t port = std::stoi(args.remote_engine.substr(args.remote_engine.find(':') + 1U));
  auto ret = HixlCSClientCreate(ip.c_str(), port, &local_ep, &remote_ep, &client_handle);
  if (ret != HIXL_SUCCESS) {
    (void)printf("[ERROR] HixlCSClientCreate failed, ret = %u\n", ret);
    return -1;
  }

  // 2. 建链
  ret = HixlCSClientConnectSync(client_handle, kClientConnectTimeoutMs);
  if (ret != HIXL_SUCCESS) {
    ClientFinalize(client_handle, {});
    (void)printf("[ERROR] HixlCSClientConnectSync failed, ret = %u\n", ret);
    return -1;
  }

  // 3. 注册内存地址
  MemHandle mem_handle = nullptr;
  HcclMem mem{};
  aclError acl_ret = ACL_ERROR_NONE;
  bool is_host = (args.transfer_mode == "h2d" || args.transfer_mode == "h2h");
  if (is_host) {
    acl_ret = aclrtMallocHost(&mem.addr, kTransferMemSize);
    mem.type = HCCL_MEM_TYPE_HOST;
  } else {
    acl_ret = aclrtMalloc(&mem.addr, kTransferMemSize, ACL_MEM_MALLOC_HUGE_ONLY);
    mem.type = HCCL_MEM_TYPE_DEVICE;
  }
  if (acl_ret != ACL_ERROR_NONE) {
    (void)printf("[ERROR] aclrtMalloc failed, ret = %d\n", acl_ret);
    ClientFinalize(client_handle, {mem_handle});
    return -1;
  }
  (void)printf("[INFO] RegisterMem success\n");

  // 4. 与server进行内存传输
  if (Transfer(client_handle, static_cast<uint8_t *>(mem.addr), args.transfer_op) != 0) {
    ClientFinalize(client_handle, {mem_handle});
    return -1;
  }

  // 5. 解注册，释放内存，析构
  ClientFinalize(client_handle, {mem_handle});

  // 通过TCP通知Server侧已传输完成
  (void)tcp_server.SendTaskStatus();
  tcp_server.DisConnectClient();
  tcp_server.StopServer();
  (void)printf("[INFO] Client Sample end\n");
  return 0;
}

int32_t RunServer(const Args &args) {
  (void)printf("[INFO] server start\n");
  // 1. 初始化
  EndPointDesc ep;
  if (InitEndPointInfo(args.local_comm_res, ep) != 0) {
    (void)printf("[ERROR] Initialize EndPoint list failed\n");
    return -1;
  }

  HixlServerHandle server_handle = nullptr;
  std::string ip = args.local_engine.substr(0, args.local_engine.find(':'));
  int32_t port = std::stoi(args.local_engine.substr(args.local_engine.find(':') + 1));
  HixlServerConfig config{};
  auto ret = HixlCSServerCreate(ip.c_str(), static_cast<uint32_t>(port), &ep, 1U, &config, &server_handle);
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
  HcclMem mem{};
  bool is_host = (args.transfer_mode == "d2h" || args.transfer_mode == "h2h");
  aclError acl_ret = ACL_ERROR_NONE;
  if (is_host) {
    acl_ret = aclrtMallocHost(&mem.addr, kTransferMemSize);
    mem.type = HCCL_MEM_TYPE_HOST;
  } else {
    acl_ret = aclrtMalloc(&mem.addr, kTransferMemSize, ACL_MEM_MALLOC_HUGE_ONLY);
    mem.type = HCCL_MEM_TYPE_DEVICE;
  }
  if (acl_ret != ACL_ERROR_NONE) {
    (void)printf("[ERROR] aclrtMalloc failed, ret = %d\n", acl_ret);
    ServerFinalize(server_handle, {mem_handle});
    return -1;
  }

  ret = HixlCSServerRegMem(server_handle, kServerMemTagName, &mem, &mem_handle);
  if (ret != HIXL_SUCCESS) {
    (void)printf("[ERROR] HixlCSServerRegMem failed, ret = %u\n", ret);
    ServerFinalize(server_handle, {mem_handle});
    return -1;
  }
  (void)printf("[INFO] RegisterMem success\n");

  // 4. 等待client transfer
  TCPClient tcp_client;
  if (!tcp_client.ConnectToServer(args.remote_engine, args.tcp_port)) {
    return -1;
  }
  (void)printf("[INFO] Wait transfer begin\n");
  if (tcp_client.ReceiveTaskStatus()) {
    (void)printf("[INFO] Wait transfer end\n");
  }
  tcp_client.Disconnect();

  // 5. 解注册，释放内存，析构
  ServerFinalize(server_handle, {mem_handle});
  (void)printf("[INFO] Server Sample end\n");
  return 0;
}
}  // namespace

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
        args.transfer_mode.c_str(), args.transfer_op.c_str(), args.local_comm_res.c_str(),
        args.remote_comm_res.c_str());
  } else {
    (void)printf(
        "[ERROR] Expect 8 args(device_id, local_engine, remote_engine, tcp_port, transfer_mode, "
        "transfer_op, local_comm_res, remote_comm_res), but got %d\n",
        argc - 1);
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