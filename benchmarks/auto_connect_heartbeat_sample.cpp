/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <algorithm>
#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include "acl/acl.h"
#include "common/benchmark_config.h"
#include "common/tcp_client_server.h"
#include "hixl/hixl.h"

using hixl::AscendString;
using hixl::Hixl;
using hixl::MemDesc;
using hixl::MemHandle;
using hixl::MemType;
using hixl::SUCCESS;
using hixl::TransferOp;
using hixl::TransferOpDesc;
using hixl_benchmark::BenchmarkConfig;
using hixl_benchmark::BenchmarkConfigParser;
using hixl_benchmark::BenchmarkRole;

namespace {

constexpr uint32_t kDefaultServerLifetimeSec = 15U;
constexpr uint32_t kDefaultHeartbeatWaitSec = 45U;
constexpr uint32_t kDefaultPollIntervalMs = 1000U;
constexpr uint32_t kMinPollIntervalMs = 10U;

struct SampleConfig {
  uint32_t server_lifetime_s = kDefaultServerLifetimeSec;
  uint32_t heartbeat_wait_s = kDefaultHeartbeatWaitSec;
  uint32_t poll_interval_ms = kDefaultPollIntervalMs;
};

const char *RecentErrMsg() {
  const char *errmsg = aclGetRecentErrMsg();
  if (errmsg == nullptr) {
    return "no error";
  }
  return errmsg;
}

void PrintUsage(FILE *out) {
  BenchmarkConfigParser::PrintUsage(out);
  std::fprintf(out,
               "\nAuto connect heartbeat sample extra keys:\n"
               "  --server_lifetime_s   server only: seconds to keep server alive after TCP handshake (default %u)\n"
               "  --heartbeat_wait_s     client only: max seconds to wait heartbeat cleanup (default %u)\n"
               "  --poll_interval_ms     client only: DeregisterMem polling interval in ms (default %u)\n",
               kDefaultServerLifetimeSec, kDefaultHeartbeatWaitSec, kDefaultPollIntervalMs);
}

bool ParseU32(const std::string &value, uint32_t *out) {
  if (out == nullptr || value.empty()) {
    return false;
  }
  try {
    std::size_t pos = 0;
    const unsigned long parsed = std::stoul(value, &pos, 10);
    if (pos != value.size() || parsed > static_cast<unsigned long>(UINT32_MAX)) {
      return false;
    }
    *out = static_cast<uint32_t>(parsed);
    return true;
  } catch (const std::exception &) {
    return false;
  }
}

bool ApplySampleArg(const std::string &key, const std::string &value, SampleConfig *sample_cfg) {
  if (sample_cfg == nullptr) {
    return false;
  }
  uint32_t parsed = 0U;
  if (!ParseU32(value, &parsed) || parsed == 0U) {
    std::fprintf(stderr, "[ERROR] Invalid %s=%s, expect positive integer\n", key.c_str(), value.c_str());
    return false;
  }
  if (key == "--server_lifetime_s") {
    sample_cfg->server_lifetime_s = parsed;
    return true;
  }
  if (key == "--heartbeat_wait_s") {
    sample_cfg->heartbeat_wait_s = parsed;
    return true;
  }
  if (key == "--poll_interval_ms") {
    sample_cfg->poll_interval_ms = std::max(parsed, kMinPollIntervalMs);
    return true;
  }
  return false;
}

bool BuildConfigs(int argc, char **argv, BenchmarkConfig *bench_cfg, SampleConfig *sample_cfg) {
  if (argc <= 1) {
    PrintUsage(stderr);
    return false;
  }
  std::vector<std::string> filtered_args;
  filtered_args.reserve(static_cast<size_t>(argc));
  filtered_args.emplace_back(argv[0]);
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "-h" || arg == "--help") {
      PrintUsage(stdout);
      return false;
    }
    const auto pos = arg.find('=');
    if (pos == std::string::npos) {
      filtered_args.emplace_back(arg);
      continue;
    }
    const std::string key = arg.substr(0, pos);
    const std::string value = arg.substr(pos + 1);
    if (key == "--server_lifetime_s" || key == "--heartbeat_wait_s" || key == "--poll_interval_ms") {
      if (!ApplySampleArg(key, value, sample_cfg)) {
        return false;
      }
      continue;
    }
    filtered_args.emplace_back(arg);
  }

  std::vector<char *> filtered_argv;
  filtered_argv.reserve(filtered_args.size());
  for (std::string &arg : filtered_args) {
    filtered_argv.push_back(arg.data());
  }
  if (!BenchmarkConfigParser::BuildFromArgv(static_cast<int>(filtered_argv.size()), filtered_argv.data(), bench_cfg)) {
    return false;
  }
  if (!BenchmarkConfigParser::Validate(bench_cfg)) {
    return false;
  }
  if (bench_cfg->expanded_device_ids.size() != 1U || bench_cfg->expanded_local_engines.size() != 1U ||
      bench_cfg->expanded_remote_engines.size() != 1U) {
    std::fprintf(stderr, "[ERROR] auto_connect_heartbeat_sample supports exactly one client/server pair\n");
    return false;
  }
  return true;
}

