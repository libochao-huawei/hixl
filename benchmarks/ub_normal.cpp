/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING
 * BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE. See LICENSE in the root of
 * the software repository for the full text of the License.
 */

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <map>
#include <string>
#include <thread>
#include <vector>
#include "common/tcp_client_server.h"
#include "acl/acl.h"
#include "hixl/hixl.h"
// #include "common/ub_common_config.h"

using namespace hixl;
namespace {
constexpr int32_t kExpectedArgCnt = 7;
constexpr uint32_t kArgIndexDeviceId = 1;
constexpr uint32_t kArgIndexLocalEngine = 2;
constexpr uint32_t kArgIndexRemoteEngine = 3;
constexpr uint32_t kArgIndexTcpPort = 4;
constexpr uint32_t kArgIndexTransferMode = 5;
constexpr uint32_t kArgIndexTransferOp = 6;
constexpr uint64_t kTransferMemSize = 1048576 * 128u;  // (已修改为64M)128M
constexpr int32_t kPortMaxValue = 65535;
constexpr int32_t kConnectTimeoutMs = 1000 * 60;
constexpr int32_t kDisconnectTimeoutMs = 1000;
constexpr int32_t kHeartbeatCleanupWaitSec = 35;

#define CHECK_ACL_RETURN(x)                                                           \
  do {                                                                                \
    aclError __ret = x;                                                               \
    if (__ret != ACL_ERROR_NONE) {                                                    \
      std::cerr << __FILE__ << ":" << __LINE__ << " aclError:" << __ret << std::endl; \
      return __ret;                                                                   \
    }                                                                                 \
  } while (0)
}  // namespace

int32_t Initialize(Hixl &hixl_engine, const char *local_engine, bool is_client) {
  std::map<AscendString, AscendString> options;
  if (is_client) {
    options[hixl::OPTION_AUTO_CONNECT] = "1";
    const char *env_ret = getenv("ClientOptionCommRes");
    if (env_ret != nullptr) {
      options[hixl::OPTION_LOCAL_COMM_RES] = env_ret;
    }
  } else {
    const char *env_ret = getenv("ServerOptionCommRes");
    if (env_ret != nullptr) {
      options[hixl::OPTION_LOCAL_COMM_RES] = env_ret;
    }
  }
  auto ret = hixl_engine.Initialize(local_engine, options);
  if (ret != SUCCESS) {
    printf("[ERROR] Initialize failed, ret = %u\n", ret);
    return -1;
  }
  printf("[INFO] Initialize success\n");
  return 0;
}

int32_t Connect(Hixl &hixl_engine, const char *remote_engine) {
  auto ret = hixl_engine.Connect(remote_engine, kConnectTimeoutMs);
  if (ret != SUCCESS) {
    printf("[ERROR] Connect failed, ret = %u\n", ret);
    return -1;
  }
  printf("[INFO] Connect success\n");
  return 0;
}

Status Disconnect(Hixl &hixl_engine, const char *remote_engine, bool connected) {
  if (!connected) {
    return NOT_CONNECTED;
  }
  auto ret = hixl_engine.Disconnect(remote_engine, kDisconnectTimeoutMs);
  if (ret != SUCCESS) {
    printf("[INFO] Disconnect failed as expected after heartbeat cleanup, ret = %u\n", ret);
  } else {
    printf("[ERROR] Disconnect success, heartbeat cleanup did not destroy the link\n");
  }
  return ret;
}

void Finalize(Hixl &hixl_engine, bool is_host, const std::vector<MemHandle> &handles,
              const std::vector<void *> &buffers = {}) {
  for (const auto &handle : handles) {
    auto ret = hixl_engine.DeregisterMem(handle);
    if (ret != 0) {
      printf("[ERROR] DeregisterMem failed, ret = %u\n", ret);
    } else {
      printf("[INFO] DeregisterMem success\n");
    }
  }
  if (is_host) {
    for (const auto &buffer : buffers) {
      aclrtFreeHost(buffer);
    }
  } else {
    for (const auto &buffer : buffers) {
      aclrtFree(buffer);
    }
  }
  hixl_engine.Finalize();
}

