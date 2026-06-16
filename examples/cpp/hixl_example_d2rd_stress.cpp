/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "acl/acl.h"
#include "hixl/hixl.h"

using namespace hixl;
namespace {
constexpr size_t kXferBufSize = 8 * 1024 * 1024;
constexpr size_t kXferBlockSize = 16 * 1024;
constexpr size_t kXferBlockCount = kXferBufSize / kXferBlockSize;
constexpr int32_t kDefaultDevA = 0;
constexpr int32_t kDefaultDevB = 2;
constexpr int32_t kVersionLegacy = 0;
constexpr uint8_t kFillA = 0xAA;
constexpr uint8_t kFillB = 0xBB;
constexpr int32_t kConnTimeout = 5000;
constexpr int32_t kXferTimeout = 30000;
constexpr const char *kModeBasic = "basic";
constexpr const char *kModeReconnect = "reconnect";
constexpr const char *kMode1k = "1k";
static const std::vector<std::string> kValidProtos = {"roce:device", "roce:host",     "uboe:device",
                                                      "ubg:device",  "ub_ctp:device", "ub_tp:device"};

#define CHECK_ACL(x)                                                                      \
  do {                                                                                    \
    aclError __acl_ret = x;                                                               \
    if (__acl_ret != ACL_ERROR_NONE) {                                                    \
      std::cerr << __FILE__ << ":" << __LINE__ << " aclError:" << __acl_ret << std::endl; \
    }                                                                                     \
  } while (0)

struct EngineCtx {
  Hixl engine;
  int32_t device_id = 0;
  std::string name;
  bool initialized = false;
  bool connected = false;
  void *dev_buf = nullptr;
  MemHandle dev_handle = nullptr;
};

const char *GetRecentErrMsg() {
  const char *msg = aclGetRecentErrMsg();
  return (msg == nullptr) ? "no error" : msg;
}

void SplitProtocolString(const std::string &input, std::vector<std::string> &out) {
  size_t begin = 0;
  size_t comma = input.find(',');
  while (comma != std::string::npos) {
    out.push_back(input.substr(begin, comma - begin));
    begin = comma + 1;
    comma = input.find(',', begin);
  }
  out.push_back(input.substr(begin));
}

int32_t CheckProtocolValidity(const std::string &proto) {
  for (const auto &p : kValidProtos) {
    if (p == proto) return 0;
  }
  printf("[ERROR] Invalid protocol: %s\n", proto.c_str());
  printf("Supported:");
  for (const auto &p : kValidProtos) printf(" %s", p.c_str());
  printf("\n");
  return -1;
}

int32_t ParseArgs(int32_t argc, char **argv, int32_t &device_a, int32_t &device_b, std::vector<std::string> &protocols,
                  int32_t &version, std::string &mode, int32_t &rounds, int32_t &sleep_before, int32_t &sleep_after) {
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
      SplitProtocolString(arg.substr(11), protocols);
    } else if (arg.find("--version=") == 0) {
      version = std::stoi(arg.substr(10));
    } else if (arg.find("--mode=") == 0) {
      mode = arg.substr(7);
    } else if (arg.find("--rounds=") == 0) {
      rounds = std::stoi(arg.substr(9));
    } else if (arg.find("--count=") == 0) {
      rounds = std::stoi(arg.substr(8));
    } else if (arg.find("--sleep-before=") == 0) {
      sleep_before = std::stoi(arg.substr(15));
    } else if (arg.find("--sleep-after=") == 0) {
      sleep_after = std::stoi(arg.substr(14));
    } else {
      printf("[ERROR] Unknown argument: %s\n", arg.c_str());
      printf("Usage: %s --protocol=<type>[,...] [--device=id1,id2] [--version=0|1]\n", argv[0]);
      printf("        [--mode=basic|reconnect|1k] [--rounds=N] [--count=N]\n");
      printf("        [--sleep-before=SEC] [--sleep-after=SEC]\n");
      return -1;
    }
  }

  if (protocols.empty()) {
    printf("[ERROR] --protocol is required\n");
    return -1;
  }
  for (const auto &p : protocols) {
    if (CheckProtocolValidity(p) != 0) return -1;
  }
  if (version == kVersionLegacy && (protocols.size() != 1 || protocols[0] != "roce:device")) {
    printf("[ERROR] version 0 only supports roce:device\n");
    return -1;
  }
  if (mode != kModeBasic && mode != kModeReconnect && mode != kMode1k) {
    printf("[ERROR] Invalid --mode: %s (use basic|reconnect|1k)\n", mode.c_str());
    return -1;
  }

  printf("[INFO] Args: devA=%d, devB=%d, ver=%d, mode=%s, rounds=%d, sleep_before=%ds, sleep_after=%ds\n", device_a,
         device_b, version, mode.c_str(), rounds, sleep_before, sleep_after);
  for (const auto &p : protocols) printf("[INFO]   protocol: %s\n", p.c_str());
  return 0;
}

