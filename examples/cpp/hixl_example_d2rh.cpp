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
  int32_t device_id = 0;
  const char *name = nullptr;
  void *dev_buf = nullptr;
  void *host_buf = nullptr;
  MemHandle dev_handle = nullptr;
  MemHandle host_handle = nullptr;
};

const char *get_recent_err_msg() {
  const char *errmsg = aclGetRecentErrMsg();
  if (errmsg == nullptr) {
    return "no error";
  }
  return errmsg;
}

int parse_args(int32_t argc, char **argv, int32_t &device_a, int32_t &device_b,
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
      device_a = std::stoi(val.substr(0, pos));
      device_b = std::stoi(val.substr(pos + 1));
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
  printf("[INFO] parse_args success: device_a=%d, device_b=%d, protocol=%s, version=%d\n",
         device_a, device_b, protocol.c_str(), version);
  return 0;
}

int init_engine(EngineCtx &ctx, const std::string &protocol, int32_t version, uint8_t fill_value) {
  CHECK_ACL(aclrtSetDevice(ctx.device_id));
  std::map<AscendString, AscendString> options;
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
    if (protocol == "roce:device") {
      options[OPTION_BUFFER_POOL] = "0:0";
      setenv("HCCL_INTRA_ROCE_ENABLE", "1", 1);
    }
  } else {
    std::string name_str(ctx.name);
    uint32_t listen_port = std::stoi(name_str.substr(name_str.find(':') + 1));
    std::string resource_config =
        "{\"comm_resource_config.protocol_desc\": [\"" + protocol + "\"],"
        "\"comm_resource_config.listen_port\": " + std::to_string(listen_port) + "}";
    options[OPTION_GLOBAL_RESOURCE_CONFIG] = resource_config.c_str();
  }
  auto ret = ctx.engine.Initialize(ctx.name, options);
  if (ret != SUCCESS) {
    printf("[ERROR] Initialize %s failed, ret=%u, errmsg:%s\n", ctx.name, ret, get_recent_err_msg());
    return -1;
  }
  printf("[INFO] init_engine %s success\n", ctx.name);
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
    printf("[ERROR] %s RegisterMem device failed, ret=%u, errmsg:%s\n", ctx.name, ret, get_recent_err_msg());
    return -1;
  }
  desc.addr = reinterpret_cast<uintptr_t>(ctx.host_buf);
  ret = ctx.engine.RegisterMem(desc, MEM_HOST, ctx.host_handle);
  if (ret != SUCCESS) {
    printf("[ERROR] %s RegisterMem host failed, ret=%u, errmsg:%s\n", ctx.name, ret, get_recent_err_msg());
    return -1;
  }
  std::memset(ctx.host_buf, fill_value, kBufSize);
  CHECK_ACL(aclrtMemcpy(ctx.dev_buf, kBufSize, ctx.host_buf, kBufSize, ACL_MEMCPY_HOST_TO_DEVICE));
  printf("[INFO] %s init_engine success, dev:%p, host:%p\n", ctx.name, ctx.dev_buf, ctx.host_buf);
  return 0;
}

int connect(EngineCtx &ctx_a, EngineCtx &ctx_b) {
  auto ret = ctx_a.engine.ConnectAsync(ctx_b.name, kConnectTimeout);
  if (ret != SUCCESS) {
    printf("[ERROR] ConnectAsync %s->%s failed, ret=%u, errmsg:%s\n",
           ctx_a.name, ctx_b.name, ret, get_recent_err_msg());
    return -1;
  }
  ret = ctx_b.engine.ConnectAsync(ctx_a.name, kConnectTimeout);
  if (ret != SUCCESS) {
    printf("[ERROR] ConnectAsync %s->%s failed, ret=%u, errmsg:%s\n",
           ctx_b.name, ctx_a.name, ret, get_recent_err_msg());
    return -1;
  }
  AsyncConnectStatus status_a = AsyncConnectStatus::NOT_CONNECT;
  AsyncConnectStatus status_b = AsyncConnectStatus::NOT_CONNECT;
  while (status_a != AsyncConnectStatus::CONNECTED || status_b != AsyncConnectStatus::CONNECTED) {
    ctx_a.engine.GetAsyncConnectStatus(ctx_b.name, status_a);
    ctx_b.engine.GetAsyncConnectStatus(ctx_a.name, status_b);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  printf("[INFO] connect success\n");
  return 0;
}

int transfer(EngineCtx &ctx_a, EngineCtx &ctx_b) {
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
           ctx_a.name, ctx_b.name, ret, get_recent_err_msg());
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
           ctx_b.name, ctx_a.name, ret, get_recent_err_msg());
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
      printf("[ERROR] transfer poll timeout\n");
      return -1;
    }
    std::this_thread::sleep_for(std::chrono::microseconds(100));
  }
  if (st_a != TransferStatus::COMPLETED) {
    printf("[ERROR] transfer %s->%s failed, status=%d\n", ctx_a.name, ctx_b.name, static_cast<int>(st_a));
    return -1;
  }
  if (st_b != TransferStatus::COMPLETED) {
    printf("[ERROR] transfer %s->%s failed, status=%d\n", ctx_b.name, ctx_a.name, static_cast<int>(st_b));
    return -1;
  }
  printf("[INFO] transfer completed\n");
  return 0;
}

