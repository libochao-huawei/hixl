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
constexpr uint64_t kDefaultBlockSize = 262144ULL;   // 256 KiB
constexpr uint32_t kDefaultBlockSteps = 6U;
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
  uint64_t block_size = kDefaultBlockSize;
  uint32_t block_steps = kDefaultBlockSteps;
  uint32_t loops = kDefaultLoops;
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
          "  --block_size|-k      first block size in bytes (default %" PRIu64 ")\n"
          "  --block_steps|-s     block count: sizes are block_size<<i, i in [0,steps-1] (default %u)\n"
          "  --loops|-n           repeat full step ladder (default %u)\n"
          "Defaults: client device_id=0 local_engine=127.0.0.1:16000 remote_engine=127.0.0.1:16001\n"
          "          server device_id=1 local_engine=127.0.0.1:16001 remote_engine=127.0.0.1 (TCP host)\n"
          "          transfer_mode=d2d transfer_op=read use_buffer_pool=false tcp_port=20000\n",
          kDefaultTotalSize, kDefaultBlockSize, kDefaultBlockSteps, kDefaultLoops);
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

bool CollectArgs(int argc, char **argv, std::map<std::string, std::string> *kv) {
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
    (*kv)[key] = val;
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
  cfg->block_size = kDefaultBlockSize;
  cfg->block_steps = kDefaultBlockSteps;
  cfg->loops = kDefaultLoops;
}

bool ApplyKvOverrides(const std::map<std::string, std::string> &kv, BenchmarkConfig *cfg) {
  for (const auto &entry : kv) {
    const std::string &key = entry.first;
    const std::string &val = entry.second;
    if (key == "--role" || key == "-r") {
      if (val == "client") {
        cfg->role = Role::kClient;
      } else if (val == "server") {
        cfg->role = Role::kServer;
      } else {
        fprintf(stderr, "[ERROR] Invalid --role=%s (expect client|server)\n", val.c_str());
        return false;
      }
    } else if (key == "--device_id" || key == "-d") {
      if (!ParseI32(val, &cfg->device_id)) {
        fprintf(stderr, "[ERROR] Invalid --device_id=%s\n", val.c_str());
        return false;
      }
    } else if (key == "--local_engine" || key == "-l") {
      cfg->local_engine = val;
    } else if (key == "--remote_engine" || key == "-e") {
      cfg->remote_engine = val;
    } else if (key == "--tcp_port" || key == "-p") {
      int32_t port = 0;
      if (!ParseI32(val, &port) || port < 0 || port > kPortMaxValue) {
        fprintf(stderr, "[ERROR] Invalid --tcp_port=%s\n", val.c_str());
        return false;
      }
      cfg->tcp_port = static_cast<uint16_t>(port);
    } else if (key == "--transfer_mode" || key == "-m") {
      cfg->transfer_mode = val;
    } else if (key == "--transfer_op" || key == "-o") {
      cfg->transfer_op = val;
    } else if (key == "--use_buffer_pool" || key == "-b") {
      if (!ParseBool(val, &cfg->use_buffer_pool)) {
        fprintf(stderr, "[ERROR] Invalid --use_buffer_pool=%s (expect true|false|0|1)\n", val.c_str());
        return false;
      }
    } else if (key == "--total_size" || key == "-t") {
      if (!ParseU64(val, &cfg->total_size) || cfg->total_size == 0) {
        fprintf(stderr, "[ERROR] Invalid --total_size=%s\n", val.c_str());
        return false;
      }
    } else if (key == "--block_size" || key == "-k") {
      if (!ParseU64(val, &cfg->block_size) || cfg->block_size == 0) {
        fprintf(stderr, "[ERROR] Invalid --block_size=%s\n", val.c_str());
        return false;
      }
    } else if (key == "--block_steps" || key == "-s") {
      if (!ParseU32(val, &cfg->block_steps) || cfg->block_steps == 0) {
        fprintf(stderr, "[ERROR] Invalid --block_steps=%s\n", val.c_str());
        return false;
      }
    } else if (key == "--loops" || key == "-n") {
      if (!ParseU32(val, &cfg->loops) || cfg->loops == 0) {
        fprintf(stderr, "[ERROR] Invalid --loops=%s\n", val.c_str());
        return false;
      }
    } else {
      fprintf(stderr, "[ERROR] Unknown option: %s\n", key.c_str());
      return false;
    }
  }
  return true;
}