int32_t ConfigLegacyOptions(EngineCtx &ctx, const std::vector<std::string> &protocols,
                            std::map<AscendString, AscendString> &options) {
  printf("[INFO] %s using legacy flow (version=0)\n", ctx.name.c_str());
  size_t colon_pos = ctx.name.find(':');
  uint32_t port = std::stoi(ctx.name.substr(colon_pos + 1));
  std::string comm_res = "{\"version\": \"1.2\"}";
  options[OPTION_LOCAL_COMM_RES] = comm_res.c_str();
  std::string config = "{\"comm_resource_config.listen_port\": " + std::to_string(port) + "}";
  options[OPTION_GLOBAL_RESOURCE_CONFIG] = config.c_str();
  options[OPTION_BUFFER_POOL] = "0:0";
  if (protocols[0] == "roce:device") {
    setenv("HCCL_INTRA_ROCE_ENABLE", "1", 1);
  }
  return 0;
}

int32_t ConfigV2Options(const std::vector<std::string> &protocols, std::map<AscendString, AscendString> &options) {
  std::string desc_array;
  for (size_t i = 0; i < protocols.size(); ++i) {
    if (i > 0) desc_array += ",";
    desc_array += "\"" + protocols[i] + "\"";
  }
  std::string config = "{\"comm_resource_config.protocol_desc\": [" + desc_array + "]}";
  options[OPTION_GLOBAL_RESOURCE_CONFIG] = config.c_str();
  return 0;
}

int32_t InitEngine(EngineCtx &ctx, const std::vector<std::string> &protocols, int32_t version, uint8_t fill) {
  CHECK_ACL(aclrtSetDevice(ctx.device_id));
  std::map<AscendString, AscendString> options;
  if (version == kVersionLegacy) {
    ConfigLegacyOptions(ctx, protocols, options);
  } else {
    ConfigV2Options(protocols, options);
  }
  auto ret = ctx.engine.Initialize(ctx.name.c_str(), options);
  if (ret != SUCCESS) {
    printf("[ERROR] Initialize %s failed, ret=%u, errmsg:%s\n", ctx.name.c_str(), ret, GetRecentErrMsg());
    return -1;
  }
  ctx.initialized = true;

  uint8_t *dev_ptr = nullptr;
  CHECK_ACL(aclrtMalloc(reinterpret_cast<void **>(&dev_ptr), kXferBufSize, ACL_MEM_MALLOC_HUGE_ONLY));
  ctx.dev_buf = dev_ptr;
  MemDesc desc{};
  desc.addr = reinterpret_cast<uintptr_t>(ctx.dev_buf);
  desc.len = kXferBufSize;
  ret = ctx.engine.RegisterMem(desc, MEM_DEVICE, ctx.dev_handle);
  if (ret != SUCCESS) {
    printf("[ERROR] %s RegisterMem failed, ret=%u, errmsg:%s\n", ctx.name.c_str(), ret, GetRecentErrMsg());
    return -1;
  }

  std::vector<uint8_t> host_tmp(kXferBufSize, fill);
  CHECK_ACL(aclrtMemcpy(ctx.dev_buf, kXferBufSize, host_tmp.data(), kXferBufSize, ACL_MEMCPY_HOST_TO_DEVICE));
  printf("[INFO] InitEngine %s success, dev:%p\n", ctx.name.c_str(), ctx.dev_buf);
  return 0;
}

int32_t ConnectEngines(EngineCtx &ctx_a, EngineCtx &ctx_b) {
  auto ret = ctx_a.engine.Connect(ctx_b.name.c_str(), kConnTimeout);
  if (ret != SUCCESS) {
    printf("[ERROR] Connect %s->%s failed, ret=%u, errmsg:%s\n", ctx_a.name.c_str(), ctx_b.name.c_str(), ret,
           GetRecentErrMsg());
    return -1;
  }
  ctx_a.connected = true;
  printf("[INFO] Connect %s->%s success\n", ctx_a.name.c_str(), ctx_b.name.c_str());
  return 0;
}

