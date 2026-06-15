/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
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
#include <chrono>
#include <thread>
#include <string>
#include <vector>
#include "acl/acl.h"
#include "hixl/hixl.h"

using namespace hixl;
namespace {
constexpr size_t kBufSize = 8 * 1024 * 1024;
constexpr size_t kBlockSize = 16 * 1024;
constexpr size_t kBlockCount = kBufSize / kBlockSize;
constexpr int32_t kDefaultDeviceA = 0;
constexpr int32_t kDefaultDeviceB = 2;
constexpr int32_t kVersionLegacy = 0;
constexpr const char *kEngineA = "127.0.0.1:16000";
constexpr const char *kEngineB = "127.0.0.1:16001";
constexpr uint8_t kFillA = 0xAA;
constexpr uint8_t kFillB = 0xBB;
constexpr int32_t kConnectTimeout = 5000;
constexpr int32_t kMaxPollCount = 100000;
static const std::vector<std::string> kValidProtocols = {
    "roce:device", "roce:host",
    "uboe:device", "ubg:device",
    "ub_ctp:device", "ub_tp:device", "ub_ctp:host", "ub_tp:host"};

#define CHECK_ACL(x)                                                                  \
  do {                                                                                \
    aclError __ret = x;                                                               \
    if (__ret != ACL_ERROR_NONE) {                                                    \
      std::cerr << __FILE__ << ":" << __LINE__ << " aclError:" << __ret << std::endl; \
    }                                                                                 \
  } while (0)

struct EngineCtx {
  Hixl engine;
  int32_t device_id = 0;
  const char *name = nullptr;
  bool initialized = false;
  bool connected = false;
  void *dev_buf = nullptr;
  void *host_buf = nullptr;
  MemHandle dev_handle = nullptr;
  MemHandle host_handle = nullptr;
};

const char *GetRecentErrMsg() {
  const char *errmsg = aclGetRecentErrMsg();
  if (errmsg == nullptr) {
    return "no error";
  }
  return errmsg;
}

int32_t ParseArgs(int32_t argc, char **argv, int32_t &device_a, int32_t &device_b,
                  std::vector<std::string> &protocols, int32_t &version) {
  for (int32_t i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg.find("--device=") == 0) {
      std::string val = arg.substr(9);
      auto pos = val.find(',');
      if (pos == std::string::npos) {
        printf("[ERROR] Invalid --device format, expected id1,id2\n");
        return -1;
      }
      device_a = std::stoi(val.substr(0, pos));
      device_b = std::stoi(val.substr(pos + 1));
    } else if (arg.find("--protocol=") == 0) {
      std::string val = arg.substr(11);
      size_t start = 0;
      size_t pos = val.find(',');
      while (pos != std::string::npos) {
        protocols.push_back(val.substr(start, pos - start));
        start = pos + 1;
        pos = val.find(',', start);
      }
      protocols.push_back(val.substr(start));
    } else if (arg.find("--version=") == 0) {
      version = std::stoi(arg.substr(10));
    } else {
      printf("[ERROR] Unknown argument: %s\n", arg.c_str());
      printf("Usage: %s --protocol=<type>[,...] [--device=id1,id2] [--version=0|1]\n", argv[0]);
      return -1;
    }
  }
  if (protocols.empty()) {
    printf("[ERROR] --protocol is required\n");
    printf("Usage: %s --protocol=<type>[,...] [--device=id1,id2] [--version=0|1]\n", argv[0]);
    return -1;
  }
  for (const auto &p : protocols) {
    bool valid = false;
    for (const auto &vp : kValidProtocols) {
      if (vp == p) {
        valid = true;
        break;
      }
    }
    if (!valid) {
      printf("[ERROR] Invalid protocol: %s\n", p.c_str());
      printf("Supported:");
      for (const auto &vp : kValidProtocols) {
        printf(" %s", vp.c_str());
      }
      printf("\n");
      return -1;
    }
  }
  if (version == kVersionLegacy && (protocols.size() != 1 || protocols[0] != "roce:device")) {
    printf("[ERROR] version 0 only supports roce:device\n");
    return -1;
  }
  printf("[INFO] ParseArgs success: device_a=%d, device_b=%d, version=%d\n", device_a, device_b, version);
  for (const auto &p : protocols) {
    printf("[INFO]   protocol: %s\n", p.c_str());
  }
  return 0;
}

