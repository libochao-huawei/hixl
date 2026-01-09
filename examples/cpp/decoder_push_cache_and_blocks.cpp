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
#include <thread>
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <poll.h>

#include "acl/acl.h"
#include "llm_datadist/llm_datadist.h"

using namespace llm_datadist;
namespace {
constexpr uint16_t kSocketListenPort = 16001;
constexpr uint16_t kDecoderListenPort = 26001;
constexpr uint16_t kDecoderClusterId = 1;
constexpr uint32_t kNumTensors = 4U;
constexpr size_t kTensorSize = 8 * 16 * sizeof(int32_t);
const std::vector<int64_t> kTensorShape = {8, 16};
constexpr size_t kTensorBlockElementNum = 16;
constexpr int32_t kWaitPromptTime = 10;
constexpr int32_t kExpectedArgCnt = 3;
constexpr uint32_t kArgIndexDeviceId = 1;
constexpr uint32_t kArgIndexLocalIp = 2;

#define CHECK_ACL(x)                                                                  \
  do {                                                                                \
    aclError __ret = x;                                                               \
    if (__ret != ACL_ERROR_NONE) {                                                    \
      std::cerr << __FILE__ << ":" << __LINE__ << " aclError:" << __ret << std::endl; \
    }                                                                                 \
  } while (0)
}  // namespace

int Initialize(LlmDataDist &llm_datadist, const std::string &device_id, const std::string &local_ip) {
  std::map<AscendString, AscendString> options;
  options[OPTION_DEVICE_ID] = device_id.c_str();
  options[OPTION_LISTEN_IP_INFO] = (std::string(local_ip) + ":" + std::to_string(kDecoderListenPort)).c_str();
  auto ret = llm_datadist.Initialize(options);
  if (ret != LLM_SUCCESS) {
    printf("[ERROR] Initialize failed, ret = %u\n", ret);
    return -1;
  }
  printf("[INFO] Initialize success\n");
  return LLM_SUCCESS;
}

int32_t CheckBuffers(const std::vector<void *> &buffers, const std::vector<uint32_t> &check_index_list) {
  for (auto buffer : buffers) {
    std::vector<int32_t> host_buffer(kTensorSize / sizeof(int32_t));
    CHECK_ACL(aclrtMemcpy(&host_buffer[0], kTensorSize, buffer, kTensorSize, ACL_MEMCPY_DEVICE_TO_HOST));
    std::cout << "[INFO] Transfered data: [";
    for (const auto &data : host_buffer) {
      std::cout << data << " ";
    }
    std::cout << "]" << std::endl;
    for (auto check_index : check_index_list) {
      for (size_t i = 0U; i < kTensorBlockElementNum; ++i) {
        auto expect = check_index * kTensorBlockElementNum + i;
        if (static_cast<uint32_t>(host_buffer[expect]) != expect) {
          printf("[ERROR] Buffer check failed, index = %zu, val = %d, expect val = %zu\n", expect, host_buffer[expect],
                 expect);
          return -1;
        }
      }
    }
  }
  printf("[INFO] CheckBuffers success\n");
  return 0;
}

int32_t StartServer() {
  // 创建Socket
  int32_t server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd == -1) {
    std::cerr << "[ERROR] Create socket failed" << std::endl;
    return -1;
  }

  // 设置socket选项
  int opt = 1;
  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
    std::cerr << "[ERROR] Set socket option failed" << std::endl;
    close(server_fd);
    return false;
  }

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_port = htons(kSocketListenPort);

  // 绑定端口
  if (bind(server_fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) < 0) {
    std::cerr << "[ERROR] Bind port failed" << '[' << kSocketListenPort << ']' << std::endl;
    close(server_fd);
    return -1;
  }

  // 监听端口
  if (listen(server_fd, 3) < 0) {
    std::cerr << "[ERROR] Listen port failed" << '[' << kSocketListenPort << ']' << std::endl;
    close(server_fd);
    return -1;
  }
  std::cout << "[INFO] Socket server is listening port: " << kSocketListenPort << "..." << std::endl;
  return server_fd;
}

