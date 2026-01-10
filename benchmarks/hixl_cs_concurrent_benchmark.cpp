/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <acl/acl.h>
#include <arpa/inet.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "nlohmann/json.hpp"
#include "hixl/common/hixl_cs.h"
#include "hixl/hixl_types.h"

using json = nlohmann::json;

void from_json(const json &j, EndPointLocation &l) {
  std::string s = j.get<std::string>();
  if (s == "host") {
    l = END_POINT_LOCATION_HOST;
  } else {
    l = END_POINT_LOCATION_DEVICE;
  }
}

void from_json(const json &j, CommProtocol &p) {
  std::string s = j.get<std::string>();
  if (s == "hccs") {
    p = COMM_PROTOCOL_HCCS;
  } else if (s == "roce") {
    p = COMM_PROTOCOL_ROCE;
  } else if (s == "UB_CTP") {
    p = COMM_PROTOCOL_UB_CTP;
  } else if (s == "UB_TP") {
    p = COMM_PROTOCOL_UB_TP;
  } else {
    p = COMM_PROTOCOL_RESERVED;
  }
}

void from_json(const json &j, EndPointInfo &info) {
  j.at("location").get_to(info.location);
  j.at("protocol").get_to(info.protocol);

  std::string addr = "";
  j.at("addr").get_to(addr);

  if (info.protocol != COMM_PROTOCOL_ROCE) {
    return;
  }

  int32_t ok4 = inet_pton(AF_INET, addr.c_str(), &info.addr.addr);
  if (ok4 == 1) {
    info.addr.type = COMM_ADDR_TYPE_IP_V4;
    return;
  }

  int32_t ok6 = inet_pton(AF_INET6, addr.c_str(), &info.addr.addr6);
  if (ok6 == 1) {
    info.addr.type = COMM_ADDR_TYPE_IP_V6;
    return;
  }

  info.addr.type = COMM_ADDR_TYPE_RESERVED;
}

