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
constexpr const char *kProtocolHccsDevice = "hccs:device";
constexpr const char *kEngineA = "127.0.0.1:16000";
constexpr const char *kEngineB = "127.0.0.1:16001";
constexpr uint8_t kFillA = 0xAA;
constexpr uint8_t kFillB = 0xBB;
constexpr int32_t kConnectTimeout = 5000;
constexpr int32_t kMaxPollCount = 100000;
static const std::vector<std::string> kValidProtocols = {
    "roce:device", "hccs:device", "roce:host",
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
  int32_t deviceId = 0;
  const char *name = nullptr;
  void *devBuf = nullptr;
  void *hostBuf = nullptr;
  MemHandle devHandle = nullptr;
  MemHandle hostHandle = nullptr;
};

const char *GetRecentErrMsg() {
  const char *errmsg = aclGetRecentErrMsg();
  if (errmsg == nullptr) {
    return "no error";
  }
  return errmsg;
}

int ParseArgs(int32_t argc, char **argv, int32_t &deviceA, int32_t &deviceB,
              std::string &protocol, int32_t &version) {
  for (int32_t i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg.find("--device=") == 0) {
      std::string val = arg.substr(9);
      auto pos = val.find(',');
      if (pos == std::string::npos) {
        printf("[ERROR] Invalid --device format, expected id1,id2\n");
        return -1;
      }
      deviceA = std::stoi(val.substr(0, pos));
      deviceB = std::stoi(val.substr(pos + 1));
    } else if (arg.find("--protocol=") == 0) {
      protocol = arg.substr(11);
    } else if (arg.find("--version=") == 0) {
      version = std::stoi(arg.substr(10));
    } else {
      printf("[ERROR] Unknown argument: %s\n", arg.c_str());
      printf("Usage: %s --protocol=<type> [--device=id1,id2] [--version=0|1]\n", argv[0]);
      return -1;
    }
  }
  if (protocol.empty()) {
    printf("[ERROR] --protocol is required\n");
    printf("Usage: %s --protocol=<type> [--device=id1,id2] [--version=0|1]\n", argv[0]);
    return -1;
  }
  if (protocol == kProtocolHccsDevice) {
    version = kVersionLegacy;
    printf("[INFO] hccs only supports d2rh in relay scenario, "
           "direct transfer only supports h2rd and d2rd, version set to 0\n");
  }
  if (version == kVersionLegacy && protocol != "roce:device" && protocol != kProtocolHccsDevice) {
    printf("[ERROR] version 0 only supports roce:device and hccs:device, got %s\n", protocol.c_str());
    return -1;
  }
  bool valid = false;
  for (const auto &p : kValidProtocols) {
    if (p == protocol) {
      valid = true;
      break;
    }
  }
  if (!valid) {
    printf("[ERROR] Invalid protocol: %s\n", protocol.c_str());
    printf("Supported:");
    for (const auto &p : kValidProtocols) {
      printf(" %s", p.c_str());
    }
    printf("\n");
    return -1;
  }
  printf("[INFO] ParseArgs success: device_a=%d, device_b=%d, protocol=%s, version=%d\n",
         deviceA, deviceB, protocol.c_str(), version);
  return 0;
}

int InitEngine(EngineCtx &ctx, const std::string &protocol, int32_t version, uint8_t fillValue) {
  CHECK_ACL(aclrtSetDevice(ctx.deviceId));
  std::string nameStr(ctx.name);
  uint32_t listenPort = std::stoi(nameStr.substr(nameStr.find(':') + 1));
  std::map<AscendString, AscendString> options;
  if (version == kVersionLegacy) {
    // 老版本流程
    printf("[INFO] %s using legacy flow (version=0)\n", ctx.name);
    std::string localCommRes = "{\"version\": \"1.2\"}";
    options[OPTION_LOCAL_COMM_RES] = localCommRes.c_str();
    std::string resourceConfig =
        "{\"comm_resource_config.listen_port\": " + std::to_string(listenPort) + "}";
    options[OPTION_GLOBAL_RESOURCE_CONFIG] = resourceConfig.c_str();
    if (protocol == "roce:device") {
      options[OPTION_BUFFER_POOL] = "0:0";
      setenv("HCCL_INTRA_ROCE_ENABLE", "1", 1);
    }
  } else {
    std::string resourceConfig =
        "{\"comm_resource_config.protocol_desc\": [\"" + protocol + "\"],"
        "\"comm_resource_config.listen_port\": " + std::to_string(listenPort) + "}";
    options[OPTION_GLOBAL_RESOURCE_CONFIG] = resourceConfig.c_str();
  }
  auto ret = ctx.engine.Initialize(ctx.name, options);
  if (ret != SUCCESS) {
    printf("[ERROR] Initialize %s failed, ret=%u, errmsg:%s\n", ctx.name, ret, GetRecentErrMsg());
    return -1;
  }
  printf("[INFO] InitEngine %s success\n", ctx.name);
  uint8_t *devPtr = nullptr;
  CHECK_ACL(aclrtMalloc(reinterpret_cast<void **>(&devPtr), kBufSize, ACL_MEM_MALLOC_HUGE_ONLY));
  ctx.devBuf = devPtr;
  CHECK_ACL(aclrtMallocHost(&ctx.hostBuf, kBufSize));
  std::memset(ctx.hostBuf, 0, kBufSize);
  MemDesc desc{};
  desc.addr = reinterpret_cast<uintptr_t>(ctx.devBuf);
  desc.len = kBufSize;
  ret = ctx.engine.RegisterMem(desc, MEM_DEVICE, ctx.devHandle);
  if (ret != SUCCESS) {
    printf("[ERROR] %s RegisterMem device failed, ret=%u, errmsg:%s\n", ctx.name, ret, GetRecentErrMsg());
    return -1;
  }
  desc.addr = reinterpret_cast<uintptr_t>(ctx.hostBuf);
  ret = ctx.engine.RegisterMem(desc, MEM_HOST, ctx.hostHandle);
  if (ret != SUCCESS) {
    printf("[ERROR] %s RegisterMem host failed, ret=%u, errmsg:%s\n", ctx.name, ret, GetRecentErrMsg());
    return -1;
  }
  std::memset(ctx.hostBuf, fillValue, kBufSize);
  CHECK_ACL(aclrtMemcpy(ctx.devBuf, kBufSize, ctx.hostBuf, kBufSize, ACL_MEMCPY_HOST_TO_DEVICE));
  printf("[INFO] %s InitEngine success, dev:%p, host:%p\n", ctx.name, ctx.devBuf, ctx.hostBuf);
  return 0;
}