int32_t InitEngine(EngineCtx &ctx, const std::vector<std::string> &protocols, int32_t version, uint8_t fill_value) {
  CHECK_ACL(aclrtSetDevice(ctx.device_id));
  std::map<AscendString, AscendString> options;
  std::string desc_array;
  for (size_t i = 0; i < protocols.size(); ++i) {
    if (i > 0) {
      desc_array += ",";
    }
    desc_array += "\"" + protocols[i] + "\"";
  }
  if (version == kVersionLegacy) {
    // 老版本流程
    printf("[INFO] %s using legacy flow (version=0)\n", ctx.name);
    std::string name_str(ctx.name);
    uint32_t listen_port = std::stoi(name_str.substr(name_str.find(':') + 1));
    std::string local_comm_res = "{\"version\": \"1.2\"}";
    options[OPTION_LOCAL_COMM_RES] = local_comm_res.c_str();
    std::string resource_config =
        "{\"comm_resource_config.listen_port\": " + std::to_string(listen_port) + "}";
    options[OPTION_GLOBAL_RESOURCE_CONFIG] = resource_config.c_str();
    if (protocols[0] == "roce:device") {
      options[OPTION_BUFFER_POOL] = "0:0";
      setenv("HCCL_INTRA_ROCE_ENABLE", "1", 1);
    }
  } else {
    std::string resource_config =
        "{\"comm_resource_config.protocol_desc\": [" + desc_array + "]}";
    options[OPTION_GLOBAL_RESOURCE_CONFIG] = resource_config.c_str();
  }
  auto ret = ctx.engine.Initialize(ctx.name, options);
  if (ret != SUCCESS) {
    printf("[ERROR] Initialize %s failed, ret=%u, errmsg:%s\n", ctx.name, ret, GetRecentErrMsg());
    return -1;
  }
  ctx.initialized = true;
  printf("[INFO] InitEngine %s success\n", ctx.name);
  uint8_t *dev_ptr = nullptr;
  CHECK_ACL(aclrtMalloc(reinterpret_cast<void **>(&dev_ptr), kBufSize, ACL_MEM_MALLOC_HUGE_ONLY));
  ctx.dev_buf = dev_ptr;
  CHECK_ACL(aclrtMallocHost(&ctx.host_buf, kBufSize));
  std::memset(ctx.host_buf, 0, kBufSize);
  MemDesc desc{};
  desc.addr = reinterpret_cast<uintptr_t>(ctx.dev_buf);
  desc.len = kBufSize;
  ret = ctx.engine.RegisterMem(desc, MEM_DEVICE, ctx.dev_handle);
  if (ret != SUCCESS) {
    printf("[ERROR] %s RegisterMem device failed, ret=%u, errmsg:%s\n", ctx.name, ret, GetRecentErrMsg());
    return -1;
  }
  desc.addr = reinterpret_cast<uintptr_t>(ctx.host_buf);
  ret = ctx.engine.RegisterMem(desc, MEM_HOST, ctx.host_handle);
  if (ret != SUCCESS) {
    printf("[ERROR] %s RegisterMem host failed, ret=%u, errmsg:%s\n", ctx.name, ret, GetRecentErrMsg());
    return -1;
  }
  std::memset(ctx.host_buf, fill_value, kBufSize);
  CHECK_ACL(aclrtMemcpy(ctx.dev_buf, kBufSize, ctx.host_buf, kBufSize, ACL_MEMCPY_HOST_TO_DEVICE));
  printf("[INFO] %s InitEngine success, dev:%p, host:%p\n", ctx.name, ctx.dev_buf, ctx.host_buf);
  return 0;
}