int32_t DisconnectEngines(EngineCtx &ctx_a, EngineCtx &ctx_b) {
  auto ret = ctx_a.engine.Disconnect(ctx_b.name.c_str(), kConnTimeout);
  if (ret != SUCCESS) {
    printf("[ERROR] Disconnect %s->%s failed, ret=%u, errmsg:%s\n", ctx_a.name.c_str(), ctx_b.name.c_str(), ret,
           GetRecentErrMsg());
    return -1;
  }
  ctx_a.connected = false;
  printf("[INFO] Disconnect %s->%s success\n", ctx_a.name.c_str(), ctx_b.name.c_str());
  return 0;
}

int32_t Transfer(EngineCtx &ctx_a, EngineCtx &ctx_b) {
  std::vector<TransferOpDesc> descs;
  descs.reserve(kXferBlockCount);
  for (size_t i = 0; i < kXferBlockCount; ++i) {
    TransferOpDesc desc{};
    desc.local_addr = reinterpret_cast<uintptr_t>(ctx_a.dev_buf) + i * kXferBlockSize;
    desc.remote_addr = reinterpret_cast<uintptr_t>(ctx_b.dev_buf) + i * kXferBlockSize;
    desc.len = kXferBlockSize;
    descs.push_back(desc);
  }
  auto ret = ctx_a.engine.TransferSync(ctx_b.name.c_str(), WRITE, descs, kXferTimeout);
  if (ret != SUCCESS) {
    printf("[ERROR] TransferSync %s->%s failed, ret=%u, errmsg:%s\n", ctx_a.name.c_str(), ctx_b.name.c_str(), ret,
           GetRecentErrMsg());
    return -1;
  }
  printf("[INFO] Transfer %s->%s success\n", ctx_a.name.c_str(), ctx_b.name.c_str());
  return 0;
}

int32_t Verify(EngineCtx &ctx_b, const std::vector<uint8_t> &expected) {
  void *host_tmp = nullptr;
  CHECK_ACL(aclrtMallocHost(&host_tmp, kXferBufSize));
  CHECK_ACL(aclrtMemcpy(host_tmp, kXferBufSize, ctx_b.dev_buf, kXferBufSize, ACL_MEMCPY_DEVICE_TO_HOST));
  int result = std::memcmp(host_tmp, expected.data(), kXferBufSize);
  CHECK_ACL(aclrtFreeHost(host_tmp));
  if (result != 0) {
    printf("[ERROR] Verify %s failed, data mismatch\n", ctx_b.name.c_str());
    return -1;
  }
  printf("[INFO] Verify %s success\n", ctx_b.name.c_str());
  return 0;
}

void CleanupEngine(EngineCtx &ctx) {
  if (ctx.dev_handle != nullptr) {
    ctx.engine.DeregisterMem(ctx.dev_handle);
    ctx.dev_handle = nullptr;
  }
  if (ctx.dev_buf != nullptr) {
    CHECK_ACL(aclrtFree(ctx.dev_buf));
    ctx.dev_buf = nullptr;
  }
  if (ctx.initialized) {
    ctx.engine.Finalize();
    ctx.initialized = false;
  }
}

void Finalize(EngineCtx &ctx_a, EngineCtx &ctx_b) {
  if (ctx_a.connected) {
    auto ret = ctx_a.engine.Disconnect(ctx_b.name.c_str(), kConnTimeout);
    if (ret != SUCCESS) {
      printf("[ERROR] Disconnect %s->%s failed, ret=%u, errmsg:%s\n", ctx_a.name.c_str(), ctx_b.name.c_str(), ret,
             GetRecentErrMsg());
    }
    ctx_a.connected = false;
  }
  CleanupEngine(ctx_a);
  CleanupEngine(ctx_b);
  CHECK_ACL(aclrtResetDevice(ctx_a.device_id));
  CHECK_ACL(aclrtResetDevice(ctx_b.device_id));
}

// ---- Test: basic single transfer ----
int32_t TestBasic(EngineCtx &ctx_a, EngineCtx &ctx_b, const std::vector<std::string> &protocols, int32_t version) {
  if (InitEngine(ctx_a, protocols, version, kFillA) != 0) return -1;
  if (InitEngine(ctx_b, protocols, version, kFillB) != 0) return -1;
  if (ConnectEngines(ctx_a, ctx_b) != 0) return -1;
  if (Transfer(ctx_a, ctx_b) != 0) return -1;
  std::vector<uint8_t> expected(kXferBufSize, kFillA);
  int32_t ret = Verify(ctx_b, expected);
  Finalize(ctx_a, ctx_b);
  return ret;
}