int32_t RunClient(const char *local_engine, const char *remote_engine, uint16_t tcp_port,
                  const std::string &transfer_mode, TransferOp transfer_op) {
  (void)transfer_op;
  printf("[INFO] client start\n");

  // 通过TCP接收Server侧的内存地址
  TCPServer tcp_server;
  uint64_t remote_addr = 0;
  if (!tcp_server.StartServer(tcp_port)) {
    printf("[ERROR] Failed to start TCP server.\n");
    return -1;
  }
  printf("[INFO] TCP server started.\n");
  if (!tcp_server.AcceptConnection()) {
    return -1;
  }
  remote_addr = tcp_server.ReceiveUint64();
  if (remote_addr != 0) {
    printf("[INFO] Success to receive server mem addr: 0x%lx\n", remote_addr);
  }

  // 1. 初始化
  Hixl hixl_engine;
  if (Initialize(hixl_engine, local_engine, true) != 0) {
    printf("[ERROR] Initialize Hixl failed\n");
    return -1;
  }

  // 2. 注册内存地址
  int32_t *src = nullptr;
  void *tmp = nullptr;
  MemHandle handle = nullptr;
  bool connected = false;
  bool is_host = (transfer_mode == "h2d" || transfer_mode == "h2h");
  if (is_host) {
    CHECK_ACL_RETURN(aclrtMallocHost(&tmp, kTransferMemSize));
  } else {
    CHECK_ACL_RETURN(aclrtMalloc(&tmp, kTransferMemSize, ACL_MEM_MALLOC_HUGE_ONLY));
  }
  src = static_cast<int32_t *>(tmp);

  MemDesc desc{};
  desc.addr = reinterpret_cast<uintptr_t>(src);
  desc.len = kTransferMemSize;
  auto ret = hixl_engine.RegisterMem(desc, is_host ? MemType::MEM_HOST : MEM_DEVICE, handle);
  if (ret != SUCCESS) {
    printf("[ERROR] RegisterMem failed, ret = %u\n", ret);
    Finalize(hixl_engine, is_host, {handle}, {src});
    return -1;
  }
  printf("[INFO] RegisterMem success\n");

  // 等待server注册完成
  if (tcp_server.ReceiveTaskStatus()) {
    printf("[INFO] Server RegisterMem success\n");
  }

  // 3. 与server建链
  if (Connect(hixl_engine, remote_engine) != 0) {
    Finalize(hixl_engine, is_host, {handle}, {src});
    return -1;
  }
  connected = true;

  // 通知Server侧已建链，Server随后直接退出以模拟异常挂掉。
  (void)tcp_server.SendTaskStatus();
  tcp_server.DisConnectClient();
  tcp_server.StopServer();

  printf("[INFO] Wait %d seconds for heartbeat failure cleanup\n", kHeartbeatCleanupWaitSec);
  std::this_thread::sleep_for(std::chrono::seconds(kHeartbeatCleanupWaitSec));

  auto disconnect_ret = Disconnect(hixl_engine, remote_engine, connected);
  const bool heartbeat_cleanup_ok = (disconnect_ret != SUCCESS);
  connected = false;

  // 5. 解注册，释放内存，析构
  Finalize(hixl_engine, is_host, {handle}, {src});
  if (heartbeat_cleanup_ok) {
    printf("[PASS] Heartbeat failure destroyed the client link, Disconnect ret = %u\n", disconnect_ret);
    return 0;
  }
  printf("[FAIL] Heartbeat cleanup was not observed, Disconnect ret = %u\n", disconnect_ret);
  return -1;
}

