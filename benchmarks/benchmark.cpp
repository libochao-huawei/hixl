/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
#include "common/tcp_client_server.h"
#include "acl/acl.h"
#include "hixl/hixl.h"

using namespace hixl;

namespace {

constexpr int32_t kWaitTransTime = 20;
constexpr int32_t kPortMaxValue = 65535;

constexpr uint64_t kDefaultTotalSize = 134217728ULL;  // 128 MiB
constexpr uint32_t kDefaultBlockSteps = 1U;
constexpr uint32_t kDefaultLoops = 1U;

#define CHECK_ACL_RETURN(x)                                                                  \
  do {                                                                                       \
    aclError __ret = x;                                                                      \
    if (__ret != ACL_ERROR_NONE) {                                                         \
      std::cerr << __FILE__ << ":" << __LINE__ << " aclError:" << __ret << std::endl;        \
      return __ret;                                                                        \
    }                                                                                      \
  } while (0)

enum class Role { kUnknown, kClient, kServer };

struct BenchmarkConfig {
  Role role = Role::kUnknown;
  int32_t device_id = 0;
  std::string local_engine;
  std::string remote_engine;
  uint16_t tcp_port = 20000;
  std::string transfer_mode = "d2d";
  std::string transfer_op = "read";
  bool use_buffer_pool = false;
  uint64_t total_size = kDefaultTotalSize;
  uint64_t block_size = kDefaultTotalSize;
  uint32_t block_steps = kDefaultBlockSteps;
  uint32_t loops = kDefaultLoops;
  /// True if --block_size / -k was set on the command line.
  bool block_size_explicit = false;
  /// Parsed from repeatable --hixl_option=KEY=VALUE / -H=KEY=VALUE (KEY is full HIXL option name).
  std::map<std::string, std::string> hixl_init_options;
};

const char *GetRecentErrMsg() {
  const char *errmsg = aclGetRecentErrMsg();
  if (errmsg == nullptr) {
    return "no error";
  }
  return errmsg;
}

void PrintUsage(FILE *out) {
  fprintf(out,
          "Usage: benchmark key=value ...\n"
          "Required: --role=client|server  or  -r=client|server\n"
          "Keys (all lowercase, value is decimal bytes where noted):\n"
          "  --role|-r            client|server\n"
          "  --device_id|-d       device id (int)\n"
          "  --local_engine|-l    local HIXL engine endpoint\n"
          "  --remote_engine|-e   client: remote HIXL; server: TCP peer host (IP only)\n"
          "  --tcp_port|-p        TCP coordination port\n"
          "  --transfer_mode|-m   d2d|h2d|d2h|h2h\n"
          "  --transfer_op|-o     read|write\n"
          "  --use_buffer_pool|-b true|false\n"
          "  --total_size|-t      total buffer size in bytes (default %" PRIu64 ")\n"
          "  --block_size|-k      first block size in bytes (default: same as total_size)\n"
          "  --block_steps|-s     block count: sizes are block_size<<i, i in [0,steps-1] (default %u)\n"
          "  --loops|-n           repeat full step ladder (default %u)\n"
          "  --hixl_option|-H     HIXL Initialize() option, form KEY=VALUE (repeatable); "
          "KEY e.g. LocalCommRes, BufferPool, RdmaTrafficClass, RdmaServiceLevel, adxl.*\n"
          "                       If neither BufferPool nor adxl.BufferPool is set and "
          "--use_buffer_pool=false, BufferPool=0:0 is added (legacy).\n"
          "Defaults: client device_id=0 local_engine=127.0.0.1:16000 remote_engine=127.0.0.1:16001\n"
          "          server device_id=1 local_engine=127.0.0.1:16001 remote_engine=127.0.0.1 (TCP host)\n"
          "          transfer_mode=d2d transfer_op=read use_buffer_pool=false tcp_port=20000\n",
          kDefaultTotalSize, kDefaultBlockSteps, kDefaultLoops);
}

bool ParseBool(const std::string &value, bool *out) {
  if (value == "true" || value == "1") {
    *out = true;
    return true;
  }
  if (value == "false" || value == "0") {
    *out = false;
    return true;
  }
  return false;
}

bool ParseU64(const std::string &value, uint64_t *out) {
  if (value.empty()) {
    return false;
  }
  try {
    std::size_t pos = 0;
    const unsigned long long v = std::stoull(value, &pos, 10);
    if (pos != value.size()) {
      return false;
    }
    *out = static_cast<uint64_t>(v);
    return true;
  } catch (const std::invalid_argument &) {
    return false;
  } catch (const std::out_of_range &) {
    return false;
  }
}

bool ParseU32(const std::string &value, uint32_t *out) {
  uint64_t v = 0;
  if (!ParseU64(value, &v) || v > static_cast<uint64_t>(UINT32_MAX)) {
    return false;
  }
  *out = static_cast<uint32_t>(v);
  return true;
}

bool ParseI32(const std::string &value, int32_t *out) {
  if (value.empty()) {
    return false;
  }
  try {
    std::size_t pos = 0;
    const long long v = std::stoll(value, &pos, 10);
    if (pos != value.size()) {
      return false;
    }
    if (v < static_cast<long long>(INT32_MIN) || v > static_cast<long long>(INT32_MAX)) {
      return false;
    }
    *out = static_cast<int32_t>(v);
    return true;
  } catch (const std::invalid_argument &) {
    return false;
  } catch (const std::out_of_range &) {
    return false;
  }
}

bool CollectArgs(int argc, char **argv, std::map<std::string, std::string> *kv,
                 std::vector<std::string> *hixl_option_payloads) {
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "-h" || arg == "--help") {
      PrintUsage(stdout);
      std::exit(0);
    }
    auto pos = arg.find('=');
    if (pos == std::string::npos) {
      fprintf(stderr, "[ERROR] Expected key=value, got: %s\n", arg.c_str());
      return false;
    }
    std::string key = arg.substr(0, pos);
    std::string val = arg.substr(pos + 1);
    if (key == "--hixl_option" || key == "-H") {
      hixl_option_payloads->push_back(val);
    } else {
      (*kv)[key] = val;
    }
  }
  return true;
}