std::map<AscendString, AscendString> BuildInitializeOptions(const BenchmarkConfig &cfg, bool force_auto_connect) {
  auto options = BenchmarkConfigParser::BuildInitializeOptions(cfg);
  if (force_auto_connect) {
    options[AscendString(hixl::OPTION_AUTO_CONNECT)] = AscendString("1");
  }
  return options;
}

bool SetDevice(int32_t device_id) {
  const aclError ret = aclrtSetDevice(device_id);
  if (ret != ACL_ERROR_NONE) {
    std::fprintf(stderr, "[ERROR] aclrtSetDevice(%d) failed, aclError=%d\n", device_id, static_cast<int>(ret));
    return false;
  }
  return true;
}

bool AllocBuffer(const BenchmarkConfig &cfg, bool is_client, void **buffer, bool *is_host) {
  if (buffer == nullptr || is_host == nullptr) {
    return false;
  }
  *is_host = is_client ? (cfg.transfer_mode == "h2d" || cfg.transfer_mode == "h2h")
                       : (cfg.transfer_mode == "d2h" || cfg.transfer_mode == "h2h");
  const size_t alloc_size = static_cast<size_t>(cfg.total_size);
  aclError ret = ACL_ERROR_NONE;
  if (*is_host) {
    ret = aclrtMallocHost(buffer, alloc_size);
  } else {
    ret = aclrtMalloc(buffer, alloc_size, ACL_MEM_MALLOC_HUGE_ONLY);
  }
  if (ret != ACL_ERROR_NONE) {
    std::fprintf(stderr, "[ERROR] aclrtMalloc%s failed, aclError=%d, size=%zu\n", *is_host ? "Host" : "",
                 static_cast<int>(ret), alloc_size);
    return false;
  }
  std::printf("[INFO] Alloc %s buffer success, addr=%p, size=%zu\n", *is_host ? "host" : "device", *buffer,
              alloc_size);
  return true;
}

void FreeBuffer(void *buffer, bool is_host) {
  if (buffer == nullptr) {
    return;
  }
  if (is_host) {
    (void)aclrtFreeHost(buffer);
  } else {
    (void)aclrtFree(buffer);
  }
}

bool NeedRegisterMem(const BenchmarkConfig &cfg, bool is_client, bool is_host) {
  if (is_client) {
    return !(is_host && cfg.use_buffer_pool);
  }
  return !(cfg.use_buffer_pool && cfg.transfer_mode == "d2h");
}

bool RegisterMem(Hixl *hixl, const BenchmarkConfig &cfg, void *buffer, bool is_host, bool need_register,
                 MemHandle *mem_handle) {
  if (!need_register) {
    return true;
  }
  MemDesc desc{};
  desc.addr = reinterpret_cast<uintptr_t>(buffer);
  desc.len = static_cast<size_t>(cfg.total_size);
  const auto ret = hixl->RegisterMem(desc, is_host ? MemType::MEM_HOST : MemType::MEM_DEVICE, *mem_handle);
  if (ret != SUCCESS) {
    std::fprintf(stderr, "[ERROR] RegisterMem failed, ret=%u, errmsg:%s\n", ret, RecentErrMsg());
    return false;
  }
  std::printf("[INFO] RegisterMem success, addr=%p, len=%zu\n", buffer, desc.len);
  return true;
}

