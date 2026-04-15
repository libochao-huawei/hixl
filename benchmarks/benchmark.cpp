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
#include <iostream>
#include <map>
#include <string>
#include <vector>
#include "common/tcp_client_server.h"
#include "common/benchmark_config.h"
#include "acl/acl.h"
#include "hixl/hixl.h"

using namespace hixl;
using namespace hixl_benchmark;

namespace {

constexpr int32_t kWaitTransTime = 20;

#define CHECK_ACL_RETURN(x)                                                                  \
  do {                                                                                       \
    aclError __ret = x;                                                                      \
    if (__ret != ACL_ERROR_NONE) {                                                         \
      std::cerr << __FILE__ << ":" << __LINE__ << " aclError:" << __ret << std::endl;        \
      return __ret;                                                                        \
    }                                                                                      \
  } while (0)

const char *GetRecentErrMsg() {
  const char *errmsg = aclGetRecentErrMsg();
  if (errmsg == nullptr) {
    return "no error";
  }
  return errmsg;
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

struct RunParam {
  TCPClient tcp_client;
  Hixl hixl;
  void *buffer = nullptr;
  MemHandle mem_handle = nullptr;
  bool is_host = false;
  bool need_register = false;
  bool hixl_initialized = false;
  bool hixl_connected = false;
  uint64_t remote_addr = 0;
};

bool GetRemoteAddr(TCPClient *tcp_client, const BenchmarkConfig &cfg, uint64_t *out_remote_addr) {
  if (!tcp_client->ConnectToServer(ExtractTcpHost(cfg.remote_engine), cfg.tcp_port)) {
    return false;
  }
  if (!tcp_client->ReceiveUint64(out_remote_addr)) {
    return false;
  }
  if (!tcp_client->ReceiveTaskStatus()) {
    return false;
  }
  return true;
}

bool SendNotify(TCPClient *tcp_client) { return tcp_client->SendTaskStatus(); }

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

void ReleaseHixlResources(Hixl &hixl_engine, bool need_register, bool is_host, const std::vector<MemHandle> &handles,
                          const std::vector<void *> &buffers = {}) {
  if (need_register) {
    for (const auto &handle : handles) {
      if (handle == nullptr) {
        continue;
      }
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
      if (buffer != nullptr) {
        aclrtFreeHost(buffer);
      }
    }
  } else {
    for (const auto &buffer : buffers) {
      if (buffer != nullptr) {
        aclrtFree(buffer);
      }
    }
  }
  hixl_engine.Finalize();
}

int32_t Initialize(const BenchmarkConfig &cfg, RunParam *p) {
  const std::map<AscendString, AscendString> init_options = BenchmarkConfigParser::BuildInitializeOptions(cfg);
  const auto ret = p->hixl.Initialize(cfg.local_engine.c_str(), init_options);
  if (ret != SUCCESS) {
    printf("[ERROR] Initialize failed, ret = %u, errmsg: %s\n", ret, GetRecentErrMsg());
    return -1;
  }
  p->hixl_initialized = true;
  printf("[INFO] Initialize success\n");
  return 0;
}

void Finalize(RunParam *p, const BenchmarkConfig &cfg) {
  if (p->hixl_connected) {
    const auto ret = p->hixl.Disconnect(cfg.remote_engine.c_str());
    if (ret != SUCCESS) {
      printf("[ERROR] Disconnect failed, ret = %u, errmsg: %s\n", ret, GetRecentErrMsg());
    } else {
      printf("[INFO] Disconnect success\n");
    }
    p->hixl_connected = false;
  }
  if (p->hixl_initialized) {
    if (p->buffer != nullptr) {
      ReleaseHixlResources(p->hixl, p->need_register, p->is_host, {p->mem_handle}, {p->buffer});
    } else {
      ReleaseHixlResources(p->hixl, p->need_register, p->is_host, {p->mem_handle}, {});
    }
    p->hixl_initialized = false;
    p->buffer = nullptr;
    p->mem_handle = nullptr;
  }
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

int32_t RunClient(const BenchmarkConfig &cfg, RunParam *p) {
  printf("[INFO] client start\n");

  if (ClientAllocSrcBuffer(cfg, &p->is_host, &p->buffer) != 0) {
    return -1;
  }
  p->need_register = !(p->is_host && cfg.use_buffer_pool);
  if (ClientRegisterMemIfNeeded(p->hixl, cfg, p->buffer, p->is_host, p->need_register, &p->mem_handle) != 0) {
    return -1;
  }

  if (!GetRemoteAddr(&p->tcp_client, cfg, &p->remote_addr)) {
    return -1;
  }
  if (p->remote_addr != 0U) {
    printf("[INFO] Success to receive server mem addr: 0x%" PRIx64 "\n", p->remote_addr);
  }
  printf("[INFO] Server RegisterMem success\n");

  const auto connect_ret = p->hixl.Connect(cfg.remote_engine.c_str());
  if (connect_ret != SUCCESS) {
    printf("[ERROR] Connect failed, ret = %u, errmsg: %s\n", connect_ret, GetRecentErrMsg());
    return -1;
  }
  printf("[INFO] Connect success\n");
  p->hixl_connected = true;

  if (Transfer(p->hixl, p->buffer, cfg.remote_engine.c_str(), p->remote_addr, cfg) != 0) {
    return -1;
  }

  const auto disconnect_ret = p->hixl.Disconnect(cfg.remote_engine.c_str());
  if (disconnect_ret != SUCCESS) {
    printf("[ERROR] Disconnect failed, ret = %u, errmsg: %s\n", disconnect_ret, GetRecentErrMsg());
  } else {
    printf("[INFO] Disconnect success\n");
  }
  p->hixl_connected = false;
  (void)SendNotify(&p->tcp_client);

  printf("[INFO] Client Sample end\n");
  return 0;
}

int32_t RunServer(const BenchmarkConfig &cfg, RunParam *p) {
  printf("[INFO] server start\n");

  p->is_host = (cfg.transfer_mode == "d2h" || cfg.transfer_mode == "h2h");
  const size_t alloc_size = static_cast<size_t>(cfg.total_size);
  if (p->is_host) {
    CHECK_ACL_RETURN(aclrtMallocHost(&p->buffer, alloc_size));
  } else {
    CHECK_ACL_RETURN(aclrtMalloc(&p->buffer, alloc_size, ACL_MEM_MALLOC_HUGE_ONLY));
  }
  const auto addr = reinterpret_cast<uintptr_t>(p->buffer);

  const auto mem_type = p->is_host ? MemType::MEM_HOST : MemType::MEM_DEVICE;
  p->need_register = !(cfg.use_buffer_pool && cfg.transfer_mode == "d2h");
  if (p->need_register) {
    MemDesc desc{};
    desc.addr = addr;
    desc.len = static_cast<size_t>(cfg.total_size);
    const auto ret = p->hixl.RegisterMem(desc, mem_type, p->mem_handle);
    if (ret != SUCCESS) {
      printf("[ERROR] RegisterMem failed, ret = %u, errmsg: %s\n", ret, GetRecentErrMsg());
      return -1;
    }
    printf("[INFO] RegisterMem success, addr:%p\n", p->buffer);
  }

  TcpServerSession bench_tcp(cfg.tcp_port, cfg.tcp_accept_wait_sec, cfg.tcp_client_count);
  if (!bench_tcp.WaitAndSendAddr(addr)) {
    return -1;
  }

  printf("[INFO] Wait transfer begin (N=%zu)\n", bench_tcp.ConnectedPeerCount());
  if (!bench_tcp.WaitAllNotify()) {
    return -1;
  }
  printf("[INFO] Wait transfer end\n");

  printf("[INFO] Server Sample end\n");
  return 0;
}

int32_t main(int32_t argc, char **argv) {
  if (argc <= 1) {
    BenchmarkConfigParser::PrintUsage(stderr);
    return -1;
  }
  BenchmarkConfig cfg;
  if (!BenchmarkConfigParser::BuildFromArgv(argc, argv, &cfg)) {
    return -1;
  }
  if (!BenchmarkConfigParser::Validate(cfg)) {
    return -1;
  }
  printf(
      "[INFO] role=%s device_id=%d local_engine=%s remote_engine=%s tcp_port=%u tcp_accept_wait_s=%" PRIu32
      " tcp_client_count=%" PRIu32 " transfer_mode=%s transfer_op=%s "
      "use_buffer_pool=%s total_size=%" PRIu64 " block_size=%" PRIu64 " block_steps=%u loops=%u\n",
      cfg.role == BenchmarkRole::kClient ? "client" : "server", static_cast<int>(cfg.device_id), cfg.local_engine.c_str(),
      cfg.remote_engine.c_str(), static_cast<unsigned>(cfg.tcp_port), cfg.tcp_accept_wait_sec, cfg.tcp_client_count,
      cfg.transfer_mode.c_str(), cfg.transfer_op.c_str(), cfg.use_buffer_pool ? "true" : "false", cfg.total_size,
      cfg.block_size, cfg.block_steps, cfg.loops);
  if (cfg.loops == 1U) {
    printf(
        "[INFO] loops=1: the first transfer is often warm-up; for steady throughput use the second repeat's "
        "metrics or set loops>1 (--loops|-n).\n");
  }
  {
    const std::map<AscendString, AscendString> eff = BenchmarkConfigParser::BuildInitializeOptions(cfg);
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

  RunParam param{};
  int32_t ret = Initialize(cfg, &param);
  if (ret == 0) {
    if (cfg.role == BenchmarkRole::kClient) {
      ret = RunClient(cfg, &param);
    } else {
      ret = RunServer(cfg, &param);
    }
  }
  Finalize(&param, cfg);

  CHECK_ACL_RETURN(aclrtResetDevice(cfg.device_id));
  return ret;
}