void ApplyRoleDefaults(BenchmarkConfig *cfg) {
  if (cfg->role == Role::kClient) {
    cfg->device_id = 0;
    cfg->local_engine = "127.0.0.1:16000";
    cfg->remote_engine = "127.0.0.1:16001";
  } else if (cfg->role == Role::kServer) {
    cfg->device_id = 1;
    cfg->local_engine = "127.0.0.1:16001";
    cfg->remote_engine = "127.0.0.1";
  }
}

void ApplyCommonDefaults(BenchmarkConfig *cfg) {
  cfg->tcp_port = 20000;
  cfg->transfer_mode = "d2d";
  cfg->transfer_op = "read";
  cfg->use_buffer_pool = false;
  cfg->total_size = kDefaultTotalSize;
  cfg->block_size = kDefaultTotalSize;
  cfg->block_steps = kDefaultBlockSteps;
  cfg->loops = kDefaultLoops;
}

using KvApplyFn = bool (*)(const std::string &val, BenchmarkConfig *cfg);

bool ApplyRoleKv(const std::string &val, BenchmarkConfig *cfg) {
  if (val == "client") {
    cfg->role = Role::kClient;
    return true;
  }
  if (val == "server") {
    cfg->role = Role::kServer;
    return true;
  }
  fprintf(stderr, "[ERROR] Invalid --role=%s (expect client|server)\n", val.c_str());
  return false;
}

bool ApplyDeviceIdKv(const std::string &val, BenchmarkConfig *cfg) {
  if (!ParseI32(val, &cfg->device_id)) {
    fprintf(stderr, "[ERROR] Invalid --device_id=%s\n", val.c_str());
    return false;
  }
  return true;
}

