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
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include "acl/acl.h"
#include "hixl/hixl.h"

using namespace hixl;
namespace {
constexpr size_t kBufSize = 8 * 1024 * 1024;
constexpr size_t kBlockSize = 16 * 1024;
constexpr size_t kBlockCount = kBufSize / kBlockSize;
constexpr int32_t kDefaultDeviceClient = 0;
constexpr int32_t kDefaultDeviceServer = 1;
constexpr int32_t kConnectTimeout = 5000;
constexpr int32_t kTransferTimeout = 30000;
constexpr int32_t kSocketPortOffset = 1000;
constexpr int32_t kSocketBacklog = 1;
constexpr int32_t kSocketRetryCount = 10;
constexpr int32_t kSocketRetryIntervalUs = 500000;
constexpr uint8_t kFillValue = 0xAA;

#define CHECK_ACL(x)                                                                     \
  do {                                                                                   \
    aclError __status = x;                                                               \
    if (__status != ACL_ERROR_NONE) {                                                    \
      std::cerr << __FILE__ << ":" << __LINE__ << " aclError:" << __status << std::endl; \
    }                                                                                    \
  } while (0)

struct EngineCtx {
  Hixl engine;
  int32_t device_id = 0;
  std::string local_engine;
  std::string remote_engine;
  bool is_client = false;
  bool initialized = false;
  bool connected = false;
  void *dev_buf = nullptr;
  MemHandle dev_handle = nullptr;
  uintptr_t remote_addr = 0;
  int listen_fd = -1;
  int conn_fd = -1;
};

const char *GetRecentErrMsg() {
  const char *err_msg = aclGetRecentErrMsg();
  return (err_msg == nullptr) ? "no error" : err_msg;
}

int32_t ParsePort(const std::string &engine) {
  auto pos = engine.find(':');
  if (pos == std::string::npos) {
    return -1;
  }
  return std::stoi(engine.substr(pos + 1));
}

int32_t InitEngine(EngineCtx &ctx, const std::string &local_engine, const std::string &remote_engine) {
  CHECK_ACL(aclrtSetDevice(ctx.device_id));
  std::map<AscendString, AscendString> options;
  std::string resource_config = "{\"comm_resource_config.protocol_desc\": [\"roce:device\"]}";
  options[OPTION_GLOBAL_RESOURCE_CONFIG] = resource_config.c_str();
  
  auto ret = ctx.engine.Initialize(local_engine.c_str(), options);
  if (ret != SUCCESS) {
    printf("[HIXL ERROR] Initialize failed, ret=%u, errmsg:%s\n", ret, GetRecentErrMsg());
    return -1;
  }
  ctx.initialized = true;
  ctx.local_engine = local_engine;
  ctx.remote_engine = remote_engine;
  
  uint8_t *dev_ptr = nullptr;
  CHECK_ACL(aclrtMalloc(reinterpret_cast<void **>(&dev_ptr), kBufSize, ACL_MEM_MALLOC_HUGE_ONLY));
  ctx.dev_buf = dev_ptr;
  
  MemDesc desc{};
  desc.addr = reinterpret_cast<uintptr_t>(ctx.dev_buf);
  desc.len = kBufSize;
  ret = ctx.engine.RegisterMem(desc, MEM_DEVICE, ctx.dev_handle);
  if (ret != SUCCESS) {
    printf("[HIXL ERROR] RegisterMem failed, ret=%u, errmsg:%s\n", ret, GetRecentErrMsg());
    return -1;
  }
  
  if (!ctx.is_client) {
    std::vector<uint8_t> fill_data(kBufSize, kFillValue);
    CHECK_ACL(aclrtMemcpy(ctx.dev_buf, kBufSize, fill_data.data(), kBufSize, ACL_MEMCPY_HOST_TO_DEVICE));
  }
  printf("[HIXL] InitEngine success, device:%d, local:%s, remote:%s\n", ctx.device_id, local_engine.c_str(), remote_engine.c_str());
  return 0;
}

int32_t GetRemoteAddr(EngineCtx &ctx) {
  int port = ParsePort(ctx.remote_engine) + kSocketPortOffset;
  std::string ip = ctx.remote_engine.substr(0, ctx.remote_engine.find(':'));
  int fd = -1;
  for (int32_t retry = 0; retry < kSocketRetryCount; ++retry) {
    fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);
    if (connect(fd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) == 0) {
      break;
    }
    close(fd);
    fd = -1;
    if (retry + 1 < kSocketRetryCount) {
      printf("[HIXL] Connect retry %d/%d...\n", retry + 1, kSocketRetryCount);
      usleep(kSocketRetryIntervalUs);
    }
  }
  if (fd < 0) {
    printf("[HIXL ERROR] Failed to connect to server on port %d\n", port);
    return -1;
  }
  ssize_t received = 0;
  while (received < static_cast<ssize_t>(sizeof(ctx.remote_addr))) {
    ssize_t n = recv(fd, reinterpret_cast<char *>(&ctx.remote_addr) + received, sizeof(ctx.remote_addr) - received, 0);
    if (n <= 0) {
      printf("[HIXL ERROR] recv failed\n");
      close(fd);
      return -1;
    }
    received += n;
  }
  ctx.conn_fd = fd;
  printf("[HIXL] GetRemoteAddr success, remote_addr:%p\n", reinterpret_cast<void *>(ctx.remote_addr));
  return 0;
}