bool BuildConfig(int argc, char **argv, BenchmarkConfig *cfg) {
  std::map<std::string, std::string> kv;
  if (!CollectArgs(argc, argv, &kv)) {
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

}  // namespace

int32_t Initialize(Hixl &hixl_engine, const char *local_engine, bool use_buffer_pool) {
  std::map<AscendString, AscendString> options;
  if (!use_buffer_pool) {
    options["BufferPool"] = "0:0";
  }
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

int32_t Transfer(Hixl &hixl_engine, void *src_base, const char *remote_engine, uint64_t dst_addr,
                 const BenchmarkConfig &cfg) {
  TransferOp transfer_op = (cfg.transfer_op == "read") ? TransferOp::READ : TransferOp::WRITE;
  const uintptr_t base = reinterpret_cast<uintptr_t>(src_base);
  for (uint32_t loop = 0; loop < cfg.loops; ++loop) {
    for (uint32_t i = 0; i < cfg.block_steps; ++i) {
      uint64_t block_size_u = cfg.block_size << i;
      auto block_size = static_cast<uint32_t>(block_size_u);
      if (static_cast<uint64_t>(block_size) != block_size_u) {
        printf("[ERROR] block size too large at step %u\n", i);
        return -1;
      }
      auto trans_num = static_cast<uint32_t>(cfg.total_size / block_size_u);
      std::vector<TransferOpDesc> descs;
      descs.reserve(trans_num);
      for (uint32_t j = 0; j < trans_num; j++) {
        TransferOpDesc desc{};
        desc.local_addr = base + static_cast<uintptr_t>(j) * block_size;
        desc.remote_addr = static_cast<uintptr_t>(dst_addr) + static_cast<uintptr_t>(j) * block_size;
        desc.len = block_size;
        descs.emplace_back(desc);
      }
      const auto start = std::chrono::steady_clock::now();
      auto ret = hixl_engine.TransferSync(remote_engine, transfer_op, descs, 1000 * kWaitTransTime);
      if (ret != SUCCESS) {
        printf("[ERROR] TransferSync failed, ret = %u, errmsg: %s\n", ret, GetRecentErrMsg());
        return -1;
      }
      auto time_cost =
          std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
      double time_second = static_cast<double>(time_cost) / 1000 / 1000;
      double throughput = static_cast<double>(cfg.total_size) / 1024 / 1024 / 1024 / time_second;
      printf(
          "[INFO] Transfer success, loop %u/%u, block size: %u Bytes, transfer num: %u, time cost: %ld us, "
          "throughput: %.3lf GB/s\n",
          loop + 1U, cfg.loops, block_size, trans_num, time_cost, throughput);
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

int32_t RunClient(const BenchmarkConfig &cfg) {
  printf("[INFO] client start\n");

  TCPServer tcp_server;
  uint64_t remote_addr = 0;
  if (!tcp_server.StartServer(cfg.tcp_port)) {
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

  Hixl hixl_engine;
  if (Initialize(hixl_engine, cfg.local_engine.c_str(), cfg.use_buffer_pool) != 0) {
    printf("[ERROR] Initialize Hixl failed\n");
    return -1;
  }

  void *tmp = nullptr;
  MemHandle handle = nullptr;
  bool connected = false;
  bool is_host = (cfg.transfer_mode == "h2d" || cfg.transfer_mode == "h2h");
  const size_t alloc_size = static_cast<size_t>(cfg.total_size);
  if (is_host) {
    CHECK_ACL_RETURN(aclrtMallocHost(&tmp, alloc_size));
  } else {
    CHECK_ACL_RETURN(aclrtMalloc(&tmp, alloc_size, ACL_MEM_MALLOC_HUGE_ONLY));
  }
  void *src = tmp;

  bool need_register = !(is_host && cfg.use_buffer_pool);
  if (need_register) {
    MemDesc desc{};
    desc.addr = reinterpret_cast<uintptr_t>(src);
    desc.len = static_cast<size_t>(cfg.total_size);
    auto ret = hixl_engine.RegisterMem(desc, is_host ? MemType::MEM_HOST : MEM_DEVICE, handle);
    if (ret != SUCCESS) {
      printf("[ERROR] RegisterMem failed, ret = %u, errmsg: %s\n", ret, GetRecentErrMsg());
      Finalize(hixl_engine, need_register, is_host, {handle}, {src});
      return -1;
    }
    printf("[INFO] RegisterMem success\n");
  }

  if (tcp_server.ReceiveTaskStatus()) {
    printf("[INFO] Server RegisterMem success\n");
  }

  if (Connect(hixl_engine, cfg.remote_engine.c_str()) != 0) {
    Finalize(hixl_engine, need_register, is_host, {handle}, {src});
    return -1;
  }
  connected = true;

  if (Transfer(hixl_engine, src, cfg.remote_engine.c_str(), remote_addr, cfg) != 0) {
    Disconnect(hixl_engine, cfg.remote_engine.c_str(), connected);
    Finalize(hixl_engine, need_register, is_host, {handle}, {src});
    return -1;
  }

  Disconnect(hixl_engine, cfg.remote_engine.c_str(), connected);
  (void)tcp_server.SendTaskStatus();
  tcp_server.DisConnectClient();
  tcp_server.StopServer();

  Finalize(hixl_engine, need_register, is_host, {handle}, {src});
  printf("[INFO] Client Sample end\n");
  return 0;
}

int32_t RunServer(const BenchmarkConfig &cfg) {
  printf("[INFO] server start\n");
  Hixl hixl_engine;
  if (Initialize(hixl_engine, cfg.local_engine.c_str(), cfg.use_buffer_pool) != 0) {
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
