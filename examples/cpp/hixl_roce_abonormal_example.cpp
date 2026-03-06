/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <numeric>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <set>
#include <iostream>
#include <string>
#include <fstream>
#include <unistd.h> 
#include <sys/wait.h>
#include <sys/types.h>
#include <signal.h>
#include "common/tcp_client_server.h"
#include "nlohmann/json.hpp"
#include "acl/acl.h"
#include "hixl/hixl.h"

using namespace hixl;
namespace {
constexpr int32_t kWaitTransTime = 20;
constexpr int32_t kExpectedArgCnt = 8;
constexpr uint32_t kArgIndexDeviceId = 1;
constexpr uint32_t kArgIndexLocalEngine = 2;
constexpr uint32_t kArgIndexRemoteEngine = 3;
constexpr uint32_t kArgIndexTcpPort = 4;
constexpr uint32_t kArgIndexTransferMode = 5;
constexpr uint32_t kArgIndexTransferOp = 6;
constexpr uint32_t kArgIndexAbnormalIndex = 7;
constexpr uint32_t kBaseBlockSize = 262144; // 0.25M
constexpr uint32_t kTransferMemSize = 134217728; // 128M
constexpr int32_t kSrcValue = 2;
constexpr int32_t kPortMaxValue = 65535;

const std::set<uint32_t> kAbnormalIndices = {1, 2, 3, 4, 5, 6, 7, 8};

// 多线程延迟时间
constexpr int32_t kThreadDelayMs = 5;

// 进程销毁延迟时间（ms）
constexpr int32_t kKillDelayMs = 5;

constexpr const char kServerJsonFilePath[] = "../../../examples/cpp/local_comm_res_server.json";
constexpr const char kClientJsonFilePath[] = "../../../examples/cpp/local_comm_res_client.json";
constexpr const char kMapKey[] = "adxl.LocalCommRes";

#define CHECK_ACL_RETURN(x)                                                           \
  do {                                                                                \
    aclError __ret = x;                                                               \
    if (__ret != ACL_ERROR_NONE) {                                                    \
      std::cerr << __FILE__ << ":" << __LINE__ << " aclError:" << __ret << std::endl; \
      return __ret;                                                                   \
    }                                                                                 \
  } while (0)
}  // namespace

using StringMap = std::map<AscendString, AscendString>;
using json = nlohmann::json;

void FillMapWithJsonFileContent(StringMap &target_map, const std::string &json_file_path, const std::string &map_key,
                                bool validate_json_format) {
  // 1. 打开JSON文件（只读、二进制模式避免换行符转换）
  std::ifstream json_file(json_file_path, std::ios::in | std::ios::binary);
  if (!json_file.is_open()) {
    std::cerr << "[ERROR] 无法打开JSON文件：" << json_file_path << "，原因：文件不存在或权限不足" << std::endl;
  }

  try {
    // 2. 读取文件全部内容到字符串（保留原始格式）
    std::ostringstream oss;
    oss << json_file.rdbuf();
    std::string json_raw_content = oss.str();
    json_file.close();  // 及时关闭文件

    // 3. 校验内容非空
    if (json_raw_content.empty()) {
      std::cerr << "[ERROR] JSON文件内容为空：" << json_file_path << std::endl;
    }

    // 4. 可选：校验JSON格式合法性（避免填充非法JSON）
    if (validate_json_format) {
      try {
        auto j = json::parse(json_raw_content); // 解析失败会抛异常
      } catch (const json::parse_error &e) {
        std::cerr << "[ERROR] JSON文件格式非法：" << json_file_path << "，错误位置：" << e.byte << "，原因："
                  << e.what() << std::endl;
      }
    }

    // 5. 填充到map的指定字段（覆盖原有值）
    target_map[map_key.c_str()] = AscendString(json_raw_content.c_str());
    std::cout << "[INFO] 成功读取JSON文件并填充到map字段[" << map_key << "]，文件路径：" << json_file_path << std::endl;
  } catch (const std::exception &e) {
    // 捕获所有异常（文件读取/内存不足等）
    std::cerr << "[ERROR] 处理JSON文件时发生异常：" << json_file_path << "，原因：" << e.what() << std::endl;
    if (json_file.is_open()) {
      json_file.close();
    }
  }
}

int Initialize(Hixl &hixl_engine, const char *local_engine, bool is_client) {
  std::map<AscendString, AscendString> options;
  if (is_client) {
    FillMapWithJsonFileContent(options, kClientJsonFilePath, kMapKey, true);
  } else {
    FillMapWithJsonFileContent(options, kServerJsonFilePath, kMapKey, true);
  }
  // 在不需要使用中转buffer进行传输的场景下，关闭中转内存池
  options["BufferPool"] = "0:0";
  auto ret = hixl_engine.Initialize(local_engine, options);
  if (ret != SUCCESS) {
    printf("[ERROR] Initialize failed, ret = %u\n", ret);
    return -1;
  }
  printf("[INFO] Initialize success\n");
  return 0;
}