bool ApplyLocalEngineKv(const std::string &val, BenchmarkConfig *cfg) {
  cfg->local_engine = val;
  return true;
}

bool ApplyRemoteEngineKv(const std::string &val, BenchmarkConfig *cfg) {
  cfg->remote_engine = val;
  return true;
}

bool ApplyTcpPortKv(const std::string &val, BenchmarkConfig *cfg) {
  int32_t port = 0;
  if (!ParseI32(val, &port) || port < 0 || port > kPortMaxValue) {
    fprintf(stderr, "[ERROR] Invalid --tcp_port=%s\n", val.c_str());
    return false;
  }
  cfg->tcp_port = static_cast<uint16_t>(port);
  return true;
}

bool ApplyTransferModeKv(const std::string &val, BenchmarkConfig *cfg) {
  cfg->transfer_mode = val;
  return true;
}

bool ApplyTransferOpKv(const std::string &val, BenchmarkConfig *cfg) {
  cfg->transfer_op = val;
  return true;
}

bool ApplyUseBufferPoolKv(const std::string &val, BenchmarkConfig *cfg) {
  if (!ParseBool(val, &cfg->use_buffer_pool)) {
    fprintf(stderr, "[ERROR] Invalid --use_buffer_pool=%s (expect true|false|0|1)\n", val.c_str());
    return false;
  }
  return true;
}

bool ApplyTotalSizeKv(const std::string &val, BenchmarkConfig *cfg) {
  if (!ParseU64(val, &cfg->total_size) || cfg->total_size == 0) {
    fprintf(stderr, "[ERROR] Invalid --total_size=%s\n", val.c_str());
    return false;
  }
  return true;
}

bool ApplyBlockSizeKv(const std::string &val, BenchmarkConfig *cfg) {
  if (!ParseU64(val, &cfg->block_size) || cfg->block_size == 0) {
    fprintf(stderr, "[ERROR] Invalid --block_size=%s\n", val.c_str());
    return false;
  }
  cfg->block_size_explicit = true;
  return true;
}

bool ApplyBlockStepsKv(const std::string &val, BenchmarkConfig *cfg) {
  if (!ParseU32(val, &cfg->block_steps) || cfg->block_steps == 0) {
    fprintf(stderr, "[ERROR] Invalid --block_steps=%s\n", val.c_str());
    return false;
  }
  return true;
}

bool ApplyLoopsKv(const std::string &val, BenchmarkConfig *cfg) {
  if (!ParseU32(val, &cfg->loops) || cfg->loops == 0) {
    fprintf(stderr, "[ERROR] Invalid --loops=%s\n", val.c_str());
    return false;
  }
  return true;
}

const std::map<std::string, KvApplyFn> &KvHandlerTable() {
  static const std::map<std::string, KvApplyFn> kTable = {
      {"--role", ApplyRoleKv},
      {"-r", ApplyRoleKv},
      {"--device_id", ApplyDeviceIdKv},
      {"-d", ApplyDeviceIdKv},
      {"--local_engine", ApplyLocalEngineKv},
      {"-l", ApplyLocalEngineKv},
      {"--remote_engine", ApplyRemoteEngineKv},
      {"-e", ApplyRemoteEngineKv},
      {"--tcp_port", ApplyTcpPortKv},
      {"-p", ApplyTcpPortKv},
      {"--transfer_mode", ApplyTransferModeKv},
      {"-m", ApplyTransferModeKv},
      {"--transfer_op", ApplyTransferOpKv},
      {"-o", ApplyTransferOpKv},
      {"--use_buffer_pool", ApplyUseBufferPoolKv},
      {"-b", ApplyUseBufferPoolKv},
      {"--total_size", ApplyTotalSizeKv},
      {"-t", ApplyTotalSizeKv},
      {"--block_size", ApplyBlockSizeKv},
      {"-k", ApplyBlockSizeKv},
      {"--block_steps", ApplyBlockStepsKv},
      {"-s", ApplyBlockStepsKv},
      {"--loops", ApplyLoopsKv},
      {"-n", ApplyLoopsKv},
  };
  return kTable;
}