int32_t Connect(EngineCtx &ctx_a, EngineCtx &ctx_b) {
  auto ret = ctx_a.engine.ConnectAsync(ctx_b.name, kConnectTimeout);
  if (ret != SUCCESS) {
    printf("[ERROR] ConnectAsync %s->%s failed, ret=%u, errmsg:%s\n",
           ctx_a.name, ctx_b.name, ret, GetRecentErrMsg());
    return -1;
  }
  ret = ctx_b.engine.ConnectAsync(ctx_a.name, kConnectTimeout);
  if (ret != SUCCESS) {
    printf("[ERROR] ConnectAsync %s->%s failed, ret=%u, errmsg:%s\n",
           ctx_b.name, ctx_a.name, ret, GetRecentErrMsg());
    return -1;
  }
  AsyncConnectStatus status_a = AsyncConnectStatus::NOT_CONNECT;
  AsyncConnectStatus status_b = AsyncConnectStatus::NOT_CONNECT;
  while (status_a != AsyncConnectStatus::CONNECTED || status_b != AsyncConnectStatus::CONNECTED) {
    ctx_a.engine.GetAsyncConnectStatus(ctx_b.name, status_a);
    ctx_b.engine.GetAsyncConnectStatus(ctx_a.name, status_b);
    if (status_a == AsyncConnectStatus::CONNECT_FAILED || status_b == AsyncConnectStatus::CONNECT_FAILED) {
      printf("[ERROR] Connect failed, status_a=%d, status_b=%d\n",
             static_cast<int>(status_a), static_cast<int>(status_b));
      return -1;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  ctx_a.connected = true;
  ctx_b.connected = true;
  printf("[INFO] Connect success\n");
  return 0;
}

int32_t Transfer(EngineCtx &ctx_a, EngineCtx &ctx_b) {
  std::vector<TransferOpDesc> descs;
  descs.reserve(kBlockCount);
  for (size_t i = 0; i < kBlockCount; ++i) {
    TransferOpDesc desc{};
    desc.local_addr = reinterpret_cast<uintptr_t>(ctx_a.dev_buf) + i * kBlockSize;
    desc.remote_addr = reinterpret_cast<uintptr_t>(ctx_b.host_buf) + i * kBlockSize;
    desc.len = kBlockSize;
    descs.push_back(desc);
  }
  TransferArgs args{};
  TransferReq req_a = nullptr;
  auto ret = ctx_a.engine.TransferAsync(ctx_b.name, WRITE, descs, args, req_a);
  if (ret != SUCCESS) {
    printf("[ERROR] TransferAsync %s->%s failed, ret=%u, errmsg:%s\n",
           ctx_a.name, ctx_b.name, ret, GetRecentErrMsg());
    return -1;
  }
  for (size_t i = 0; i < kBlockCount; ++i) {
    descs[i].local_addr = reinterpret_cast<uintptr_t>(ctx_b.dev_buf) + i * kBlockSize;
    descs[i].remote_addr = reinterpret_cast<uintptr_t>(ctx_a.host_buf) + i * kBlockSize;
  }
  TransferReq req_b = nullptr;
  ret = ctx_b.engine.TransferAsync(ctx_a.name, WRITE, descs, args, req_b);
  if (ret != SUCCESS) {
    printf("[ERROR] TransferAsync %s->%s failed, ret=%u, errmsg:%s\n",
           ctx_b.name, ctx_a.name, ret, GetRecentErrMsg());
    return -1;
  }
  TransferStatus st_a = TransferStatus::WAITING;
  TransferStatus st_b = TransferStatus::WAITING;
  int32_t poll_count = 0;
  while (st_a == TransferStatus::WAITING || st_b == TransferStatus::WAITING) {
    if (st_a == TransferStatus::WAITING) {
      ctx_a.engine.GetTransferStatus(req_a, st_a);
    }
    if (st_b == TransferStatus::WAITING) {
      ctx_b.engine.GetTransferStatus(req_b, st_b);
    }
    if (++poll_count > kMaxPollCount) {
      printf("[ERROR] Transfer poll timeout\n");
      return -1;
    }
    std::this_thread::sleep_for(std::chrono::microseconds(100));
  }
  if (st_a != TransferStatus::COMPLETED) {
    printf("[ERROR] Transfer %s->%s failed, status=%d\n", ctx_a.name, ctx_b.name, static_cast<int>(st_a));
    return -1;
  }
  if (st_b != TransferStatus::COMPLETED) {
    printf("[ERROR] Transfer %s->%s failed, status=%d\n", ctx_b.name, ctx_a.name, static_cast<int>(st_b));
    return -1;
  }
  printf("[INFO] Transfer completed\n");
  return 0;
}

int32_t Verify(EngineCtx &ctx_a, EngineCtx &ctx_b) {
  std::vector<uint8_t> expected_a(kBufSize, kFillA);
  std::vector<uint8_t> expected_b(kBufSize, kFillB);
  if (std::memcmp(ctx_b.host_buf, expected_a.data(), kBufSize) != 0) {
    printf("[ERROR] Verify %s host failed, expected 0xAA\n", ctx_b.name);
    return -1;
  }
  if (std::memcmp(ctx_a.host_buf, expected_b.data(), kBufSize) != 0) {
    printf("[ERROR] Verify %s host failed, expected 0xBB\n", ctx_a.name);
    return -1;
  }
  printf("[INFO] Verify success\n");
  return 0;
}

void Finalize(EngineCtx &ctx_a, EngineCtx &ctx_b) {
  if (ctx_a.connected) {
    auto ret = ctx_a.engine.DisconnectAsync(ctx_b.name, kConnectTimeout);
    if (ret != SUCCESS) {
      printf("[ERROR] DisconnectAsync %s->%s failed, ret=%u, errmsg:%s\n",
             ctx_a.name, ctx_b.name, ret, GetRecentErrMsg());
    }
  }
  if (ctx_b.connected) {
    auto ret = ctx_b.engine.DisconnectAsync(ctx_a.name, kConnectTimeout);
    if (ret != SUCCESS) {
      printf("[ERROR] DisconnectAsync %s->%s failed, ret=%u, errmsg:%s\n",
             ctx_b.name, ctx_a.name, ret, GetRecentErrMsg());
    }
  }
  if (ctx_a.connected || ctx_b.connected) {
    AsyncConnectStatus st_a = AsyncConnectStatus::DISCONNECT_PENDING;
    AsyncConnectStatus st_b = AsyncConnectStatus::DISCONNECT_PENDING;
    while (st_a != AsyncConnectStatus::NOT_CONNECT || st_b != AsyncConnectStatus::NOT_CONNECT) {
      ctx_a.engine.GetAsyncConnectStatus(ctx_b.name, st_a);
      ctx_b.engine.GetAsyncConnectStatus(ctx_a.name, st_b);
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    printf("[INFO] Disconnect success\n");
  }
  if (ctx_a.dev_handle != nullptr) {
    ctx_a.engine.DeregisterMem(ctx_a.dev_handle);
  }
  if (ctx_a.host_handle != nullptr) {
    ctx_a.engine.DeregisterMem(ctx_a.host_handle);
  }
  if (ctx_b.dev_handle != nullptr) {
    ctx_b.engine.DeregisterMem(ctx_b.dev_handle);
  }
  if (ctx_b.host_handle != nullptr) {
    ctx_b.engine.DeregisterMem(ctx_b.host_handle);
  }
  if (ctx_a.dev_buf != nullptr) {
    CHECK_ACL(aclrtFree(ctx_a.dev_buf));
  }
  if (ctx_a.host_buf != nullptr) {
    CHECK_ACL(aclrtFreeHost(ctx_a.host_buf));
  }
  if (ctx_b.dev_buf != nullptr) {
    CHECK_ACL(aclrtFree(ctx_b.dev_buf));
  }
  if (ctx_b.host_buf != nullptr) {
    CHECK_ACL(aclrtFreeHost(ctx_b.host_buf));
  }
  if (ctx_a.initialized) {
    ctx_a.engine.Finalize();
  }
  if (ctx_b.initialized) {
    ctx_b.engine.Finalize();
  }
  CHECK_ACL(aclrtResetDevice(ctx_a.device_id));
  CHECK_ACL(aclrtResetDevice(ctx_b.device_id));
}

int32_t Run(EngineCtx &ctx_a, EngineCtx &ctx_b, const std::vector<std::string> &protocols,
            int32_t version) {
  if (InitEngine(ctx_a, protocols, version, kFillA) != 0) {
    return -1;
  }
  if (InitEngine(ctx_b, protocols, version, kFillB) != 0) {
    return -1;
  }
  if (Connect(ctx_a, ctx_b) != 0) {
    return -1;
  }
  if (Transfer(ctx_a, ctx_b) != 0) {
    return -1;
  }
  return Verify(ctx_a, ctx_b);
}
}  // namespace

int main(int32_t argc, char **argv) {
  int32_t device_a = kDefaultDeviceA;
  int32_t device_b = kDefaultDeviceB;
  std::vector<std::string> protocols;
  int32_t version = 1;
  if (ParseArgs(argc, argv, device_a, device_b, protocols, version) != 0) {
    return -1;
  }
  EngineCtx ctx_a;
  EngineCtx ctx_b;
  ctx_a.device_id = device_a;
  ctx_a.name = kEngineA;
  ctx_b.device_id = device_b;
  ctx_b.name = kEngineB;
  int32_t ret = Run(ctx_a, ctx_b, protocols, version);
  Finalize(ctx_a, ctx_b);
  return ret;
}