int32_t RunServer(const char *local_engine, const char *remote_engine, uint16_t tcp_port,
                  const std::string &transfer_mode) {
  printf("[INFO] server start\n");
  // 1. 初始化
  Hixl hixl_engine;
  if (Initialize(hixl_engine, local_engine, false) != 0) {
    printf("[ERROR] Initialize Hixl failed\n");
    return -1;
  }
  // 2. 注册内存地址
  void *buffer = nullptr;
  bool is_host = (transfer_mode == "d2h" || transfer_mode == "h2h");
  if (is_host) {
    CHECK_ACL_RETURN(aclrtMallocHost(&buffer, kTransferMemSize));
  } else {
    CHECK_ACL_RETURN(aclrtMalloc(&buffer, kTransferMemSize, ACL_MEM_MALLOC_HUGE_ONLY));
  }
  auto addr = reinterpret_cast<uintptr_t>(buffer);

  // 通过TCP传输内存地址到Client侧
  TCPClient tcp_client;
  if (!tcp_client.ConnectToServer(remote_engine, tcp_port)) {
    return -1;
  }
  (void)tcp_client.SendUint64(addr);

  MemHandle handle = nullptr;
  auto mem_type = is_host ? MemType::MEM_HOST : MemType::MEM_DEVICE;

  MemDesc desc{};
  desc.addr = addr;
  desc.len = kTransferMemSize;
  auto ret = hixl_engine.RegisterMem(desc, mem_type, handle);
  if (ret != SUCCESS) {
    printf("[ERROR] RegisterMem failed, ret = %u\n", ret);
    Finalize(hixl_engine, is_host, {handle}, {buffer});
    return -1;
  }
  // 3. RegisterMem成功后，将地址保存到本地文件中等待client读取
  printf("[INFO] RegisterMem success, addr:%p\n", buffer);

  // 通过TCP通知Client侧内存已注册
  (void)tcp_client.SendTaskStatus();

  // 4. 等待client建链完成，然后直接退出，模拟server异常挂掉。
  printf("[INFO] Wait client connect done\n");
  if (tcp_client.ReceiveTaskStatus()) {
    printf("[INFO] Client connect done, server exits without Hixl Finalize now\n");
    fflush(stdout);
    fflush(stderr);
    std::_Exit(0);
  }
  tcp_client.Disconnect();

  // 5. 解注册，释放内存，析构
  Finalize(hixl_engine, is_host, {handle}, {buffer});
  printf("[INFO] Server Sample end\n");
  return 0;
}

int32_t main(int32_t argc, char **argv) {
  bool is_client = false;
  std::string device_id;
  std::string local_engine;
  std::string remote_engine;
  std::string tcp_port_str;
  std::string transfer_mode;
  std::string transfer_op_str;
  if (argc == kExpectedArgCnt) {
    device_id = argv[kArgIndexDeviceId];
    local_engine = argv[kArgIndexLocalEngine];
    remote_engine = argv[kArgIndexRemoteEngine];
    tcp_port_str = argv[kArgIndexTcpPort];
    transfer_mode = argv[kArgIndexTransferMode];
    transfer_op_str = argv[kArgIndexTransferOp];
    is_client = (remote_engine.find(':') != std::string::npos);
    printf(
        "[INFO] device_id = %s, local_engine = %s, remote_engine = %s, tcp_port = %s, transfer_mode = %s, transfer_op "
        "= %s\n",
        device_id.c_str(), local_engine.c_str(), remote_engine.c_str(), tcp_port_str.c_str(), transfer_mode.c_str(),
        transfer_op_str.c_str());
  } else {
    printf(
        "[ERROR] Expect 6 args(device_id, local_engine, remote_engine, tcp_port, transfer_mode, transfer_op), but got "
        "%d\n",
        argc - 1);
    return -1;
  }
  int32_t device = std::stoi(device_id);
  int32_t input_tcp_port = std::stoi(tcp_port_str);
  if (input_tcp_port < 0 || input_tcp_port > kPortMaxValue) {
    printf("[ERROR] Invalid port: %d, should be in 0~65535\n", input_tcp_port);
    return -1;
  }
  auto tcp_port = static_cast<uint16_t>(input_tcp_port);
  CHECK_ACL_RETURN(aclrtSetDevice(device));

  if (transfer_mode != "d2d" && transfer_mode != "h2d" && transfer_mode != "d2h" && transfer_mode != "h2h") {
    printf("[ERROR] Invalid value for transfer_mode: %s\n", transfer_mode.c_str());
    return -1;
  }

  if (transfer_op_str != "write" && transfer_op_str != "read") {
    printf("[ERROR] Invalid value for transfer_op: %s\n", transfer_op_str.c_str());
    return -1;
  }
  TransferOp transfer_op = (transfer_op_str == "read") ? TransferOp::READ : TransferOp::WRITE;

  int32_t ret = 0;
  if (is_client) {
    ret = RunClient(local_engine.c_str(), remote_engine.c_str(), tcp_port, transfer_mode, transfer_op);
  } else {
    ret = RunServer(local_engine.c_str(), remote_engine.c_str(), tcp_port, transfer_mode);
  }
  CHECK_ACL_RETURN(aclrtResetDevice(device));
  return ret;
}