bool InitializeHixl(Hixl *hixl, const BenchmarkConfig &cfg, bool force_auto_connect) {
  const auto options = BuildInitializeOptions(cfg, force_auto_connect);
  const std::string &local_engine = cfg.expanded_local_engines[0];
  const auto ret = hixl->Initialize(AscendString(local_engine.c_str()), options);
  if (ret != SUCCESS) {
    std::fprintf(stderr, "[ERROR] Hixl Initialize failed, local_engine=%s, ret=%u, errmsg:%s\n",
                 local_engine.c_str(), ret, RecentErrMsg());
    return false;
  }
  std::printf("[INFO] Hixl Initialize success, local_engine=%s, AutoConnect=%s\n", local_engine.c_str(),
              force_auto_connect ? "1" : "unchanged");
  return true;
}

bool DoOneTransfer(Hixl *hixl, const BenchmarkConfig &cfg, void *local_buffer, uint64_t remote_addr) {
  const TransferOp transfer_op = (cfg.transfer_op == "read") ? TransferOp::READ : TransferOp::WRITE;
  const uint32_t len = static_cast<uint32_t>(std::min<uint64_t>(cfg.total_size, UINT32_MAX));
  TransferOpDesc desc{};
  desc.local_addr = reinterpret_cast<uintptr_t>(local_buffer);
  desc.remote_addr = static_cast<uintptr_t>(remote_addr);
  desc.len = len;
  const std::string &remote_engine = cfg.expanded_remote_engines[0];
  const auto ret = hixl->TransferSync(AscendString(remote_engine.c_str()), transfer_op, {desc},
                                      static_cast<int32_t>(cfg.connect_timeout_ms));
  if (ret != SUCCESS) {
    std::fprintf(stderr, "[ERROR] TransferSync failed, ret=%u, errmsg:%s\n", ret, RecentErrMsg());
    return false;
  }
  std::printf("[INFO] One TransferSync success, len=%u\n", len);
  return true;
}

bool GetRemoteAddr(const BenchmarkConfig &cfg, TCPClient *tcp_client, uint64_t *remote_addr) {
  const std::string host = hixl_benchmark::ExtractTcpHost(cfg.expanded_remote_engines[0]);
  if (host.empty()) {
    std::fprintf(stderr, "[ERROR] remote_engine must be host:port, got %s\n", cfg.expanded_remote_engines[0].c_str());
    return false;
  }
  if (!tcp_client->ConnectToServer(host, cfg.expanded_tcp_ports[0])) {
    return false;
  }
  if (!tcp_client->ReceiveUint64(remote_addr)) {
    return false;
  }
  if (!tcp_client->ReceiveTaskStatus()) {
    return false;
  }
  std::printf("[INFO] Received remote buffer addr=0x%" PRIx64 "\n", *remote_addr);
  return true;
}

int RunServer(const BenchmarkConfig &cfg, const SampleConfig &sample_cfg) {
  if (!SetDevice(cfg.expanded_device_ids[0])) {
    return -1;
  }

  Hixl hixl;
  void *buffer = nullptr;
  bool is_host = false;
  MemHandle mem_handle = nullptr;
  bool ok = AllocBuffer(cfg, false, &buffer, &is_host) && InitializeHixl(&hixl, cfg, false);
  const bool need_register = ok && NeedRegisterMem(cfg, false, is_host);
  ok = ok && RegisterMem(&hixl, cfg, buffer, is_host, need_register, &mem_handle);
  if (!ok) {
    hixl.Finalize();
    FreeBuffer(buffer, is_host);
    (void)aclrtResetDevice(cfg.expanded_device_ids[0]);
    return -1;
  }

  TcpServerSession session(cfg.expanded_tcp_ports[0], cfg.tcp_accept_wait_sec, 1U);
  if (!session.WaitAndSendAddr(reinterpret_cast<uint64_t>(buffer))) {
    hixl.Finalize();
    FreeBuffer(buffer, is_host);
    (void)aclrtResetDevice(cfg.expanded_device_ids[0]);
    return -1;
  }
  std::printf("[INFO] Server will exit in %" PRIu32 " seconds to simulate server failure\n",
              sample_cfg.server_lifetime_s);
  std::this_thread::sleep_for(std::chrono::seconds(sample_cfg.server_lifetime_s));
  std::printf("[INFO] Server exits without Hixl Finalize now\n");
  std::fflush(stdout);
  std::fflush(stderr);
  std::_Exit(0);

  return 0;
}