int Connect(Hixl &hixl_engine, const char *remote_engine) {
  auto ret = hixl_engine.Connect(remote_engine);
  if (ret != SUCCESS) {
    printf("[ERROR] Connect failed, ret = %u\n", ret);
    return -1;
  }
  printf("[INFO] Connect success\n");
  return 0;
}

int Disconnect(Hixl &hixl_engine, const char *remote_engine) {
  auto ret = hixl_engine.Disconnect(remote_engine);
  if (ret != SUCCESS) {
    printf("[ERROR] Disconnect failed, ret = %u\n", ret);
    return -1;
  }
  printf("[INFO] Disconnect success\n");
  return 0;
}

int32_t Transfer(Hixl &hixl_engine, int32_t &src, const char *remote_engine, uint64_t dst_addr, TransferOp transfer_op) {
  auto block_size = kBaseBlockSize;
  auto trans_num = kTransferMemSize / block_size;
  std::vector<TransferOpDesc> descs;
  descs.reserve(trans_num);
  for (uint32_t j = 0; j < trans_num; j++) {
    TransferOpDesc desc{};
    desc.local_addr = (reinterpret_cast<uintptr_t>(&src) + j * block_size);
    desc.remote_addr = (reinterpret_cast<uintptr_t>(dst_addr) + j * block_size);
    desc.len = block_size;
    descs.emplace_back(desc);
  }
  auto ret = hixl_engine.TransferSync(remote_engine, transfer_op, descs, 1000 * kWaitTransTime);
  if (ret != SUCCESS) {
    printf("[ERROR] TransferSync failed, ret = %u\n", ret);
    return -1;
  }
  return 0;
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
      free(buffer);
    }
  } else {
    for (const auto &buffer : buffers) {
      aclrtFree(buffer);
    }
  }
  hixl_engine.Finalize();
}

void FinalizeInThread(Hixl *hixl_engine) {
  std::this_thread::sleep_for(std::chrono::milliseconds(kThreadDelayMs));
  printf("[WARNING] Thread start: Call Finalize during Transfer\n");
  
  if (hixl_engine != nullptr) {
    hixl_engine->Finalize();
    printf("[WARNING] Thread Finalize hixl_engine success\n");
  }
}

void Disconnect(Hixl &hixl_engine, const char *remote_engine, bool connected) {
  if (connected) {
    auto ret = hixl_engine.Disconnect(remote_engine);
    if (ret != SUCCESS) {
      printf("[ERROR] Disconnect failed, ret = %u\n", ret);
    } else {
      printf("[INFO] Disconnect success\n");
    }
  }
}