int verify(EngineCtx &ctx_a, EngineCtx &ctx_b) {
  std::vector<uint8_t> expected_a(kBufSize, kFillA);
  std::vector<uint8_t> expected_b(kBufSize, kFillB);
  if (std::memcmp(ctx_b.host_buf, expected_a.data(), kBufSize) != 0) {
    printf("[ERROR] verify %s host failed, expected 0xAA\n", ctx_b.name);
    return -1;
  }
  if (std::memcmp(ctx_a.host_buf, expected_b.data(), kBufSize) != 0) {
    printf("[ERROR] verify %s host failed, expected 0xBB\n", ctx_a.name);
    return -1;
  }
  printf("[INFO] verify success\n");
  return 0;
}

int finalize(EngineCtx &ctx_a, EngineCtx &ctx_b) {
  auto ret = ctx_a.engine.DisconnectAsync(ctx_b.name, kConnectTimeout);
  if (ret != SUCCESS) {
    printf("[ERROR] DisconnectAsync %s->%s failed, ret=%u, errmsg:%s\n",
           ctx_a.name, ctx_b.name, ret, get_recent_err_msg());
  }
  ret = ctx_b.engine.DisconnectAsync(ctx_a.name, kConnectTimeout);
  if (ret != SUCCESS) {
    printf("[ERROR] DisconnectAsync %s->%s failed, ret=%u, errmsg:%s\n",
           ctx_b.name, ctx_a.name, ret, get_recent_err_msg());
  }
  AsyncConnectStatus st_a = AsyncConnectStatus::DISCONNECT_PENDING;
  AsyncConnectStatus st_b = AsyncConnectStatus::DISCONNECT_PENDING;
  while (st_a != AsyncConnectStatus::NOT_CONNECT || st_b != AsyncConnectStatus::NOT_CONNECT) {
    ctx_a.engine.GetAsyncConnectStatus(ctx_b.name, st_a);
    ctx_b.engine.GetAsyncConnectStatus(ctx_a.name, st_b);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  printf("[INFO] disconnect success\n");
  ctx_a.engine.DeregisterMem(ctx_a.dev_handle);
  ctx_a.engine.DeregisterMem(ctx_a.host_handle);
  ctx_b.engine.DeregisterMem(ctx_b.dev_handle);
  ctx_b.engine.DeregisterMem(ctx_b.host_handle);
  printf("[INFO] DeregisterMem success\n");
  CHECK_ACL(aclrtFree(ctx_a.dev_buf));
  CHECK_ACL(aclrtFreeHost(ctx_a.host_buf));
  CHECK_ACL(aclrtFree(ctx_b.dev_buf));
  CHECK_ACL(aclrtFreeHost(ctx_b.host_buf));
  printf("[INFO] free memory success\n");
  ctx_a.engine.Finalize();
  ctx_b.engine.Finalize();
  printf("[INFO] finalize success\n");
  CHECK_ACL(aclrtResetDevice(ctx_a.device_id));
  CHECK_ACL(aclrtResetDevice(ctx_b.device_id));
  return 0;
}

int32_t run(int32_t device_a, int32_t device_b, const std::string &protocol, int32_t version) {
  EngineCtx ctx_a;
  EngineCtx ctx_b;
  ctx_a.device_id = device_a;
  ctx_a.name = kEngineA;
  ctx_b.device_id = device_b;
  ctx_b.name = kEngineB;
  if (init_engine(ctx_a, protocol, version, kFillA) != 0) {
    return -1;
  }
  if (init_engine(ctx_b, protocol, version, kFillB) != 0) {
    return -1;
  }
  if (connect(ctx_a, ctx_b) != 0) {
    return -1;
  }
  int32_t result = transfer(ctx_a, ctx_b);
  if (result == 0) {
    result = verify(ctx_a, ctx_b);
  }
  finalize(ctx_a, ctx_b);
  return result;
}
}  // namespace

int main(int32_t argc, char **argv) {
  int32_t device_a = kDefaultDeviceA;
  int32_t device_b = kDefaultDeviceB;
  std::string protocol;
  int32_t version = 1;
  if (parse_args(argc, argv, device_a, device_b, protocol, version) != 0) {
    return -1;
  }
  return run(device_a, device_b, protocol, version);
}