namespace {

constexpr int32_t kArgcExpected = 9;

constexpr uint32_t kArgIndexProg = 0U;
constexpr uint32_t kArgIndexDevId = 1U;
constexpr uint32_t kArgIndexIpPort = 2U;
constexpr uint32_t kArgIndexSrvNum = 3U;
constexpr uint32_t kArgIndexCliNum = 4U;
constexpr uint32_t kArgIndexMode = 5U;
constexpr uint32_t kArgIndexOp = 6U;
constexpr uint32_t kArgIndexLocalJson = 7U;
constexpr uint32_t kArgIndexRemoteJson = 8U;

constexpr uint32_t kTransferMemSizeBytes = 134217728U;
constexpr uint32_t kBlockSizeBytes = 4194304U;
constexpr uint32_t kRepeatNum = 5U;

constexpr int32_t kBackLog = 4096;
constexpr int32_t kConnectTimeoutMs = 30000;
constexpr int32_t kGetRemoteMemTimeoutMs = 30000;

constexpr int32_t kPollSleepMs = 1;
constexpr int32_t kServerIdleSleepMs = 100;

constexpr uint16_t kPortMax = 65535;

constexpr uint64_t kBytesPerGB = static_cast<uint64_t>(1024ULL * 1024ULL * 1024ULL);

constexpr const char *kServerMemTagName = "server_mem";
constexpr const char *kClientMemTagPrefix = "client_mem_";

constexpr int32_t kCompleteOk = 0;
constexpr int32_t kError = -1;
constexpr int32_t kSuccess = 0;

std::atomic<uint64_t> g_total_bytes{0ULL};
std::mutex g_log_mutex;

static size_t GetThreadIdHash() {
  size_t v = std::hash<std::thread::id>{}(std::this_thread::get_id());
  return v;
}

#define LOG_THREAD(fmt, ...)                                                \
  do {                                                                      \
    std::lock_guard<std::mutex> lock(g_log_mutex);                          \
    (void)std::printf("[T%zu] " fmt, GetThreadIdHash(), ##__VA_ARGS__);     \
  } while (false)

static bool AclOk(aclError ret, const char *what) {
  if (ret != ACL_ERROR_NONE) {
    LOG_THREAD("[ERROR] ACL %s failed, ret=%d\n", what, static_cast<int32_t>(ret));
    return false;
  }
  return true;
}

class CyclicBarrier {
 public:
  explicit CyclicBarrier(size_t count) : threshold_(count), count_(count) {}

  void Wait() {
    std::unique_lock<std::mutex> lock(mutex_);
    size_t gen = generation_;

    if (threshold_ == 0U) {
      return;
    }

    if (count_ == 0U) {
      return;
    }

    count_ -= 1U;

    if (count_ == 0U) {
      generation_ += 1U;
      count_ = threshold_;
      cv_.notify_all();
      return;
    }

    auto predicate = [this, gen]() -> bool {
      return gen != generation_;
    };

    cv_.wait(lock, predicate);
  }

 private:
  std::mutex mutex_;
  std::condition_variable cv_;
  size_t threshold_;
  size_t count_;
  size_t generation_ = 0U;
};

struct TestConfig {
  int32_t device_id;
  std::string base_ip;
  uint16_t base_port;
  uint32_t num_servers;
  uint32_t num_clients;
  std::string local_json;
  std::string remote_json;
  std::string op;
  bool server_use_host_mem;
  bool client_use_host_mem;
};

struct ServerCtx {
  uint32_t idx;
  const TestConfig *cfg;
  CyclicBarrier *ready_barrier;
  std::atomic<bool> *stop_flag;
};

struct ClientCtx {
  uint32_t idx;
  const TestConfig *cfg;
  CyclicBarrier *conn_barrier;
  CyclicBarrier *trans_barrier;
};

struct ServerRuntime {
  HixlServerHandle server_handle;
  MemHandle mem_handle;
  HcclMem mem;
};

struct ClientRuntime {
  HixlClientHandle client_handle;
  MemHandle mem_handle;
  HcclMem local_mem;
  void *remote_addr;
  std::vector<const void *> l_addrs;
  std::vector<void *> r_addrs;
  std::vector<uint64_t> lens;
};

static int32_t ParseEndpoint(const std::string &json_str, EndPointInfo &ep) {
  try {
    ep = json::parse(json_str).get<EndPointInfo>();
  } catch (const std::exception &e) {
    LOG_THREAD("[ERROR] ParseEndpoint failed: %s\n", e.what());
    return kError;
  }
  return kSuccess;
}

static int32_t SplitIpPort(const std::string &ip_port, std::string &ip, uint16_t &port) {
  size_t pos = ip_port.find(':');
  if (pos == std::string::npos) {
    return kError;
  }

  std::string ip_str = ip_port.substr(0U, pos);
  std::string port_str = ip_port.substr(pos + 1U);

  int64_t p64 = std::stoll(port_str);
  if (p64 < 0) {
    return kError;
  }

  if (p64 > static_cast<int64_t>(kPortMax)) {
    return kError;
  }

  ip = ip_str;
  port = static_cast<uint16_t>(p64);
  return kSuccess;
}

static int32_t ParseMode(const std::string &mode, bool &cli_host, bool &srv_host) {
  if (mode == "d2d") {
    cli_host = false;
    srv_host = false;
    return kSuccess;
  }

  if (mode == "h2d") {
    cli_host = true;
    srv_host = false;
    return kSuccess;
  }

  if (mode == "d2h") {
    cli_host = false;
    srv_host = true;
    return kSuccess;
  }

  if (mode == "h2h") {
    cli_host = true;
    srv_host = true;
    return kSuccess;
  }

  return kError;
}

static bool AllocHcclMem(bool is_host, uint32_t size, HcclMem &mem) {
  mem.addr = nullptr;
  mem.size = size;

  if (is_host) {
    mem.type = HCCL_MEM_TYPE_HOST;
    aclError r = aclrtMallocHost(&mem.addr, size);
    return AclOk(r, "aclrtMallocHost");
  }

  mem.type = HCCL_MEM_TYPE_DEVICE;
  aclError r = aclrtMalloc(&mem.addr, size, ACL_MEM_MALLOC_HUGE_ONLY);
  return AclOk(r, "aclrtMalloc");
}

static void FreeHcclMem(bool is_host, void *addr) {
  if (addr == nullptr) {
    return;
  }

  if (is_host) {
    (void)aclrtFreeHost(addr);
  } else {
    (void)aclrtFree(addr);
  }
}

static uint32_t CalcServerPortU32(const TestConfig &cfg, uint32_t server_idx) {
  uint32_t base = static_cast<uint32_t>(cfg.base_port);
  uint32_t port_u32 = base + server_idx;
  return port_u32;
}

static bool ValidatePortRange(const TestConfig &cfg) {
  uint32_t last_port_u32 = static_cast<uint32_t>(cfg.base_port) + cfg.num_servers - 1U;
  if (last_port_u32 > static_cast<uint32_t>(kPortMax)) {
    return false;
  }
  return true;
}

static void CleanupServerRuntime(const TestConfig &cfg, ServerRuntime &rt) {
  if (rt.server_handle != nullptr) {
    if (rt.mem_handle != nullptr) {
      (void)HixlCSServerUnregMem(rt.server_handle, rt.mem_handle);
    }
    (void)HixlCSServerDestroy(rt.server_handle);
  }

  FreeHcclMem(cfg.server_use_host_mem, rt.mem.addr);

  rt.server_handle = nullptr;
  rt.mem_handle = nullptr;
  rt.mem.addr = nullptr;
}

static bool ServerCreateListen(const ServerCtx &ctx, const EndPointInfo &ep, ServerRuntime &rt) {
  const TestConfig &cfg = *(ctx.cfg);

  uint32_t my_port_u32 = CalcServerPortU32(cfg, ctx.idx);

  HixlServerConfig config = {};

  rt.server_handle = nullptr;

  HixlStatus ret = HixlCSServerCreate(cfg.base_ip.c_str(), my_port_u32, &ep, 1U, &config, &rt.server_handle);
  if (ret != HIXL_SUCCESS) {
    LOG_THREAD("[Server %u] Create failed, ret=%u\n", ctx.idx, ret);
    return false;
  }

  ret = HixlCSServerListen(rt.server_handle, kBackLog);
  if (ret != HIXL_SUCCESS) {
    LOG_THREAD("[Server %u] Listen failed, ret=%u\n", ctx.idx, ret);
    return false;
  }

  return true;
}

static bool ServerAllocRegMem(const ServerCtx &ctx, ServerRuntime &rt) {
  const TestConfig &cfg = *(ctx.cfg);

  rt.mem = {};
  rt.mem_handle = nullptr;

  bool ok = AllocHcclMem(cfg.server_use_host_mem, kTransferMemSizeBytes, rt.mem);
  if (!ok) {
    return false;
  }

  HixlStatus ret = HixlCSServerRegMem(rt.server_handle, kServerMemTagName, &rt.mem, &rt.mem_handle);
  if (ret != HIXL_SUCCESS) {
    LOG_THREAD("[Server %u] RegMem failed, ret=%u\n", ctx.idx, ret);
    return false;
  }

  uint32_t my_port_u32 = CalcServerPortU32(cfg, ctx.idx);

  LOG_THREAD("[Server %u] Ready on %s:%u, Mem=%s\n",
             ctx.idx,
             cfg.base_ip.c_str(),
             my_port_u32,
             cfg.server_use_host_mem ? "Host" : "Device");

  return true;
}

static void ServerThreadMain(ServerCtx *ctx) {
  const TestConfig &cfg = *(ctx->cfg);
  (void)AclOk(aclrtSetDevice(cfg.device_id), "aclrtSetDevice");
  EndPointInfo ep = {};
  int32_t p = ParseEndpoint(cfg.local_json, ep);
  if (p != kSuccess) {
    ctx->ready_barrier->Wait();
    return;
  }
  ServerRuntime rt = {};
  bool ok = ServerCreateListen(*ctx, ep, rt);
  if (!ok) {
    CleanupServerRuntime(cfg, rt);
    ctx->ready_barrier->Wait();
    return;
  }
  ok = ServerAllocRegMem(*ctx, rt);
  if (!ok) {
    CleanupServerRuntime(cfg, rt);
    ctx->ready_barrier->Wait();
    return;
  }
  ctx->ready_barrier->Wait();
  while (!ctx->stop_flag->load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(kServerIdleSleepMs));
  }
  CleanupServerRuntime(cfg, rt);
}

static void CleanupClientRuntime(const TestConfig &cfg, ClientRuntime &rt) {
  if (rt.client_handle != nullptr) {
    if (rt.mem_handle != nullptr) {
      (void)HixlCSClientUnregMem(rt.client_handle, rt.mem_handle);
    }
    (void)HixlCSClientDestroy(rt.client_handle);
  }
  FreeHcclMem(cfg.client_use_host_mem, rt.local_mem.addr);
  rt.client_handle = nullptr;
  rt.mem_handle = nullptr;
  rt.local_mem.addr = nullptr;
  rt.remote_addr = nullptr;
}

static bool FindServerMemAddr(uint32_t idx,
                              HcclMem *remote_mem_list,
                              char **mem_tag_list,
                              uint32_t list_num,
                              void *&remote) {
  remote = nullptr;
  if (remote_mem_list == nullptr) {
    return false;
  }
  if (mem_tag_list == nullptr) {
    return false;
  }
  if (list_num == 0U) {
    return false;
  }
  for (uint32_t i = 0U; i < list_num; ++i) {
    if (mem_tag_list[i] == nullptr) {
      continue;
    }
    std::string tag = std::string(mem_tag_list[i]);
    if (tag == std::string(kServerMemTagName)) {
      remote = remote_mem_list[i].addr;
      break;
    }
  }
  if (remote == nullptr) {
    LOG_THREAD("[Client %u] server_mem tag not found\n", idx);
    return false;
  }
  return true;
}

static bool BuildIoLists(ClientRuntime &rt) {
  if (rt.local_mem.addr == nullptr) {
    return false;
  }
  if (rt.remote_addr == nullptr) {
    return false;
  }
  uint32_t trans_num_u32 = kTransferMemSizeBytes / kBlockSizeBytes;
  rt.l_addrs.clear();
  rt.r_addrs.clear();
  rt.lens.clear();
  rt.l_addrs.reserve(trans_num_u32);
  rt.r_addrs.reserve(trans_num_u32);
  rt.lens.reserve(trans_num_u32);
  uint8_t *l_ptr = static_cast<uint8_t *>(rt.local_mem.addr);
  uint8_t *r_ptr = static_cast<uint8_t *>(rt.remote_addr);
  for (uint32_t i = 0U; i < trans_num_u32; ++i) {
    uint32_t off = i * kBlockSizeBytes;
    rt.l_addrs.push_back(l_ptr + off);
    rt.r_addrs.push_back(r_ptr + off);
    uint64_t len = static_cast<uint64_t>(kBlockSizeBytes);
    rt.lens.push_back(len);
  }
  return true;
}

static bool WaitComplete(HixlClientHandle client_handle, void *complete_handle) {
  while (true) {
    int32_t status = kCompleteOk;
    HixlStatus ret = HixlCSClientQueryCompleteStatus(client_handle, complete_handle, &status);
    if (ret == HIXL_SUCCESS) {
      if (status == kCompleteOk) {
        return true;
      }
      return false;
    }
    if (ret != HIXL_NOT_READY) {
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(kPollSleepMs));
  }
}

static bool DoOneBatch(const TestConfig &cfg, ClientRuntime &rt) {
  void *complete_handle = nullptr;
  uint32_t trans_num_u32 = static_cast<uint32_t>(rt.lens.size());
  HixlStatus ret = HIXL_SUCCESS;
  if (cfg.op == "write") {
    ret = HixlCSClientBatchput(rt.client_handle,
                               trans_num_u32,
                               rt.r_addrs.data(),
                               rt.l_addrs.data(),
                               rt.lens.data(),
                               &complete_handle);
  } else {
    ret = HixlCSClientBatchget(rt.client_handle,
                               trans_num_u32,
                               rt.r_addrs.data(),
                               rt.l_addrs.data(),
                               rt.lens.data(),
                               &complete_handle);
  }
  if (ret != HIXL_SUCCESS) {
    return false;
  }
  bool ok = WaitComplete(rt.client_handle, complete_handle);
  return ok;
}

static bool ClientCreateConnect(const ClientCtx &ctx, ClientRuntime &rt, EndPointInfo &local_ep, EndPointInfo &remote_ep) {
  const TestConfig &cfg = *(ctx.cfg);
  int32_t pl = ParseEndpoint(cfg.local_json, local_ep);
  int32_t pr = ParseEndpoint(cfg.remote_json, remote_ep);
  if (pl != kSuccess) {
    return false;
  }
  if (pr != kSuccess) {
    return false;
  }
  uint32_t target_server = ctx.idx % cfg.num_servers;
  uint32_t target_port_u32 = CalcServerPortU32(cfg, target_server);
  int32_t target_port_i32 = static_cast<int32_t>(target_port_u32);
  rt.client_handle = nullptr;
  HixlStatus ret = HixlCSClientCreate(cfg.base_ip.c_str(), target_port_i32, &local_ep, &remote_ep, &rt.client_handle);
  if (ret != HIXL_SUCCESS) {
    LOG_THREAD("[Client %u] Create failed, ret=%u\n", ctx.idx, ret);
    return false;
  }
  ctx.conn_barrier->Wait();
  ret = HixlCSClientConnectSync(rt.client_handle, kConnectTimeoutMs);
  if (ret != HIXL_SUCCESS) {
    LOG_THREAD("[Client %u] Connect failed, ret=%u\n", ctx.idx, ret);
    return false;
  }
  return true;
}

static bool ClientAllocRegMem(const ClientCtx &ctx, ClientRuntime &rt) {
  const TestConfig &cfg = *(ctx.cfg);
  rt.local_mem = {};
  rt.mem_handle = nullptr;
  bool ok = AllocHcclMem(cfg.client_use_host_mem, kTransferMemSizeBytes, rt.local_mem);
  if (!ok) {
    return false;
  }
  std::string tag = std::string(kClientMemTagPrefix) + std::to_string(ctx.idx);
  HixlStatus ret = HixlCSClientRegMem(rt.client_handle, tag.c_str(), &rt.local_mem, &rt.mem_handle);
  if (ret != HIXL_SUCCESS) {
    LOG_THREAD("[Client %u] RegMem failed, ret=%u\n", ctx.idx, ret);
    return false;
  }

  return true;
}

static bool ClientGetRemoteAddr(const ClientCtx &ctx, ClientRuntime &rt) {
  HcclMem *remote_mem_list = nullptr;
  char **mem_tag_list = nullptr;
  uint32_t list_num = 0U;
  HixlStatus ret = HixlCSClientGetRemoteMem(rt.client_handle,
                                 &remote_mem_list,
                                 &mem_tag_list,
                                 &list_num,
                                 kGetRemoteMemTimeoutMs);
  if (ret != HIXL_SUCCESS) {
    LOG_THREAD("[Client %u] GetRemoteMem failed, ret=%u\n", ctx.idx, ret);
    return false;
  }
  bool ok = FindServerMemAddr(ctx.idx, remote_mem_list, mem_tag_list, list_num, rt.remote_addr);
  return ok;
}

static void ClientThreadMain(ClientCtx *ctx) {
  const TestConfig &cfg = *(ctx->cfg);
  (void)AclOk(aclrtSetDevice(cfg.device_id), "aclrtSetDevice");
  ClientRuntime rt = {};
  rt.remote_addr = nullptr;
  EndPointInfo local_ep = {};
  EndPointInfo remote_ep = {};
  bool ok = ClientCreateConnect(*ctx, rt, local_ep, remote_ep);
  if (!ok) {
    CleanupClientRuntime(cfg, rt);
    return;
  }
  ok = ClientAllocRegMem(*ctx, rt);
  if (!ok) {
    CleanupClientRuntime(cfg, rt);
    return;
  }
  ok = ClientGetRemoteAddr(*ctx, rt);
  if (!ok) {
    CleanupClientRuntime(cfg, rt);
    return;
  }
  ok = BuildIoLists(rt);
  if (!ok) {
    CleanupClientRuntime(cfg, rt);
    return;
  }
  ctx->trans_barrier->Wait();
  for (uint32_t r = 0U; r < kRepeatNum; ++r) {
    ok = DoOneBatch(cfg, rt);
    if (!ok) {
      LOG_THREAD("[Client %u] Batch failed at iter=%u\n", ctx->idx, r);
      break;
    }
    (void)g_total_bytes.fetch_add(static_cast<uint64_t>(kTransferMemSizeBytes), std::memory_order_relaxed);
  }
  CleanupClientRuntime(cfg, rt);
}

static int32_t ParseArgs(int argc, char **argv, TestConfig &cfg) {
  if (argc != kArgcExpected) {
    (void)std::printf("Usage:\n");
    (void)std::printf("  %s <dev_id> <ip:port> <srv_num> <cli_num> <mode> <op> <local_json> <remote_json>\n",
                      argv[kArgIndexProg]);
    (void)std::printf("  mode: d2d | h2d | d2h | h2h\n");
    (void)std::printf("  op  : write | read\n");
    return kError;
  }
  cfg.device_id = std::stoi(argv[kArgIndexDevId]);
  std::string ip = "";
  uint16_t port = 0U;
  int32_t sp = SplitIpPort(argv[kArgIndexIpPort], ip, port);
  if (sp != kSuccess) {
    return kError;
  }
  cfg.base_ip = ip;
  cfg.base_port = port;
  cfg.num_servers = static_cast<uint32_t>(std::stoul(argv[kArgIndexSrvNum]));
  cfg.num_clients = static_cast<uint32_t>(std::stoul(argv[kArgIndexCliNum]));
  if (cfg.num_servers == 0U) {
    return kError;
  }
  if (cfg.num_clients == 0U) {
    return kError;
  }
  cfg.op = argv[kArgIndexOp];
  cfg.local_json = argv[kArgIndexLocalJson];
  cfg.remote_json = argv[kArgIndexRemoteJson];
  bool cli_host = false;
  bool srv_host = false;
  int32_t pm = ParseMode(argv[kArgIndexMode], cli_host, srv_host);
  if (pm != kSuccess) {
    return kError;
  }
  cfg.client_use_host_mem = cli_host;
  cfg.server_use_host_mem = srv_host;
  if (!ValidatePortRange(cfg)) {
    return kError;
  }
  if (cfg.op != "write" && cfg.op != "read") {
    return kError;
  }
  return kSuccess;
}

static void PrintConfig(const TestConfig &cfg) {
  (void)std::printf("=== M-to-N Concurrent Benchmark ===\n");
  (void)std::printf("  Servers: %u, Base: %s:%u\n", cfg.num_servers, cfg.base_ip.c_str(), cfg.base_port);
  (void)std::printf("  Clients: %u\n", cfg.num_clients);
  (void)std::printf("  Op     : %s\n", cfg.op.c_str());
  (void)std::printf("  Mem    : Server=%s, Client=%s\n",
                    cfg.server_use_host_mem ? "Host" : "Device",
                    cfg.client_use_host_mem ? "Host" : "Device");
  (void)std::printf("  Size   : total=%u bytes, block=%u bytes, repeat=%u\n",
                    kTransferMemSizeBytes,
                    kBlockSizeBytes,
                    kRepeatNum);
}

static void JoinAll(std::vector<std::thread> &threads) {
  for (auto &t : threads) {
    if (t.joinable()) {
      t.join();
    }
  }
}

static void BuildServerCtxs(const TestConfig &cfg,
                            CyclicBarrier &ready_barrier,
                            std::atomic<bool> &stop_flag,
                            std::vector<ServerCtx> &server_ctxs) {
  server_ctxs.clear();
  server_ctxs.reserve(cfg.num_servers);

  for (uint32_t i = 0U; i < cfg.num_servers; ++i) {
    ServerCtx sctx = {};
    sctx.idx = i;
    sctx.cfg = &cfg;
    sctx.ready_barrier = &ready_barrier;
    sctx.stop_flag = &stop_flag;
    server_ctxs.push_back(sctx);
  }
}

static void LaunchServerThreads(const TestConfig &cfg,
                                std::vector<ServerCtx> &server_ctxs,
                                std::vector<std::thread> &server_threads) {
  server_threads.clear();
  server_threads.reserve(cfg.num_servers);

  for (uint32_t i = 0U; i < cfg.num_servers; ++i) {
    server_threads.emplace_back(ServerThreadMain, &server_ctxs[i]);
  }
}

static void BuildClientCtxs(const TestConfig &cfg,
                            CyclicBarrier &conn_barrier,
                            CyclicBarrier &trans_barrier,
                            std::vector<ClientCtx> &client_ctxs) {
  client_ctxs.clear();
  client_ctxs.reserve(cfg.num_clients);

  for (uint32_t i = 0U; i < cfg.num_clients; ++i) {
    ClientCtx cctx = {};
    cctx.idx = i;
    cctx.cfg = &cfg;
    cctx.conn_barrier = &conn_barrier;
    cctx.trans_barrier = &trans_barrier;
    client_ctxs.push_back(cctx);
  }
}

static void LaunchClientThreads(const TestConfig &cfg,
                                std::vector<ClientCtx> &client_ctxs,
                                std::vector<std::thread> &client_threads) {
  client_threads.clear();
  client_threads.reserve(cfg.num_clients);

  for (uint32_t i = 0U; i < cfg.num_clients; ++i) {
    client_threads.emplace_back(ClientThreadMain, &client_ctxs[i]);
  }
}

static int64_t CalcElapsedMs(const std::chrono::steady_clock::time_point &start,
                            const std::chrono::steady_clock::time_point &end) {
  int64_t ms = 0;
  ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
  return ms;
}

static void PrintResultMs(int64_t elapsed_ms) {
  uint64_t total = 0ULL;
  total = g_total_bytes.load(std::memory_order_relaxed);

  double gb = 0.0;
  gb = static_cast<double>(total) / static_cast<double>(kBytesPerGB);

  double sec = 0.0;
  sec = static_cast<double>(elapsed_ms) / 1000.0;//1000.0:换算单位

  (void)std::printf("\n=== Result ===\n");
  (void)std::printf("  Total Data : %.3f GB\n", gb);
  (void)std::printf("  Total Time : %lld ms\n", static_cast<long long>(elapsed_ms));
  (void)std::printf("  Total Time : %.3f s\n", sec);

  if (elapsed_ms > 0) {
    double throughput = 0.0;
    throughput = gb / sec;
    (void)std::printf("  Throughput : %.3f GB/s\n", throughput);
  }
}

static void RunBenchmark(const TestConfig &cfg) {
  g_total_bytes.store(0ULL, std::memory_order_relaxed);
  std::atomic<bool> stop_flag;
  stop_flag.store(false);
  CyclicBarrier ready_barrier(static_cast<size_t>(cfg.num_servers) + 1U);
  CyclicBarrier conn_barrier(static_cast<size_t>(cfg.num_clients));
  CyclicBarrier trans_barrier(static_cast<size_t>(cfg.num_clients));
  std::vector<ServerCtx> server_ctxs;
  std::vector<std::thread> server_threads;
  BuildServerCtxs(cfg, ready_barrier, stop_flag, server_ctxs);
  LaunchServerThreads(cfg, server_ctxs, server_threads);
  ready_barrier.Wait();
  (void)std::printf("[INFO] All servers ready.\n");
  std::vector<ClientCtx> client_ctxs;
  std::vector<std::thread> client_threads;
  BuildClientCtxs(cfg, conn_barrier, trans_barrier, client_ctxs);
  const auto start = std::chrono::steady_clock::now();
  LaunchClientThreads(cfg, client_ctxs, client_threads);
  JoinAll(client_threads);
  const auto end = std::chrono::steady_clock::now();
  stop_flag.store(true);
  JoinAll(server_threads);
  int64_t elapsed_ms = 0;
  elapsed_ms = CalcElapsedMs(start, end);
  PrintResultMs(elapsed_ms);
}


}  // namespace

int main(int argc, char **argv) {
  TestConfig cfg = {};

  int32_t ok = ParseArgs(argc, argv, cfg);
  if (ok != kSuccess) {
    return kError;
  }

  if (!AclOk(aclInit(nullptr), "aclInit")) {
    return kError;
  }

  if (!AclOk(aclrtSetDevice(cfg.device_id), "aclrtSetDevice")) {
    (void)aclFinalize();
    return kError;
  }

  PrintConfig(cfg);
  RunBenchmark(cfg);

  (void)AclOk(aclrtResetDevice(cfg.device_id), "aclrtResetDevice");
  (void)AclOk(aclFinalize(), "aclFinalize");

  return kSuccess;
}