int32_t RunClient(const char *local_engine, const char *remote_engine, uint16_t tcp_port, 
                  std::string &transfer_mode, TransferOp transfer_op, bool is_client, uint32_t abnormal_idx) {
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
  if (Initialize(hixl_engine, local_engine, is_client) != 0) {
    printf("[ERROR] Initialize Hixl failed\n");
    return -1;
  }
  // 2. 注册内存地址
  int32_t *src = nullptr;
  void *tmp = nullptr;
  MemHandle handle = nullptr;
  bool connected = false;
  MemType mem_type = (transfer_mode == "d2d" || transfer_mode == "d2h") ? MemType::MEM_DEVICE : MemType::MEM_HOST;
  bool is_host = mem_type == MemType::MEM_HOST ? true : false;
  if (is_host) {
    tmp = malloc(kTransferMemSize);
  } else {
    CHECK_ACL_RETURN(aclrtMalloc(&tmp, kTransferMemSize, ACL_MEM_MALLOC_HUGE_ONLY));
  }
  src = static_cast<int32_t*>(tmp);

  MemDesc desc{};
  // 异常场景(3): client地址未注册，执行传输
  if (abnormal_idx != 3) {
    desc.addr = reinterpret_cast<uintptr_t>(src);
    desc.len = kTransferMemSize;
    auto ret = hixl_engine.RegisterMem(desc, is_host ? MemType::MEM_HOST : MEM_DEVICE, handle);
    if (ret != SUCCESS) {
      printf("[ERROR] RegisterMem failed, ret = %u\n", ret);
      Finalize(hixl_engine, is_host, {handle}, {src});
      return -1;
    }
    printf("[INFO] RegisterMem success\n");
  }

  // 等待server注册完成
  if (tcp_server.ReceiveTaskStatus()) {
    printf("[INFO] Server RegisterMem success\n");
  }

  // 异常场景(1): client地址重复注册
  if (abnormal_idx == 1) {
    printf("[WARNING] Abnormal scenario 1 triggered, duplicate registration for client memory address, error expected here\n");
    (void)hixl_engine.RegisterMem(desc, mem_type, handle);
  }

  // 3. 与server建链
  if (Connect(hixl_engine, remote_engine) != 0) {
    Finalize(hixl_engine, is_host, {handle}, {src});
    return -1;
  }
  connected = true;

  // 异常场景(3): client地址未注册，执行传输
  if (abnormal_idx == 3) {
    printf("[WARNING] Abnormal scenario 3 triggered, transfer without registering client memory address, error expected here\n");
    (void)Transfer(hixl_engine, *src, remote_engine, remote_addr, transfer_op);
  }

  // 4. 从server get内存，并向server put内存
  auto transfer_ret = -1;
  // 异常场景7: 在传输过程中销毁client，关注server能否自动回收client的链路资源
  if (abnormal_idx == 7) {
    std::thread finalize_thread;
    printf("[WARNING] Error may occur here, the client is finalized\n");
    // 启动线程：在Transfer执行时调用Finalize
    finalize_thread = std::thread(FinalizeInThread, &hixl_engine);
    // 立即执行Transfer（此时线程会延迟后调用Finalize）
    transfer_ret = Transfer(hixl_engine, *src, remote_engine, remote_addr, transfer_op);
    // 等待线程结束
    if (finalize_thread.joinable()) {
      finalize_thread.join();
    }  
  } else if (abnormal_idx == 8) {
    // 异常场景8: 在传输过程中kill掉client的进程，关注server能否自动回收client的链路资源
    printf("[WARNING] Error may occur here, the process of client is killed\n");
    pid_t pid = fork();
    if (pid == -1) {
      printf("[ERROR] Fork failed, errno = %d\n", errno);
      Disconnect(hixl_engine, remote_engine, connected);
      Finalize(hixl_engine, is_host, {handle}, {src});
      return -1;
    }
    if (pid == 0) {
      // 子进程逻辑：延迟后杀死父进程
      usleep(kKillDelayMs * 1000); // 延迟100ms，确保Transfer已启动
      printf("[WARNING] Child process (pid=%d) kill parent process (pid=%d)\n", getpid(), getppid());
      
      // 方案1：强制终止（SIGKILL，无法捕获，最彻底）
      kill(getppid(), SIGKILL);
      
      // 方案2：优雅终止（SIGTERM，可捕获，用于自定义清理）
      // kill(getppid(), SIGTERM);
      
      exit(0); // 子进程退出
    } else {
      // 父进程逻辑：立即执行Transfer（执行中会被子进程杀死）
      printf("[INFO] Parent process start Transfer (will be killed in %dms)\n", kKillDelayMs);
      transfer_ret = Transfer(hixl_engine, *src, remote_engine, remote_addr, transfer_op);
      // 以下代码大概率不会执行（父进程已被杀死）
      printf("[INFO] Transfer finished (unexpected, process not killed)\n");
      waitpid(pid, nullptr, 0); // 等待子进程退出
    }
  } else {
    transfer_ret = Transfer(hixl_engine, *src, remote_engine, remote_addr, transfer_op);
    if (transfer_ret != 0) {
      Disconnect(hixl_engine, remote_engine, connected);
      Finalize(hixl_engine, is_host, {handle}, {src});
      return -1;
    }
  }


  // 异常场景6: 多次调用Trasfer接口，调用5k次，看是否能触发资源上限
  if (abnormal_idx == 6) {
    printf("[WARNING] Error may occur here, call TransferSync 5000 times\n");
    for (int32_t i = 0; i < 5000; i++) {
      Transfer(hixl_engine, *src, remote_engine, remote_addr, transfer_op);
    }
  }

  // 断链
  Disconnect(hixl_engine, remote_engine, connected);

  // 通过TCP通知Server侧已传输完成
  (void)tcp_server.SendTaskStatus();
  tcp_server.DisConnectClient();
  tcp_server.StopServer();

  // 5. 解注册，释放内存，析构
  Finalize(hixl_engine, is_host, {handle}, {src});
  printf("[INFO] Client Sample end\n");
  return 0;
}

