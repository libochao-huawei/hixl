/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <cstdio>
#include <iostream>
#include <netinet/in.h>
#include <numeric>
#include <string>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>
#include "acl/acl.h"
#include "llm_datadist/llm_datadist.h"

using namespace llm_datadist;
namespace {
constexpr uint16_t kPromptListenPort = 26000;
constexpr uint16_t kPromptControlPort = 26002;
constexpr uint16_t kPromptClusterId = 0;
constexpr uint32_t kNumTensors = 4U;
constexpr size_t kTensorSize = 8 * 16 * sizeof(int32_t);
const std::vector<int64_t> kTensorShape = {8, 16};
constexpr int32_t kControlTimeoutSec = 60;
constexpr int32_t kSocketBacklog = 2;
constexpr int32_t kExpectedArgCnt = 4;
constexpr uint32_t kArgIndexDeviceId = 1;
constexpr uint32_t kArgIndexLocalIp = 2;
constexpr uint32_t kArgIndexLocalCommRes = 3;
constexpr const char *kPromptReadyMessage = "LLM_DATADIST_PROMPT_READY_CHECK";
constexpr const char *kUnlinkAckMessage = "LLM_DATADIST_UNLINKED";

#define CHECK_ACL(x)                                                                  \
  do {                                                                                \
    aclError __ret = x;                                                               \
    if (__ret != ACL_ERROR_NONE) {                                                    \
      std::cerr << __FILE__ << ":" << __LINE__ << " aclError:" << __ret << std::endl; \
    }                                                                                 \
  } while (0)

const char *GetRecentErrMsg() {
  const char *errmsg = aclGetRecentErrMsg();
  if (errmsg == nullptr) {
    return "no error";
  }
  return errmsg;
}

void CloseFd(int &fd) {
  if (fd >= 0) {
    (void)close(fd);
    fd = -1;
  }
}

int StartPromptControlServer(const std::string &local_ip) {
  int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (listen_fd < 0) {
    printf("[ERROR] Create prompt control socket failed, errno = %d\n", errno);
    return -1;
  }

  const int reuse = 1;
  if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) != 0) {
    printf("[ERROR] Set prompt control socket option failed, errno = %d\n", errno);
    CloseFd(listen_fd);
    return -1;
  }

  sockaddr_in listen_addr{};
  listen_addr.sin_family = AF_INET;
  listen_addr.sin_port = htons(kPromptControlPort);
  if (inet_pton(AF_INET, local_ip.c_str(), &listen_addr.sin_addr) != 1) {
    printf("[ERROR] Invalid prompt control listen ip: %s\n", local_ip.c_str());
    CloseFd(listen_fd);
    return -1;
  }

  if (bind(listen_fd, reinterpret_cast<sockaddr *>(&listen_addr), sizeof(listen_addr)) != 0) {
    printf("[ERROR] Bind prompt control socket failed, ip = %s, port = %u, errno = %d\n", local_ip.c_str(),
           static_cast<unsigned int>(kPromptControlPort), errno);
    CloseFd(listen_fd);
    return -1;
  }

  if (listen(listen_fd, kSocketBacklog) != 0) {
    printf("[ERROR] Listen prompt control socket failed, errno = %d\n", errno);
    CloseFd(listen_fd);
    return -1;
  }

  printf("[INFO] Prompt control server listen on %s:%u\n", local_ip.c_str(),
         static_cast<unsigned int>(kPromptControlPort));
  return listen_fd;
}

int WaitControlMessage(int listen_fd, const char *expected_message, const char *message_desc) {
  while (true) {
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(listen_fd, &read_fds);
    timeval timeout{};
    timeout.tv_sec = kControlTimeoutSec;
    const int select_ret = select(listen_fd + 1, &read_fds, nullptr, nullptr, &timeout);
    if (select_ret < 0 && errno == EINTR) {
      continue;
    }
    if (select_ret == 0) {
      printf("[ERROR] Wait %s timeout, timeout = %d seconds\n", message_desc, kControlTimeoutSec);
      return -1;
    }
    if (select_ret < 0) {
      printf("[ERROR] Wait %s failed, errno = %d\n", message_desc, errno);
      return -1;
    }
    break;
  }

  sockaddr_in peer_addr{};
  socklen_t peer_addr_len = sizeof(peer_addr);
  int conn_fd = accept(listen_fd, reinterpret_cast<sockaddr *>(&peer_addr), &peer_addr_len);
  if (conn_fd < 0) {
    printf("[ERROR] Accept %s connection failed, errno = %d\n", message_desc, errno);
    return -1;
  }

  const size_t expected_len = std::strlen(expected_message);
  std::vector<char> buffer(expected_len + 1U, 0);
  size_t received_len = 0U;
  while (received_len < expected_len) {
    const ssize_t recv_len = recv(conn_fd, buffer.data() + received_len, expected_len - received_len, 0);
    if (recv_len < 0 && errno == EINTR) {
      continue;
    }
    if (recv_len <= 0) {
      printf("[ERROR] Receive %s failed, errno = %d\n", message_desc, errno);
      CloseFd(conn_fd);
      return -1;
    }
    received_len += static_cast<size_t>(recv_len);
  }
  CloseFd(conn_fd);

  if (std::string(buffer.data(), received_len) != expected_message) {
    printf("[ERROR] Unexpected %s message: %s\n", message_desc, buffer.data());
    return -1;
  }

  printf("[INFO] Receive %s success\n", message_desc);
  return 0;
}
}  // namespace