// ---- Test: reconnect cycles (disconnect -> reconnect -> transfer) ----
int32_t TestReconnect(EngineCtx &ctx_a, EngineCtx &ctx_b, const std::vector<std::string> &protocols, int32_t version,
                      int32_t rounds) {
  if (InitEngine(ctx_a, protocols, version, kFillA) != 0) return -1;
  if (InitEngine(ctx_b, protocols, version, kFillB) != 0) return -1;
  std::vector<uint8_t> expected(kXferBufSize, kFillA);
  for (int32_t i = 0; i < rounds; ++i) {
    printf("\n==== Reconnect round %d/%d ====\n", i + 1, rounds);
    if (ConnectEngines(ctx_a, ctx_b) != 0) break;
    if (Transfer(ctx_a, ctx_b) != 0) {
      printf("[WARN] Transfer failed in round %d, continuing disconnect\n", i + 1);
    }
    if (Verify(ctx_b, expected) != 0) {
      printf("[WARN] Verify failed in round %d\n", i + 1);
    }
    if (DisconnectEngines(ctx_a, ctx_b) != 0) break;
  }
  Finalize(ctx_a, ctx_b);
  printf("[INFO] TestReconnect completed\n");
  return 0;
}

// ---- Test: create N connections to same engine, one transfer for verification ----
// Phase 1: InitEngine once
// Phase 2: sleep-before (memory after init, before connections)
// Phase 3: N x Connect (no disconnect), then one transfer
// Phase 4: sleep-after (memory observation before cleanup)
int32_t Test1k(EngineCtx &ctx_a, EngineCtx &ctx_b, const std::vector<std::string> &protocols, int32_t version,
               int32_t count, int32_t sleep_before, int32_t sleep_after) {
  if (InitEngine(ctx_a, protocols, version, kFillA) != 0) return -1;
  if (InitEngine(ctx_b, protocols, version, kFillB) != 0) return -1;

  if (sleep_before > 0) {
    printf("[INFO] Sleeping %ds after InitEngine, before connections (check memory)\n", sleep_before);
    sleep(static_cast<uint32_t>(sleep_before));
  }

  printf("[INFO] Creating %d connections to same engine\n", count);
  for (int32_t i = 0; i < count; ++i) {
    printf("[INFO] ---- Connect %d/%d ----\n", i + 1, count);
    if (ConnectEngines(ctx_a, ctx_b) != 0) {
      printf("[ERROR] Connect failed at %d\n", i + 1);
      break;
    }
  }

  if (sleep_after > 0) {
    printf("[INFO] All %d connections created. Sleeping %ds (check memory before transfer)\n", count, sleep_after);
    sleep(static_cast<uint32_t>(sleep_after));
  }

  std::vector<uint8_t> expected(kXferBufSize, kFillA);
  if (Transfer(ctx_a, ctx_b) != 0) {
    printf("[WARN] Transfer failed\n");
  } else {
    Verify(ctx_b, expected);
  }

  Finalize(ctx_a, ctx_b);
  printf("[INFO] Test1k completed: %d connections\n", count);
  return 0;
}

}  // namespace

int main(int32_t argc, char **argv) {
  int32_t device_a = kDefaultDevA;
  int32_t device_b = kDefaultDevB;
  std::vector<std::string> protocols;
  int32_t version = 1;
  std::string mode = kModeBasic;
  int32_t rounds = 3;
  int32_t sleep_before = 0;
  int32_t sleep_after = 0;
  if (ParseArgs(argc, argv, device_a, device_b, protocols, version, mode, rounds, sleep_before, sleep_after) != 0) {
    return -1;
  }

  EngineCtx ctx_a;
  EngineCtx ctx_b;
  ctx_a.device_id = device_a;
  ctx_b.device_id = device_b;
  ctx_a.name = "127.0.0.1:16000";
  ctx_b.name = "127.0.0.1:16001";

  if (mode == kModeBasic) {
    return TestBasic(ctx_a, ctx_b, protocols, version);
  }
  if (mode == kModeReconnect) {
    return TestReconnect(ctx_a, ctx_b, protocols, version, rounds);
  }
  return Test1k(ctx_a, ctx_b, protocols, version, rounds, sleep_before, sleep_after);
}