int32_t RunServer(const char *local_engine, const char *remote_engine, uint16_t tcp_port, 
                  std::string &transfer_mode, bool is_client, uint32_t abnormal_idx) {
  printf("[INFO] server start\n");
  // 1. 初始化
  Hixl hixl_engine;
  if (Initialize(hixl_engine, local_engine, is_client) != 0) {
    printf("[ERROR] Initialize Hixl failed\n");
    return -1;
  }
  // 2. 注册内存地址
  void *buffer = nullptr;
  bool is_host = (transfer_mode == "d2h" || transfer_mode == "h2h");
  if (is_host) {
    buffer = malloc(kTransferMemSize);
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
  if (abnormal_idx != 4) {
    // 异常场景(4): server地址未注册，执行传输
    desc.addr = addr;
    desc.len = kTransferMemSize;
    auto ret = hixl_engine.RegisterMem(desc, mem_type, handle);
    if (ret != SUCCESS) {
      printf("[ERROR] RegisterMem failed, ret = %u\n", ret);
      Finalize(hixl_engine, is_host, {handle}, {buffer});
      return -1;
    }
    printf("[INFO] RegisterMem success, dst addr:%p\n", buffer);
  }

  // 异常场景(2): server地址重复注册
  if (abnormal_idx == 2) {
    printf("[WARNING] Abnormal scenario 2 triggered, duplicate registration for server memory address, error expected here\n");
    (void)hixl_engine.RegisterMem(desc, mem_type, handle);
  }

  // 3. RegisterMem成功后，通过TCP通知client侧内存已注册
  (void)tcp_client.SendTaskStatus();

  // 4. 等待client transfer
  printf("[INFO] Wait transfer begin\n");
  if (tcp_client.ReceiveTaskStatus()) {
    printf("[INFO] Wait transfer end\n");
  }
  tcp_client.Disconnect();

  // 5. 解注册，释放内存，析构
  Finalize(hixl_engine, is_host, {handle}, {buffer});
  printf("[INFO] Server Sample end\n");
  return 0;
}

int main(int32_t argc, char **argv) {
  bool is_client = false;
  std::string device_id;
  std::string local_engine;
  std::string remote_engine;
  std::string tcp_port_str;
  std::string transfer_mode;
  std::string transfer_op_str;
  std::string abnormal_index;
  
  if (argc != kExpectedArgCnt) {
    printf("[ERROR] client expect 7 args(device_id, local_engine, remote_engine tcp_port, transfer_mode, transfer_op,  "
           "abnormal_index), but got %d\n",
           argc - 1);
    return -1;
  }
  device_id = argv[kArgIndexDeviceId];
  local_engine = argv[kArgIndexLocalEngine];
  remote_engine = argv[kArgIndexRemoteEngine];
  tcp_port_str = argv[kArgIndexTcpPort];
  transfer_mode = argv[kArgIndexTransferMode];
  transfer_op_str = argv[kArgIndexTransferOp];
  abnormal_index = argv[kArgIndexAbnormalIndex];
  is_client = (local_engine.find(':') == std::string::npos);
  printf("[INFO] device_id = %s, local_engine = %s, remote_engine = %s\n", device_id.c_str(), local_engine.c_str(),
          remote_engine.c_str());
  
  int32_t device = std::stoi(device_id);
  CHECK_ACL_RETURN(aclrtSetDevice(device));

  int32_t input_tcp_port = std::stoi(tcp_port_str);
  if (input_tcp_port < 0 || input_tcp_port > kPortMaxValue) {
    printf("[ERROR] Invalid port: %d, should be in 0~65535\n", input_tcp_port);
    return -1;
  }
  auto tcp_port = static_cast<uint16_t>(input_tcp_port);

  if (transfer_mode != "d2d" && transfer_mode != "h2d" && transfer_mode != "d2h" && transfer_mode != "h2h"){
    printf("[ERROR] Invalid value for transfer_mode: %s\n", transfer_mode.c_str());
    return -1;
  }

  if (transfer_op_str != "write" && transfer_op_str != "read") {
    printf("[ERROR] Invalid value for transfer_op: %s\n", transfer_op_str.c_str());
    return -1;
  }
  TransferOp transfer_op = (transfer_op_str == "read") ? TransferOp::READ : TransferOp::WRITE;


  int32_t ab_idx = std::stoi(abnormal_index);
  if (kAbnormalIndices.find(ab_idx) == kAbnormalIndices.end()) {
    printf("[ERROR] Invalid value for abnormal index: %s\n", abnormal_index.c_str());
    return -1;
  }

  int32_t ret = 0;
  if (is_client) {
    ret = RunClient(local_engine.c_str(), remote_engine.c_str(), tcp_port, transfer_mode, transfer_op, is_client, ab_idx);
  } else {
    ret = RunServer(local_engine.c_str(), remote_engine.c_str(), tcp_port, transfer_mode, is_client, ab_idx);
  }
  CHECK_ACL_RETURN(aclrtResetDevice(device));
  return ret;
}