int32_t Connect(EngineCtx &ctx) {
  auto ret = ctx.engine.Connect(ctx.remote_engine.c_str(), kConnectTimeout);
  if (ret != SUCCESS) {
    printf("[HIXL ERROR] Connect failed, ret=%u, errmsg:%s\n", ret, GetRecentErrMsg());
    return -1;
  }
  ctx.connected = true;
  printf("[HIXL] Connect success\n");
  return 0;
}

int32_t Transfer(EngineCtx &ctx) {
  std::vector<TransferOpDesc> descs;
  descs.reserve(kBlockCount);
  for (size_t i = 0; i < kBlockCount; ++i) {
    TransferOpDesc desc{};
    desc.local_addr = reinterpret_cast<uintptr_t>(ctx.dev_buf) + i * kBlockSize;
    desc.remote_addr = ctx.remote_addr + i * kBlockSize;
    desc.len = kBlockSize;
    descs.push_back(desc);
  }
  auto ret = ctx.engine.TransferSync(ctx.remote_engine.c_str(), READ, descs, kTransferTimeout);
  if (ret != SUCCESS) {
    printf("[HIXL ERROR] TransferSync READ failed, ret=%u, errmsg:%s\n", ret, GetRecentErrMsg());
    return -1;
  }
  printf("[HIXL] TransferSync READ completed\n");
  return 0;
}

int32_t Verify(EngineCtx &ctx) {
  void *host_tmp = nullptr;
  CHECK_ACL(aclrtMallocHost(&host_tmp, kBufSize));
  CHECK_ACL(aclrtMemcpy(host_tmp, kBufSize, ctx.dev_buf, kBufSize, ACL_MEMCPY_DEVICE_TO_HOST));
  std::vector<uint8_t> expected(kBufSize, kFillValue);
  if (std::memcmp(host_tmp, expected.data(), kBufSize) != 0) {
    printf("[HIXL ERROR] Verify failed, data mismatch\n");
    CHECK_ACL(aclrtFreeHost(host_tmp));
    return -1;
  }
  CHECK_ACL(aclrtFreeHost(host_tmp));
  printf("[HIXL] Verify: PASS\n");
  return 0;
}