int32_t ReceiveDataBySocket() {
  int32_t server_fd = StartServer();
  if (server_fd < 0) { return -1;}

  // 建立连接
  int timeout_ms= 7500;
  struct pollfd pfd;
  pfd.fd = server_fd;
  pfd.events = POLLIN;

  auto ret = poll(&pfd, static_cast<nfds_t>(1), timeout_ms);
  if (ret <= 0) {
    std::cerr << "[ERROR] Poll error or Accept connection timeout!" << std::endl;
    close(server_fd);
    return -1;
  } 

  sockaddr_in client_address{};
  socklen_t client_addr_len = sizeof(client_address);
  int32_t client_socket = accept(server_fd, reinterpret_cast<sockaddr *>(&client_address), &client_addr_len);
  if (client_socket < 0) {
    std::cerr << "[ERROR] Accept connection failed" << std::endl;
    close(server_fd);
    return -1;
  }
  std::cout << "[INFO] Tcp server accept connection success" << std::endl;

  // 接受数据
  int32_t buffer_data = 0;
  std::vector<int32_t> received_data;
  for (uint32_t i = 0; i < kTensorSize / sizeof(int32_t); ++i) {
    ssize_t bytes_received = recv(client_socket, &buffer_data, sizeof(int32_t), 0);
    if (bytes_received <= 0) {
      std::cerr << "[ERROR] Received data failed" << std::endl;
      close(client_socket);
      close(server_fd);
      return -1;
    } else if (bytes_received != sizeof(int32_t)) {
      std::cerr << "[ERROR] Received data size mismatch, expected: " << sizeof(int32_t)
                << "Bytes, actual received: " << bytes_received << "Bytes" << std::endl;
      close(client_socket);
      close(server_fd);
      return -1;
    }
    // 网络字节序转换为主机字节序
    received_data.emplace_back(be32toh(buffer_data));
  }
  std::cout << "[INFO] Received data: [";
  for (const auto &data : received_data) { std::cout << data << " "; }
  std::cout << "]" << std::endl;
  close(client_socket);
  close(server_fd);
  return 0;
}

void Finalize(LlmDataDist &llm_datadist, int64_t cache_id, const std::vector<void *> buffers) {
  if (cache_id > 0) {
    auto ret = llm_datadist.UnregisterKvCache(cache_id);
    if (ret != 0) {
      printf("[ERROR] UnregisterKvCache failed, ret = %u\n", ret);
    } else {
      printf("[INFO] UnregisterKvCache success\n");
    }
  }
  for (auto buffer : buffers) {
    aclrtFree(buffer);
  }
  llm_datadist.Finalize();
}

int32_t RunDecoderSample(const char *device_id, const char *local_ip) {
  printf("[INFO] Decoder Sample start\n");
  // 1. 初始化
  LlmDataDist llm_datadist(kDecoderClusterId, LlmRole::kDecoder);
  if (Initialize(llm_datadist, device_id, local_ip) != 0) {
    return -1;
  }

  // 2. 注册内存地址
  CacheDesc cache_desc{};
  cache_desc.num_tensors = kNumTensors;
  cache_desc.data_type = DT_INT32;
  cache_desc.shape = kTensorShape;
  std::vector<uint64_t> tensor_addrs;
  std::vector<void *> buffers;
  for (uint32_t i = 0U; i < kNumTensors; ++i) {
    int32_t *buffer = nullptr;
    CHECK_ACL(aclrtMalloc((void **)&buffer, kTensorSize, ACL_MEM_MALLOC_HUGE_ONLY));
    tensor_addrs.emplace_back(reinterpret_cast<uint64_t>(buffer));
    buffers.emplace_back(reinterpret_cast<void *>(buffer));
  }
  int64_t cache_id = -1;
  auto ret = llm_datadist.RegisterKvCache(cache_desc, tensor_addrs, {}, cache_id);
  if (ret != LLM_SUCCESS) {
    printf("[ERROR] RegisterKvCache failed, ret = %u\n", ret);
    Finalize(llm_datadist, cache_id, buffers);
    return -1;
  }
  // 3. RegisterKvCache成功后，可以获取cache中各tensor的地址用于后续操作
  printf("[INFO] RegisterKvCache success\n");
  for (size_t i = 0U; i < tensor_addrs.size(); ++i) {
    printf("[INFO] Tensor[%zu] addr = %p\n", i, reinterpret_cast<void *>(tensor_addrs[i]));
  }

  // 通过socket接受buffer数据用于后续验证push结果正确性
  if (ReceiveDataBySocket() != 0) {
    Finalize(llm_datadist, cache_id, buffers);
    return -1;
  }
  std::cout << "[INFO] ReceiveDataBySocket success" << std::endl;

  // 4. 等待prompt写完cache，实际业务场景可通过合适方式实现通知
  std::this_thread::sleep_for(std::chrono::seconds(kWaitPromptTime));
  if (CheckBuffers(buffers, {4, 5, 6, 7}) != 0) {
    Finalize(llm_datadist, cache_id, buffers);
    return -1;
  }

  // 5. 释放cache与llmDataDist
  Finalize(llm_datadist, cache_id, buffers);
  printf("[INFO] Decoder Sample end\n");
  return 0;
}

int main(int32_t argc, char **argv) {
  if (argc != kExpectedArgCnt) {
    printf("[ERROR] expect 3 args(device_id, localHostIp), but got %d\n", argc - 1);
    return -1;
  }
  const auto device_id = argv[kArgIndexDeviceId];
  const auto local_ip = argv[kArgIndexLocalIp];
  printf("[INFO] device_id = %s, local_ip = %s\n", device_id, local_ip);
  auto ret = RunDecoderSample(device_id, local_ip);
  return ret;
}