bool ApplyKvOverrides(const std::map<std::string, std::string> &kv, BenchmarkConfig *cfg) {
  const auto &table = KvHandlerTable();
  for (const auto &entry : kv) {
    const auto it = table.find(entry.first);
    if (it == table.end()) {
      fprintf(stderr, "[ERROR] Unknown option: %s\n", entry.first.c_str());
      return false;
    }
    if (!it->second(entry.second, cfg)) {
      return false;
    }
  }
  return true;
}

bool ParseHixlOptionPayloads(const std::vector<std::string> &payloads, BenchmarkConfig *cfg) {
  for (const std::string &payload : payloads) {
    const auto inner_eq = payload.find('=');
    if (inner_eq == std::string::npos || inner_eq == 0U) {
      fprintf(stderr,
              "[ERROR] Invalid --hixl_option value %s (expect KEY=VALUE with KEY non-empty)\n",
              payload.c_str());
      return false;
    }
    const std::string opt_key = payload.substr(0, inner_eq);
    const std::string opt_val = payload.substr(inner_eq + 1);
    cfg->hixl_init_options[opt_key] = opt_val;
  }
  return true;
}

std::map<AscendString, AscendString> BuildInitializeOptions(const BenchmarkConfig &cfg) {
  std::map<AscendString, AscendString> options;
  for (const auto &entry : cfg.hixl_init_options) {
    options[AscendString(entry.first.c_str())] = AscendString(entry.second.c_str());
  }
  constexpr const char kAdxlBufferPoolKey[] = "adxl.BufferPool";
  const bool has_explicit_buffer =
      cfg.hixl_init_options.find(OPTION_BUFFER_POOL) != cfg.hixl_init_options.cend() ||
      cfg.hixl_init_options.find(kAdxlBufferPoolKey) != cfg.hixl_init_options.cend();
  if (!has_explicit_buffer && !cfg.use_buffer_pool) {
    options[AscendString(OPTION_BUFFER_POOL)] = AscendString("0:0");
  }
  return options;
}

bool BuildConfig(int argc, char **argv, BenchmarkConfig *cfg) {
  std::map<std::string, std::string> kv;
  std::vector<std::string> hixl_option_payloads;
  if (!CollectArgs(argc, argv, &kv, &hixl_option_payloads)) {
    return false;
  }
  auto it_role = kv.find("--role");
  if (it_role == kv.end()) {
    it_role = kv.find("-r");
  }
  if (it_role == kv.end()) {
    fprintf(stderr, "[ERROR] Missing required --role=client|server\n");
    PrintUsage(stderr);
    return false;
  }
  const std::string &rv = it_role->second;
  if (rv == "client") {
    cfg->role = Role::kClient;
  } else if (rv == "server") {
    cfg->role = Role::kServer;
  } else {
    fprintf(stderr, "[ERROR] Invalid --role=%s\n", rv.c_str());
    return false;
  }
  ApplyCommonDefaults(cfg);
  ApplyRoleDefaults(cfg);
  if (!ApplyKvOverrides(kv, cfg)) {
    return false;
  }
  if (!ParseHixlOptionPayloads(hixl_option_payloads, cfg)) {
    return false;
  }
  if (!cfg->block_size_explicit) {
    cfg->block_size = cfg->total_size;
  }
  return true;
}