int Initialize(LlmDataDist &llm_datadist, const std::string &device_id, const std::string &local_ip,
               const std::string &local_comm_res) {
  std::map<AscendString, AscendString> options;
  options[OPTION_DEVICE_ID] = device_id.c_str();
  options[OPTION_LISTEN_IP_INFO] = (local_ip + ":" + std::to_string(kPromptListenPort)).c_str();
  if (!local_comm_res.empty()) {
    options[OPTION_TRANSFER_BACKEND] = "hixl";
    options[OPTION_LOCAL_COMM_RES] = local_comm_res.c_str();
  }
  auto ret = llm_datadist.Initialize(options);
  if (ret != LLM_SUCCESS) {
    printf("[ERROR] Initialize failed, ret = %u, errmsg: %s\n", ret, GetRecentErrMsg());
    return -1;
  }
  printf("[INFO] Initialize success\n");
  return LLM_SUCCESS;
}

void Finalize(LlmDataDist &llm_datadist, int64_t cache_id, const std::vector<void *> &buffers) {
  if (cache_id > 0) {
    auto ret = llm_datadist.UnregisterKvCache(cache_id);
    if (ret != 0) {
      printf("[ERROR] UnregisterKvCache failed, ret = %u, errmsg: %s\n", ret, GetRecentErrMsg());
    } else {
      printf("[INFO] UnregisterKvCache success\n");
    }
  }
  for (auto buffer : buffers) {
    aclrtFree(buffer);
  }
  llm_datadist.Finalize();
}

void ForceFinalize(LlmDataDist &llm_datadist, const std::vector<void *> &buffers) {
  llm_datadist.Finalize();
  for (auto buffer : buffers) {
    aclrtFree(buffer);
  }
}

int32_t RunPromptSample(const char *device_id, const char *local_ip, const std::string &local_comm_res) {
  printf("[INFO] Prompt Sample start\n");
  // 1. 初始化
  LlmDataDist llm_datadist(kPromptClusterId, LlmRole::kPrompt);
  if (Initialize(llm_datadist, device_id, local_ip, local_comm_res) != 0) {
    printf("[ERROR] Initialize LlmDataDist failed\n");
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

    // init device buffer
    std::vector<int32_t> host_buffer(kTensorSize / sizeof(int32_t));
    std::iota(host_buffer.begin(), host_buffer.end(), 0);
    CHECK_ACL(aclrtMemcpy(buffer, kTensorSize, &host_buffer[0], kTensorSize, ACL_MEMCPY_HOST_TO_DEVICE));

    tensor_addrs.emplace_back(reinterpret_cast<uint64_t>(buffer));
    buffers.emplace_back(reinterpret_cast<void *>(buffer));
  }
  int64_t cache_id = -1;
  auto ret = llm_datadist.RegisterKvCache(cache_desc, tensor_addrs, {}, cache_id);
  if (ret != LLM_SUCCESS) {
    printf("[ERROR] RegisterKvCache failed, ret = %u, errmsg: %s\n", ret, GetRecentErrMsg());
    Finalize(llm_datadist, cache_id, buffers);
    return -1;
  }
  // 3. RegisterKvCache成功后，可以获取cache中各tensor的地址用于后续操作
  printf("[INFO] RegisterKvCache success\n");
  for (size_t i = 0U; i < tensor_addrs.size(); ++i) {
    printf("[INFO] Tensor[%zu] addr = %p\n", i, reinterpret_cast<void *>(tensor_addrs[i]));
  }

  int control_fd = StartPromptControlServer(local_ip);
  if (control_fd < 0) {
    ForceFinalize(llm_datadist, buffers);
    return -1;
  }

  // 4. 等待decoder确认prompt cache已就绪后再建链
  if (WaitControlMessage(control_fd, kPromptReadyMessage, "prompt ready check") != 0) {
    CloseFd(control_fd);
    ForceFinalize(llm_datadist, buffers);
    return -1;
  }

  // 5. 等待decoder完成UnlinkLlmClusters，确认本端comm已解绑
  if (WaitControlMessage(control_fd, kUnlinkAckMessage, "unlink ack") != 0) {
    CloseFd(control_fd);
    ForceFinalize(llm_datadist, buffers);
    return -1;
  }
  CloseFd(control_fd);

  // 6. 释放Cache与llmDataDist
  Finalize(llm_datadist, cache_id, buffers);
  printf("[INFO] Prompt Sample end\n");
  return 0;
}

int main(int32_t argc, char **argv) {
  if (argc < kExpectedArgCnt - 1 || argc > kExpectedArgCnt) {
    printf("[ERROR] expect at least 3 args(device_id, localHostIp, [local_comm_res]), but got %d\n", argc - 1);
    return -1;
  }
  const auto device_id = argv[kArgIndexDeviceId];
  const auto local_ip = argv[kArgIndexLocalIp];
  printf("[INFO] device_id = %s, local_ip = %s\n", device_id, local_ip);
  std::string local_comm_res;
  if (argc == kExpectedArgCnt) {
    local_comm_res = argv[kArgIndexLocalCommRes];
    printf("[INFO] local_comm_res = %s\n", local_comm_res.c_str());
  }
  auto ret = RunPromptSample(device_id, local_ip, local_comm_res);
  return ret;
}