int Connect(EngineCtx &ctxA, EngineCtx &ctxB) {
  auto ret = ctxA.engine.ConnectAsync(ctxB.name, kConnectTimeout);
  if (ret != SUCCESS) {
    printf("[ERROR] ConnectAsync %s->%s failed, ret=%u, errmsg:%s\n", ctxA.name, ctxB.name, ret, GetRecentErrMsg());
    return -1;
  }
  ret = ctxB.engine.ConnectAsync(ctxA.name, kConnectTimeout);
  if (ret != SUCCESS) {
    printf("[ERROR] ConnectAsync %s->%s failed, ret=%u, errmsg:%s\n", ctxB.name, ctxA.name, ret, GetRecentErrMsg());
    return -1;
  }
  AsyncConnectStatus statusA = AsyncConnectStatus::NOT_CONNECT;
  AsyncConnectStatus statusB = AsyncConnectStatus::NOT_CONNECT;
  while (statusA != AsyncConnectStatus::CONNECTED || statusB != AsyncConnectStatus::CONNECTED) {
    ctxA.engine.GetAsyncConnectStatus(ctxB.name, statusA);
    ctxB.engine.GetAsyncConnectStatus(ctxA.name, statusB);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  printf("[INFO] Connect success\n");
  return 0;
}

int Transfer(EngineCtx &ctxA, EngineCtx &ctxB) {
  std::vector<TransferOpDesc> descs;
  descs.reserve(kBlockCount);
  for (size_t i = 0; i < kBlockCount; ++i) {
    TransferOpDesc desc{};
    desc.local_addr = reinterpret_cast<uintptr_t>(ctxA.devBuf) + i * kBlockSize;
    desc.remote_addr = reinterpret_cast<uintptr_t>(ctxB.hostBuf) + i * kBlockSize;
    desc.len = kBlockSize;
    descs.push_back(desc);
  }
  TransferArgs args{};
  TransferReq reqA = nullptr;
  auto ret = ctxA.engine.TransferAsync(ctxB.name, WRITE, descs, args, reqA);
  if (ret != SUCCESS) {
    printf("[ERROR] TransferAsync %s->%s failed, ret=%u, errmsg:%s\n", ctxA.name, ctxB.name, ret, GetRecentErrMsg());
    return -1;
  }
  for (size_t i = 0; i < kBlockCount; ++i) {
    descs[i].local_addr = reinterpret_cast<uintptr_t>(ctxB.devBuf) + i * kBlockSize;
    descs[i].remote_addr = reinterpret_cast<uintptr_t>(ctxA.hostBuf) + i * kBlockSize;
  }
  TransferReq reqB = nullptr;
  ret = ctxB.engine.TransferAsync(ctxA.name, WRITE, descs, args, reqB);
  if (ret != SUCCESS) {
    printf("[ERROR] TransferAsync %s->%s failed, ret=%u, errmsg:%s\n", ctxB.name, ctxA.name, ret, GetRecentErrMsg());
    return -1;
  }
  TransferStatus stA = TransferStatus::WAITING;
  TransferStatus stB = TransferStatus::WAITING;
  int32_t pollCount = 0;
  while (stA == TransferStatus::WAITING || stB == TransferStatus::WAITING) {
    if (stA == TransferStatus::WAITING) {
      ctxA.engine.GetTransferStatus(reqA, stA);
    }
    if (stB == TransferStatus::WAITING) {
      ctxB.engine.GetTransferStatus(reqB, stB);
    }
    if (++pollCount > kMaxPollCount) {
      printf("[ERROR] Transfer poll timeout\n");
      return -1;
    }
    std::this_thread::sleep_for(std::chrono::microseconds(100));
  }
  if (stA != TransferStatus::COMPLETED) {
    printf("[ERROR] Transfer %s->%s failed, status=%d\n", ctxA.name, ctxB.name, static_cast<int>(stA));
    return -1;
  }
  if (stB != TransferStatus::COMPLETED) {
    printf("[ERROR] Transfer %s->%s failed, status=%d\n", ctxB.name, ctxA.name, static_cast<int>(stB));
    return -1;
  }
  printf("[INFO] Transfer completed\n");
  return 0;
}

int Verify(EngineCtx &ctxA, EngineCtx &ctxB) {
  std::vector<uint8_t> expectedA(kBufSize, kFillA);
  std::vector<uint8_t> expectedB(kBufSize, kFillB);
  if (std::memcmp(ctxB.hostBuf, expectedA.data(), kBufSize) != 0) {
    printf("[ERROR] Verify %s host failed, expected 0xAA\n", ctxB.name);
    return -1;
  }
  if (std::memcmp(ctxA.hostBuf, expectedB.data(), kBufSize) != 0) {
    printf("[ERROR] Verify %s host failed, expected 0xBB\n", ctxA.name);
    return -1;
  }
  printf("[INFO] Verify success\n");
  return 0;
}

int Finalize(EngineCtx &ctxA, EngineCtx &ctxB) {
  auto ret = ctxA.engine.DisconnectAsync(ctxB.name, kConnectTimeout);
  if (ret != SUCCESS) {
    printf("[ERROR] DisconnectAsync %s->%s failed, ret=%u, errmsg:%s\n", ctxA.name, ctxB.name, ret, GetRecentErrMsg());
  }
  ret = ctxB.engine.DisconnectAsync(ctxA.name, kConnectTimeout);
  if (ret != SUCCESS) {
    printf("[ERROR] DisconnectAsync %s->%s failed, ret=%u, errmsg:%s\n", ctxB.name, ctxA.name, ret, GetRecentErrMsg());
  }
  AsyncConnectStatus stA = AsyncConnectStatus::DISCONNECT_PENDING;
  AsyncConnectStatus stB = AsyncConnectStatus::DISCONNECT_PENDING;
  while (stA != AsyncConnectStatus::NOT_CONNECT || stB != AsyncConnectStatus::NOT_CONNECT) {
    ctxA.engine.GetAsyncConnectStatus(ctxB.name, stA);
    ctxB.engine.GetAsyncConnectStatus(ctxA.name, stB);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  printf("[INFO] Disconnect success\n");
  ctxA.engine.DeregisterMem(ctxA.devHandle);
  ctxA.engine.DeregisterMem(ctxA.hostHandle);
  ctxB.engine.DeregisterMem(ctxB.devHandle);
  ctxB.engine.DeregisterMem(ctxB.hostHandle);
  printf("[INFO] DeregisterMem success\n");
  CHECK_ACL(aclrtFree(ctxA.devBuf));
  CHECK_ACL(aclrtFreeHost(ctxA.hostBuf));
  CHECK_ACL(aclrtFree(ctxB.devBuf));
  CHECK_ACL(aclrtFreeHost(ctxB.hostBuf));
  printf("[INFO] Free memory success\n");
  ctxA.engine.Finalize();
  ctxB.engine.Finalize();
  printf("[INFO] Finalize success\n");
  CHECK_ACL(aclrtResetDevice(ctxA.deviceId));
  CHECK_ACL(aclrtResetDevice(ctxB.deviceId));
  return 0;
}

int32_t Run(int32_t deviceA, int32_t deviceB, const std::string &protocol, int32_t version) {
  EngineCtx ctxA;
  EngineCtx ctxB;
  ctxA.deviceId = deviceA;
  ctxA.name = kEngineA;
  ctxB.deviceId = deviceB;
  ctxB.name = kEngineB;
  if (InitEngine(ctxA, protocol, version, kFillA) != 0) {
    return -1;
  }
  if (InitEngine(ctxB, protocol, version, kFillB) != 0) {
    return -1;
  }
  if (Connect(ctxA, ctxB) != 0) {
    return -1;
  }
  int32_t result = Transfer(ctxA, ctxB);
  if (result == 0) {
    result = Verify(ctxA, ctxB);
  }
  Finalize(ctxA, ctxB);
  return result;
}
}  // namespace

int main(int32_t argc, char **argv) {
  int32_t deviceA = kDefaultDeviceA;
  int32_t deviceB = kDefaultDeviceB;
  std::string protocol;
  int32_t version = 1;
  if (ParseArgs(argc, argv, deviceA, deviceB, protocol, version) != 0) {
    return -1;
  }
  return Run(deviceA, deviceB, protocol, version);
}