bool ValidateConfig(const BenchmarkConfig &cfg) {
  if (cfg.local_engine.empty()) {
    fprintf(stderr, "[ERROR] local_engine is empty\n");
    return false;
  }
  if (cfg.remote_engine.empty()) {
    fprintf(stderr, "[ERROR] remote_engine is empty\n");
    return false;
  }
  if (cfg.transfer_mode != "d2d" && cfg.transfer_mode != "h2d" && cfg.transfer_mode != "d2h" &&
      cfg.transfer_mode != "h2h") {
    fprintf(stderr, "[ERROR] Invalid transfer_mode: %s\n", cfg.transfer_mode.c_str());
    return false;
  }
  if (cfg.transfer_op != "write" && cfg.transfer_op != "read") {
    fprintf(stderr, "[ERROR] Invalid transfer_op: %s\n", cfg.transfer_op.c_str());
    return false;
  }
  for (uint32_t i = 0; i < cfg.block_steps; ++i) {
    if (cfg.block_size > (UINT64_MAX >> i)) {
      fprintf(stderr, "[ERROR] block_size<<%u overflows\n", i);
      return false;
    }
    const uint64_t block = cfg.block_size << i;
    if (block == 0) {
      fprintf(stderr, "[ERROR] computed block size is 0\n");
      return false;
    }
    if (cfg.total_size % block != 0) {
      fprintf(stderr,
              "[ERROR] total_size (%" PRIu64 ") must be divisible by block size (%" PRIu64 ") at step %u\n",
              cfg.total_size, block, i);
      return false;
    }
  }
  return true;
}

struct TransferBlockStepCtx {
  uintptr_t base = 0;
  const char *remote_engine = nullptr;
  uint64_t dst_addr = 0;
  const BenchmarkConfig *cfg = nullptr;
  TransferOp transfer_op = TransferOp::READ;
  uint32_t loop = 0;
  uint32_t step_index = 0;
  uint64_t block_size_u = 0;
};

}  // namespace

int32_t Initialize(Hixl &hixl_engine, const char *local_engine,
                   const std::map<AscendString, AscendString> &options) {
  auto ret = hixl_engine.Initialize(local_engine, options);
  if (ret != SUCCESS) {
    printf("[ERROR] Initialize failed, ret = %u, errmsg: %s\n", ret, GetRecentErrMsg());
    return -1;
  }
  printf("[INFO] Initialize success\n");
  return 0;
}

int32_t Connect(Hixl &hixl_engine, const char *remote_engine) {
  auto ret = hixl_engine.Connect(remote_engine);
  if (ret != SUCCESS) {
    printf("[ERROR] Connect failed, ret = %u, errmsg: %s\n", ret, GetRecentErrMsg());
    return -1;
  }
  printf("[INFO] Connect success\n");
  return 0;
}

void Disconnect(Hixl &hixl_engine, const char *remote_engine, bool connected) {
  if (connected) {
    auto ret = hixl_engine.Disconnect(remote_engine);
    if (ret != SUCCESS) {
      printf("[ERROR] Disconnect failed, ret = %u, errmsg: %s\n", ret, GetRecentErrMsg());
    } else {
      printf("[INFO] Disconnect success\n");
    }
  }
}

int32_t TransferOneBlockStep(Hixl &hixl_engine, const TransferBlockStepCtx &ctx) {
  const auto block_size = static_cast<uint32_t>(ctx.block_size_u);
  if (static_cast<uint64_t>(block_size) != ctx.block_size_u) {
    printf("[ERROR] block size too large at step %u\n", ctx.step_index);
    return -1;
  }
  const auto trans_num = static_cast<uint32_t>(ctx.cfg->total_size / ctx.block_size_u);
  std::vector<TransferOpDesc> descs;
  descs.reserve(trans_num);
  for (uint32_t j = 0; j < trans_num; j++) {
    TransferOpDesc desc{};
    desc.local_addr = ctx.base + static_cast<uintptr_t>(j) * block_size;
    desc.remote_addr = static_cast<uintptr_t>(ctx.dst_addr) + static_cast<uintptr_t>(j) * block_size;
    desc.len = block_size;
    descs.emplace_back(desc);
  }
  const auto start = std::chrono::steady_clock::now();
  const auto ret =
      hixl_engine.TransferSync(ctx.remote_engine, ctx.transfer_op, descs, 1000 * kWaitTransTime);
  if (ret != SUCCESS) {
    printf("[ERROR] TransferSync failed, ret = %u, errmsg: %s\n", ret, GetRecentErrMsg());
    return -1;
  }
  const auto time_cost =
      std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
  const double time_second = static_cast<double>(time_cost) / 1000 / 1000;
  const double throughput = static_cast<double>(ctx.cfg->total_size) / 1024 / 1024 / 1024 / time_second;
  printf(
      "[INFO] Transfer success, loop %u/%u, block size: %u Bytes, transfer num: %u, time cost: %ld us, "
      "throughput: %.3lf GB/s\n",
      ctx.loop + 1U, ctx.cfg->loops, block_size, trans_num, time_cost, throughput);
  return 0;
}