int32_t SetupListenSocket(EngineCtx &ctx, int port) {
  ctx.listen_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (ctx.listen_fd < 0) {
    printf("[HIXL ERROR] socket() failed\n");
    return -1;
  }
  int opt = 1;
  setsockopt(ctx.listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
  struct sockaddr_in addr {};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(static_cast<uint16_t>(port));
  if (bind(ctx.listen_fd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) < 0) {
    printf("[HIXL ERROR] bind port %d failed\n", port);
    close(ctx.listen_fd);
    ctx.listen_fd = -1;
    return -1;
  }
  if (listen(ctx.listen_fd, kSocketBacklog) < 0) {
    printf("[HIXL ERROR] listen failed\n");
    close(ctx.listen_fd);
    ctx.listen_fd = -1;
    return -1;
  }
  return 0;
}

int32_t AcceptAndSendAddr(EngineCtx &ctx) {
  ctx.conn_fd = accept(ctx.listen_fd, nullptr, nullptr);
  if (ctx.conn_fd < 0) {
    printf("[HIXL ERROR] accept failed\n");
    return -1;
  }
  uintptr_t buf_addr = reinterpret_cast<uintptr_t>(ctx.dev_buf);
  ssize_t sent = 0;
  while (sent < static_cast<ssize_t>(sizeof(buf_addr))) {
    ssize_t n = send(ctx.conn_fd, reinterpret_cast<const char *>(&buf_addr) + sent, sizeof(buf_addr) - sent, 0);
    if (n <= 0) {
      printf("[HIXL ERROR] send failed\n");
      close(ctx.conn_fd);
      ctx.conn_fd = -1;
      return -1;
    }
    sent += n;
  }
  printf("[HIXL] Sent buffer addr %p to client, waiting for client to finish...\n", reinterpret_cast<void *>(buf_addr));
  return 0;
}

void WaitForClientDisconnect(EngineCtx &ctx) {
  char dummy;
  ssize_t recv_ret = 1;
  while (recv_ret > 0) {
    recv_ret = recv(ctx.conn_fd, &dummy, 1, 0);
  }
  printf("[HIXL] Client disconnected\n");
}

int32_t ServerSendAddr(EngineCtx &ctx) {
  int port = ParsePort(ctx.local_engine) + kSocketPortOffset;
  if (SetupListenSocket(ctx, port) != 0) {
    return -1;
  }
  printf("[HIXL] Waiting for client on port %d...\n", port);
  if (AcceptAndSendAddr(ctx) != 0) {
    return -1;
  }
  WaitForClientDisconnect(ctx);
  return 0;
}

void Finalize(EngineCtx &ctx) {
  if (ctx.connected) {
    auto ret = ctx.engine.Disconnect(ctx.remote_engine.c_str(), kConnectTimeout);
    if (ret != SUCCESS) {
      printf("[HIXL ERROR] Disconnect failed, ret=%u, errmsg:%s\n", ret, GetRecentErrMsg());
    } else {
      printf("[HIXL] Disconnect success\n");
    }
  }
  if (ctx.conn_fd >= 0) {
    close(ctx.conn_fd);
  }
  if (ctx.listen_fd >= 0) {
    close(ctx.listen_fd);
  }
  if (ctx.dev_handle != nullptr) {
    ctx.engine.DeregisterMem(ctx.dev_handle);
    printf("[HIXL] DeregisterMem success\n");
  }
  if (ctx.dev_buf != nullptr) {
    CHECK_ACL(aclrtFree(ctx.dev_buf));
    printf("[HIXL] Free memory success\n");
  }
  if (ctx.initialized) {
    ctx.engine.Finalize();
    printf("[HIXL] Finalize success\n");
  }
  CHECK_ACL(aclrtResetDevice(ctx.device_id));
}
}  // namespace

int main(int argc, char **argv) {
  EngineCtx ctx;
  std::string local_engine = "127.0.0.1:16000";
  std::string remote_engine = "127.0.0.1:16001";
  std::string wait_file = "";
  std::string signal_file = "";
  bool is_client = true;
  
  for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]) == "--role=client") {
      is_client = true;
      ctx.device_id = kDefaultDeviceClient;
    } else if (std::string(argv[i]) == "--role=server") {
      is_client = false;
      ctx.device_id = kDefaultDeviceServer;
    } else if (std::string(argv[i]) == "--device=") {
      ctx.device_id = std::stoi(argv[i] + 9);
    } else if (std::string(argv[i]) == "--local-engine=") {
      local_engine = argv[i] + 15;
    } else if (std::string(argv[i]) == "--remote-engine=") {
      remote_engine = argv[i] + 16;
    } else if (std::string(argv[i]) == "--wait-file" && i + 1 < argc) {
      wait_file = argv[++i];
    } else if (std::string(argv[i]) == "--signal-file" && i + 1 < argc) {
      signal_file = argv[++i];
    }
  }
  
  ctx.is_client = is_client;
  
  if (!wait_file.empty()) {
    printf("[HIXL] Waiting for HCCL BEFORE to complete... (%s)\n", wait_file.c_str());
    while (!std::ifstream(wait_file).good()) {
      usleep(100000);
    }
    printf("[HIXL] HCCL BEFORE completed, starting HIXL...\n");
  }
  
  if (InitEngine(ctx, local_engine, remote_engine) != 0) {
    return -1;
  }
  
  if (ctx.is_client) {
    if (GetRemoteAddr(ctx) != 0) {
      Finalize(ctx);
      return -1;
    }
    if (Connect(ctx) != 0) {
      Finalize(ctx);
      return -1;
    }
    if (Transfer(ctx) != 0) {
      Finalize(ctx);
      return -1;
    }
    if (Verify(ctx) != 0) {
      Finalize(ctx);
      return -1;
    }
  } else {
    if (ServerSendAddr(ctx) != 0) {
      Finalize(ctx);
      return -1;
    }
  }
  
  Finalize(ctx);
  
  if (!signal_file.empty()) {
    std::ofstream signal(signal_file);
    signal << "HIXL_DONE";
    signal.close();
    printf("[HIXL] Signal sent: %s\n", signal_file.c_str());
  }
  
  printf("[HIXL] === ALL TESTS PASSED ===\n");
  return 0;
}