bool WaitHeartbeatCleanup(Hixl *hixl, const BenchmarkConfig &cfg, MemHandle mem_handle,
                          const SampleConfig &sample_cfg) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(sample_cfg.heartbeat_wait_s);
  const auto poll_interval = std::chrono::milliseconds(sample_cfg.poll_interval_ms);
  bool saw_connected_reject = false;
  while (std::chrono::steady_clock::now() < deadline) {
    const auto ret = hixl->DeregisterMem(mem_handle);
    if (ret == SUCCESS) {
      std::printf("[PASS] heartbeat cleanup observed: DeregisterMem succeeds after server exit\n");
      return true;
    }
    saw_connected_reject = true;
    std::printf("[INFO] DeregisterMem still blocked while client is connected, ret=%u\n", ret);
    std::this_thread::sleep_for(poll_interval);
  }
  std::fprintf(stderr,
               "[FAIL] heartbeat cleanup not observed within %" PRIu32
               " seconds, remote_engine=%s, saw_connected_reject=%s\n",
               sample_cfg.heartbeat_wait_s, cfg.expanded_remote_engines[0].c_str(),
               saw_connected_reject ? "true" : "false");
  return false;
}

int RunClient(const BenchmarkConfig &cfg, const SampleConfig &sample_cfg) {
  if (!SetDevice(cfg.expanded_device_ids[0])) {
    return -1;
  }

  Hixl hixl;
  void *buffer = nullptr;
  bool is_host = false;
  MemHandle mem_handle = nullptr;
  bool mem_registered = false;
  bool ok = AllocBuffer(cfg, true, &buffer, &is_host) && InitializeHixl(&hixl, cfg, true);
  const bool need_register = ok && NeedRegisterMem(cfg, true, is_host);
  ok = ok && RegisterMem(&hixl, cfg, buffer, is_host, need_register, &mem_handle);
  mem_registered = ok && need_register;

  TCPClient tcp_client;
  uint64_t remote_addr = 0U;
  ok = ok && GetRemoteAddr(cfg, &tcp_client, &remote_addr);
  const std::string &remote_engine = cfg.expanded_remote_engines[0];
  if (ok) {
    const auto ret = hixl.Connect(AscendString(remote_engine.c_str()), static_cast<int32_t>(cfg.connect_timeout_ms));
    if (ret != SUCCESS) {
      std::fprintf(stderr, "[ERROR] Connect failed, remote_engine=%s, ret=%u, errmsg:%s\n", remote_engine.c_str(), ret,
                   RecentErrMsg());
      ok = false;
    } else {
      std::printf("[INFO] Connect success, remote_engine=%s\n", remote_engine.c_str());
    }
  }
  ok = ok && DoOneTransfer(&hixl, cfg, buffer, remote_addr);
  bool cleanup_observed = false;
  if (ok && mem_registered) {
    cleanup_observed = WaitHeartbeatCleanup(&hixl, cfg, mem_handle, sample_cfg);
    mem_registered = !cleanup_observed;
  } else if (ok) {
    std::fprintf(stderr, "[ERROR] Sample requires registered client memory to verify DeregisterMem cleanup\n");
  }

  if (mem_registered) {
    (void)hixl.Disconnect(AscendString(remote_engine.c_str()), static_cast<int32_t>(cfg.connect_timeout_ms));
    const auto ret = hixl.DeregisterMem(mem_handle);
    if (ret != SUCCESS) {
      std::fprintf(stderr, "[WARN] Cleanup DeregisterMem failed, ret=%u, errmsg:%s\n", ret, RecentErrMsg());
    }
  }
  hixl.Finalize();
  FreeBuffer(buffer, is_host);
  (void)aclrtResetDevice(cfg.expanded_device_ids[0]);
  return cleanup_observed ? 0 : -1;
}

}  // namespace

int main(int argc, char **argv) {
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "-h" || arg == "--help") {
      PrintUsage(stdout);
      return 0;
    }
  }
  BenchmarkConfig bench_cfg;
  SampleConfig sample_cfg;
  if (!BuildConfigs(argc, argv, &bench_cfg, &sample_cfg)) {
    return -1;
  }
  std::printf("[INFO] auto_connect_heartbeat_sample role=%s local_engine=%s remote_engine=%s tcp_port=%u\n",
              bench_cfg.role == BenchmarkRole::kClient ? "client" : "server",
              bench_cfg.expanded_local_engines[0].c_str(), bench_cfg.expanded_remote_engines[0].c_str(),
              static_cast<unsigned>(bench_cfg.expanded_tcp_ports[0]));
  if (bench_cfg.role == BenchmarkRole::kServer) {
    return RunServer(bench_cfg, sample_cfg);
  }
  return RunClient(bench_cfg, sample_cfg);
}