int32_t Transfer(Hixl &hixl_engine, void *src_base, const char *remote_engine, uint64_t dst_addr,
                 const BenchmarkConfig &cfg) {
  const TransferOp transfer_op = (cfg.transfer_op == "read") ? TransferOp::READ : TransferOp::WRITE;
  const uintptr_t base = reinterpret_cast<uintptr_t>(src_base);
  TransferBlockStepCtx step_ctx{};
  step_ctx.base = base;
  step_ctx.remote_engine = remote_engine;
  step_ctx.dst_addr = dst_addr;
  step_ctx.cfg = &cfg;
  step_ctx.transfer_op = transfer_op;
  for (uint32_t loop = 0; loop < cfg.loops; ++loop) {
    step_ctx.loop = loop;
    for (uint32_t i = 0; i < cfg.block_steps; ++i) {
      step_ctx.step_index = i;
      step_ctx.block_size_u = cfg.block_size << i;
      const int32_t step_ret = TransferOneBlockStep(hixl_engine, step_ctx);
      if (step_ret != 0) {
        return step_ret;
      }
    }
  }
  return 0;
}

void Finalize(Hixl &hixl_engine, bool need_register, bool is_host, const std::vector<MemHandle> &handles,
              const std::vector<void *> &buffers = {}) {
  if (need_register) {
    for (const auto &handle : handles) {
      auto ret = hixl_engine.DeregisterMem(handle);
      if (ret != 0) {
        printf("[ERROR] DeregisterMem failed, ret = %u, errmsg: %s\n", ret, GetRecentErrMsg());
      } else {
        printf("[INFO] DeregisterMem success\n");
      }
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

int32_t ClientAllocSrcBuffer(const BenchmarkConfig &cfg, bool *is_host, void **out_src) {
  *is_host = (cfg.transfer_mode == "h2d" || cfg.transfer_mode == "h2h");
  const size_t alloc_size = static_cast<size_t>(cfg.total_size);
  void *tmp = nullptr;
  if (*is_host) {
    CHECK_ACL_RETURN(aclrtMallocHost(&tmp, alloc_size));
  } else {
    CHECK_ACL_RETURN(aclrtMalloc(&tmp, alloc_size, ACL_MEM_MALLOC_HUGE_ONLY));
  }
  *out_src = tmp;
  return 0;
}

int32_t ClientRegisterMemIfNeeded(Hixl &hixl_engine, const BenchmarkConfig &cfg, void *src, bool is_host,
                                  bool need_register, MemHandle *handle) {
  if (!need_register) {
    return 0;
  }
  MemDesc desc{};
  desc.addr = reinterpret_cast<uintptr_t>(src);
  desc.len = static_cast<size_t>(cfg.total_size);
  const auto ret = hixl_engine.RegisterMem(desc, is_host ? MemType::MEM_HOST : MEM_DEVICE, *handle);
  if (ret != SUCCESS) {
    printf("[ERROR] RegisterMem failed, ret = %u, errmsg: %s\n", ret, GetRecentErrMsg());
    return -1;
  }
  printf("[INFO] RegisterMem success\n");
  return 0;
}

int32_t ClientTcpAcceptAndRecvAddr(TCPServer *tcp_server, uint16_t tcp_port, uint64_t *remote_addr) {
  if (!tcp_server->StartServer(tcp_port)) {
    printf("[ERROR] Failed to start TCP server.\n");
    return -1;
  }
  printf("[INFO] TCP server started.\n");
  if (!tcp_server->AcceptConnection()) {
    return -1;
  }
  *remote_addr = tcp_server->ReceiveUint64();
  if (*remote_addr != 0) {
    printf("[INFO] Success to receive server mem addr: 0x%lx\n", *remote_addr);
  }
  return 0;
}

void ClientFinalizeMem(Hixl &hixl_engine, bool need_register, bool is_host, MemHandle handle, void *src) {
  Finalize(hixl_engine, need_register, is_host, {handle}, {src});
}

int32_t RunClient(const BenchmarkConfig &cfg) {
  printf("[INFO] client start\n");

  TCPServer tcp_server;
  uint64_t remote_addr = 0;
  if (ClientTcpAcceptAndRecvAddr(&tcp_server, cfg.tcp_port, &remote_addr) != 0) {
    return -1;
  }

  Hixl hixl_engine;
  const std::map<AscendString, AscendString> init_options = BuildInitializeOptions(cfg);
  if (Initialize(hixl_engine, cfg.local_engine.c_str(), init_options) != 0) {
    printf("[ERROR] Initialize Hixl failed\n");
    tcp_server.StopServer();
    return -1;
  }

  MemHandle handle = nullptr;
  bool is_host = false;
  void *src = nullptr;
  if (ClientAllocSrcBuffer(cfg, &is_host, &src) != 0) {
    ClientFinalizeMem(hixl_engine, false, is_host, nullptr, {});
    tcp_server.StopServer();
    return -1;
  }

  const bool need_register = !(is_host && cfg.use_buffer_pool);
  if (ClientRegisterMemIfNeeded(hixl_engine, cfg, src, is_host, need_register, &handle) != 0) {
    ClientFinalizeMem(hixl_engine, need_register, is_host, handle, src);
    tcp_server.StopServer();
    return -1;
  }

  if (tcp_server.ReceiveTaskStatus()) {
    printf("[INFO] Server RegisterMem success\n");
  }

  bool connected = false;
  if (Connect(hixl_engine, cfg.remote_engine.c_str()) != 0) {
    ClientFinalizeMem(hixl_engine, need_register, is_host, handle, src);
    tcp_server.StopServer();
    return -1;
  }
  connected = true;

  if (Transfer(hixl_engine, src, cfg.remote_engine.c_str(), remote_addr, cfg) != 0) {
    Disconnect(hixl_engine, cfg.remote_engine.c_str(), connected);
    ClientFinalizeMem(hixl_engine, need_register, is_host, handle, src);
    tcp_server.StopServer();
    return -1;
  }

  Disconnect(hixl_engine, cfg.remote_engine.c_str(), connected);
  (void)tcp_server.SendTaskStatus();
  tcp_server.DisConnectClient();
  tcp_server.StopServer();

  ClientFinalizeMem(hixl_engine, need_register, is_host, handle, src);
  printf("[INFO] Client Sample end\n");
  return 0;
}

int32_t RunServer(const BenchmarkConfig &cfg) {
  printf("[INFO] server start\n");
  Hixl hixl_engine;
  const std::map<AscendString, AscendString> init_options = BuildInitializeOptions(cfg);
  if (Initialize(hixl_engine, cfg.local_engine.c_str(), init_options) != 0) {
    printf("[ERROR] Initialize Hixl failed\n");
    return -1;
  }
  void *buffer = nullptr;
  bool is_host = (cfg.transfer_mode == "d2h" || cfg.transfer_mode == "h2h");
  const size_t alloc_size = static_cast<size_t>(cfg.total_size);
  if (is_host) {
    CHECK_ACL_RETURN(aclrtMallocHost(&buffer, alloc_size));
  } else {
    CHECK_ACL_RETURN(aclrtMalloc(&buffer, alloc_size, ACL_MEM_MALLOC_HUGE_ONLY));
  }
  auto addr = reinterpret_cast<uintptr_t>(buffer);

  TCPClient tcp_client;
  if (!tcp_client.ConnectToServer(cfg.remote_engine, cfg.tcp_port)) {
    return -1;
  }
  (void)tcp_client.SendUint64(addr);

  MemHandle handle = nullptr;
  auto mem_type = is_host ? MemType::MEM_HOST : MemType::MEM_DEVICE;

  bool need_register = !(cfg.use_buffer_pool && cfg.transfer_mode == "d2h");
  if (need_register) {
    MemDesc desc{};
    desc.addr = addr;
    desc.len = static_cast<size_t>(cfg.total_size);
    auto ret = hixl_engine.RegisterMem(desc, mem_type, handle);
    if (ret != SUCCESS) {
      printf("[ERROR] RegisterMem failed, ret = %u, errmsg: %s\n", ret, GetRecentErrMsg());
      Finalize(hixl_engine, need_register, is_host, {handle}, {buffer});
      return -1;
    }
    printf("[INFO] RegisterMem success, addr:%p\n", buffer);
  }

  (void)tcp_client.SendTaskStatus();

  printf("[INFO] Wait transfer begin\n");
  if (tcp_client.ReceiveTaskStatus()) {
    printf("[INFO] Wait transfer end\n");
  }
  tcp_client.Disconnect();

  Finalize(hixl_engine, need_register, is_host, {handle}, {buffer});
  printf("[INFO] Server Sample end\n");
  return 0;
}

int32_t main(int32_t argc, char **argv) {
  if (argc <= 1) {
    PrintUsage(stderr);
    return -1;
  }
  BenchmarkConfig cfg;
  if (!BuildConfig(argc, argv, &cfg)) {
    return -1;
  }
  if (!ValidateConfig(cfg)) {
    return -1;
  }
  printf(
      "[INFO] role=%s device_id=%d local_engine=%s remote_engine=%s tcp_port=%u transfer_mode=%s transfer_op=%s "
      "use_buffer_pool=%s total_size=%" PRIu64 " block_size=%" PRIu64 " block_steps=%u loops=%u\n",
      cfg.role == Role::kClient ? "client" : "server", static_cast<int>(cfg.device_id), cfg.local_engine.c_str(),
      cfg.remote_engine.c_str(), static_cast<unsigned>(cfg.tcp_port), cfg.transfer_mode.c_str(),
      cfg.transfer_op.c_str(), cfg.use_buffer_pool ? "true" : "false", cfg.total_size, cfg.block_size, cfg.block_steps,
      cfg.loops);
  if (cfg.loops == 1U) {
    printf(
        "[INFO] loops=1: the first transfer is often warm-up; for steady throughput use the second repeat's "
        "metrics or set loops>1 (--loops|-n).\n");
  }
  {
    const std::map<AscendString, AscendString> eff = BuildInitializeOptions(cfg);
    if (eff.empty()) {
      printf("[INFO] hixl_init_options (effective): none\n");
    } else {
      printf("[INFO] hixl_init_options (effective):\n");
      for (const auto &p : eff) {
        printf("[INFO]   %s=%s\n", p.first.GetString(), p.second.GetString());
      }
    }
  }

  CHECK_ACL_RETURN(aclrtSetDevice(cfg.device_id));

  int32_t ret = 0;
  if (cfg.role == Role::kClient) {
    ret = RunClient(cfg);
  } else {
    ret = RunServer(cfg);
  }
  CHECK_ACL_RETURN(aclrtResetDevice(cfg.device_id));
  return ret;
}
