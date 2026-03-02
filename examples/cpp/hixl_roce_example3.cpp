/**
* This program is free software, you can redistribute it and/or modify it.
* Copyright (c) 2025 Huawei Technologies Co., Ltd.
* This file is a part of the CANN Open Software.
* Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
* Please refer to the License for details. You may not use this file except in compliance with the License.
* THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
* See LICENSE in the root of the software repository for the full text of the License.
*/

// 正常场景4：一个client对一个server建链，注册并传输大块数据，比如1G/10G/100G，关注注册与传输耗时

#include <cstdio>
#include <cstdlib>
#include <thread>
#include <iostream>
#include <vector>
#include "nlohmann/json.hpp"
#include "common/tcp_client_server.h"
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
constexpr uint32_t kArgIndexMultiNum = 7;
constexpr uint32_t kTransferMemSize = 1073741824;  // 1G
constexpr uint32_t kBaseBlockSize = 134217728;     // 128M
constexpr int32_t kPortMaxValue = 65535;

constexpr const char kServerJsonFilePath[] = "../../../examples/cpp/local_comm_res_server.json";
constexpr const char kClientJsonFilePath[] = "../../../examples/cpp/local_comm_res_client.json";
constexpr const char kMapKey[] = "adxl.LocalCommRes";

#define CHECK_ACL_RETURN(x)                                                                  \
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

int32_t Transfer(Hixl &hixl_engine, int32_t &src, const char *remote_engine, uint64_t dst_addr, 
                 std::string op_type, int32_t multi) {
  TransferOp type = op_type == "read" ? READ : WRITE;
  auto block_size = kBaseBlockSize;
  uint64_t data_size = (uint64_t)kTransferMemSize * (uint64_t)multi;
  auto trans_num = uint64_t(data_size) / uint64_t(block_size);
  std::vector<TransferOpDesc> descs;
  descs.reserve(trans_num);
  for (uint32_t j = 0; j < trans_num; j++) {
    TransferOpDesc desc{};
    desc.local_addr = (reinterpret_cast<uintptr_t>(&src) + j * block_size);
    desc.remote_addr = (reinterpret_cast<uintptr_t>(dst_addr) + j * block_size);
    desc.len = block_size;
    descs.emplace_back(desc);
  }
  const auto start = std::chrono::steady_clock::now();
  auto ret = hixl_engine.TransferSync(remote_engine, type, descs, 1000 * kWaitTransTime);
  if (ret != SUCCESS) {
    printf("[ERROR] TransferSync failed, ret = %u\n", ret);
    return -1;
  }
  auto time_cost =
      std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
  double time_second = static_cast<double>(time_cost) / 1000 / 1000;
  double throughput = static_cast<double>(data_size) / 1024 / 1024 / 1024 / time_second;
  printf(
      "[INFO] Transfer success, block size: %u Bytes, transfer num: %lu, time cost: %ld us, throughput: %.3lf GB/s\n",
      block_size, trans_num, time_cost, throughput);
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

int32_t RunClient(const char *local_engine, const char *remote_engine, uint16_t tcp_port, const std::string &transfer_mode, 
                  std::string op_type, bool is_client, int32_t multi) {
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
  bool is_host = (transfer_mode == "h2d" || transfer_mode == "h2h");
  uint64_t data_size = (uint64_t)kTransferMemSize * (uint64_t)multi;
  if (is_host) {
    tmp = (void*)malloc(data_size);
  } else {
    CHECK_ACL_RETURN(aclrtMalloc(&tmp, data_size, ACL_MEM_MALLOC_HUGE_ONLY));
  }
  src = static_cast<int32_t*>(tmp);

  MemDesc desc{};
  desc.addr = reinterpret_cast<uintptr_t>(src);
  desc.len = data_size;
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

  // 4. 与server进行内存传输
  if (Transfer(hixl_engine, *src, remote_engine, remote_addr, op_type, multi) != 0) {
    Disconnect(hixl_engine, remote_engine);
    Finalize(hixl_engine, is_host, {handle}, {src});
    return -1;
  }

  // 断链
  Disconnect(hixl_engine, remote_engine);
  // 通过TCP通知Server侧已传输完成
  (void)tcp_server.SendTaskStatus();
  tcp_server.DisConnectClient();
  tcp_server.StopServer();

  // 5. 解注册，释放内存，析构
  Finalize(hixl_engine, is_host, {handle}, {src});
  printf("[INFO] Client Sample end\n");
  return 0;
}

int32_t RunServer(const char *local_engine, const char *remote_engine, uint16_t tcp_port, const std::string &transfer_mode, 
                  bool is_client, int32_t multi) {
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
  uint64_t data_size = (uint64_t)kTransferMemSize * (uint64_t)multi;
  if (is_host){
    buffer = (void*)malloc(data_size);
  } else{
    CHECK_ACL_RETURN(aclrtMalloc(&buffer, data_size, ACL_MEM_MALLOC_HUGE_ONLY));
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
  desc.len = data_size;
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

int32_t main(int32_t argc, char **argv) {
  bool is_client = false;
  bool use_buffer_pool = false;
  std::string device_id;
  std::string local_engine;
  std::string remote_engine;
  std::string tcp_port_str;
  std::string transfer_mode;
  std::string op_type;
  std::string multi_str;
  if (argc == kExpectedArgCnt) {
    device_id = argv[kArgIndexDeviceId];
    local_engine = argv[kArgIndexLocalEngine];
    remote_engine = argv[kArgIndexRemoteEngine];
    tcp_port_str = argv[kArgIndexTcpPort];
    transfer_mode = argv[kArgIndexTransferMode];
    op_type = argv[kArgIndexTransferOp];
    multi_str = argv[kArgIndexMultiNum];
    is_client = (local_engine.find(':') == std::string::npos);
    printf("[INFO] device_id = %s, local_engine = %s, remote_engine = %s, tcp_port = %s, "
           "transfer_mode = %s, transfer_op = %s, data_size = %s GB\n", 
            device_id.c_str(), local_engine.c_str(), remote_engine.c_str(), tcp_port_str.c_str(), 
            transfer_mode.c_str(), op_type.c_str(), multi_str.c_str());
  } else {
    printf("[ERROR] Expect 7 args(device_id, local_engine, remote_engine, tcp_port, transfer_mode, transfer_op, multi), but got %d\n", argc - 1);
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

  if (transfer_mode != "d2d" && transfer_mode != "h2d" && transfer_mode != "d2h" && transfer_mode != "h2h"){
    printf("[ERROR] Invalid value for transfer_mode: %s\n", transfer_mode.c_str());
    return -1;
  }

  if (op_type != "write" && op_type != "read") {
    printf("[ERROR] Invalid value for transfer_op: %s\n", op_type.c_str());
    return -1;
  }

  int32_t ret = 0;
  int32_t multi = std::stoi(multi_str);
  if (is_client) {
    ret = RunClient(local_engine.c_str(), remote_engine.c_str(), tcp_port, transfer_mode, op_type, is_client, multi);
  } else {
    ret = RunServer(local_engine.c_str(), remote_engine.c_str(), tcp_port, transfer_mode, is_client, multi);
  }
  CHECK_ACL_RETURN(aclrtResetDevice(device));
  return ret;
}