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
#include <cctype>
#include <chrono>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <fstream>
#include <iostream>
#include <map>
#include <numeric>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "acl/acl.h"
#include "fabric_mem/fabric_mem_transfer_service.h"
#include "hixl/hixl.h"
#include "kvstore.h"
#include "model_config.h"

namespace {

constexpr std::uint64_t kBytesPerKiB = 1024ULL;
constexpr std::uint64_t kBytesPerMiB = kBytesPerKiB * 1024ULL;
constexpr std::uint64_t kBytesPerGiB = kBytesPerMiB * 1024ULL;
constexpr std::uintptr_t kFakeBufferBase = 0x100000000ULL;
constexpr std::uint32_t kDefaultProcessCount = 8U;
constexpr std::uint32_t kDefaultBatchSize = 128U;
constexpr std::uint64_t kDefaultSeed = 1234ULL;
constexpr std::uint32_t kDefaultBasePort = 19000U;
constexpr std::uint32_t kDefaultWarmup = 1U;
constexpr std::uint32_t kDefaultRepeat = 10U;
constexpr std::uint32_t kDefaultSyncTimeoutSec = 300U;
constexpr std::int32_t kDefaultConnectTimeoutMs = 60000;
constexpr std::int32_t kDefaultTransferTimeoutMs = 600000;

using hixl::AscendString;
using hixl::FabricMemTransferService;
using hixl::Hixl;
using hixl::MemDesc;
using hixl::MemHandle;
using hixl::MemType;
using hixl::SUCCESS;
using hixl::TransferOp;
using hixl::TransferOpDesc;
using hixl_kv_benchmark::BufferView;
using hixl_kv_benchmark::FindModelSpec;
using hixl_kv_benchmark::KvStore;
using hixl_kv_benchmark::LoadModelSpecsFromJson;
using hixl_kv_benchmark::ModelSpec;
using hixl_kv_benchmark::ParseTokenLength;
using hixl_kv_benchmark::SegmentManager;
using hixl_kv_benchmark::SupportedModelNames;

struct KvBenchConfig {
  std::uint32_t rank = 0U;
  std::uint32_t num_processes = kDefaultProcessCount;
  std::int32_t device_id = 0;
  std::uint64_t segment_size = 10ULL * kBytesPerGiB;
  std::string pool_memory = "host";
  std::string model = "deepseek-r1";
  std::string model_config = "kv_benchmark/config/models.json";
  std::string key_counts;
  std::string token_lengths;
  std::uint32_t batch_size = kDefaultBatchSize;
  std::string op_type = "put_get";
  std::uint64_t seed = kDefaultSeed;
  std::string transport = "fabric_mem";
  std::string output_dir = "kv_benchmark/output";
  std::string run_id = "manual";
  std::string listen_host = "127.0.0.1";
  std::string connect_host = "127.0.0.1";
  std::uint32_t base_port = kDefaultBasePort;
  std::uint32_t warmup = kDefaultWarmup;
  std::uint32_t repeat = kDefaultRepeat;
  std::uint32_t sync_timeout_sec = kDefaultSyncTimeoutSec;
};

struct KvWorkload {
  std::uint64_t token_length = 0U;
  std::uint64_t key_count = 0U;
  std::uint64_t key_size = 0U;
  std::uint64_t total_bytes = 0U;
};

struct TimingStats {
  double avg_us = 0.0;
  double p99_us = 0.0;
};

struct KvBenchResult {
  std::string model;
  std::uint64_t token_length = 0U;
  std::uint64_t key_count = 0U;
  std::uint32_t tokens_per_key = 0U;
  std::uint64_t key_size = 0U;
  std::uint64_t total_bytes = 0U;
  double put_bandwidth_gbps = 0.0;
  double get_bandwidth_gbps = 0.0;
  double put_avg_us = 0.0;
  double get_avg_us = 0.0;
  double put_p99_us = 0.0;
  double get_p99_us = 0.0;
};

struct RankMeta {
  std::uint32_t rank = 0U;
  std::string endpoint;
  std::uintptr_t pool_addr = 0U;
  std::uint64_t pool_size = 0U;
};

struct KvRuntime {
  Hixl hixl;
  void *local_buffer = nullptr;
  void *pool_buffer = nullptr;
  MemHandle local_handle = nullptr;
  MemHandle pool_handle = nullptr;
  bool device_bound = false;
  bool hixl_initialized = false;
  bool local_registered = false;
  bool pool_registered = false;
};

const char *RecentErrMsg() {
  const char *errmsg = aclGetRecentErrMsg();
  return errmsg == nullptr ? "no error" : errmsg;
}

std::vector<std::string> SplitComma(const std::string &value) {
  std::vector<std::string> parts;
  std::string current;
  for (char ch : value) {
    if (ch == ',') {
      if (!current.empty()) {
        parts.push_back(current);
      }
      current.clear();
      continue;
    }
    if (ch != ' ') {
      current.push_back(ch);
    }
  }
  if (!current.empty()) {
    parts.push_back(current);
  }
  return parts;
}

std::uint64_t ParseSize(const std::string &value) {
  if (value.empty()) {
    throw std::invalid_argument("empty size");
  }
  std::string digits = value;
  std::uint64_t multiplier = 1U;
  const char suffix = static_cast<char>(std::toupper(static_cast<unsigned char>(digits.back())));
  if (suffix == 'K' || suffix == 'M' || suffix == 'G') {
    digits.pop_back();
    multiplier = suffix == 'K' ? kBytesPerKiB : (suffix == 'M' ? kBytesPerMiB : kBytesPerGiB);
  }
  std::size_t pos = 0U;
  const auto parsed = std::stoull(digits, &pos, 10);
  if (pos != digits.size()) {
    throw std::invalid_argument("invalid size: " + value);
  }
  return parsed * multiplier;
}

std::map<std::string, std::string> CollectArgs(int argc, char **argv) {
  std::map<std::string, std::string> args;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    const auto pos = arg.find('=');
    if (pos == std::string::npos || pos == 0U) {
      throw std::invalid_argument("expect --key=value, got: " + arg);
    }
    args[arg.substr(0, pos)] = arg.substr(pos + 1U);
  }
  return args;
}

std::uint32_t ParseU32(const std::map<std::string, std::string> &args, const std::string &key, std::uint32_t value) {
  const auto it = args.find(key);
  if (it == args.end()) {
    return value;
  }
  return static_cast<std::uint32_t>(std::stoul(it->second));
}

std::uint64_t ParseU64(const std::map<std::string, std::string> &args, const std::string &key, std::uint64_t value) {
  const auto it = args.find(key);
  if (it == args.end()) {
    return value;
  }
  return static_cast<std::uint64_t>(std::stoull(it->second));
}

KvBenchConfig ParseConfig(int argc, char **argv) {
  const auto args = CollectArgs(argc, argv);
  KvBenchConfig cfg;
  cfg.rank = ParseU32(args, "--rank", cfg.rank);
  cfg.num_processes = ParseU32(args, "--num_processes", cfg.num_processes);
  cfg.device_id = static_cast<std::int32_t>(ParseU32(args, "--device_id", static_cast<std::uint32_t>(cfg.device_id)));
  cfg.batch_size = ParseU32(args, "--batch_size", cfg.batch_size);
  cfg.seed = ParseU64(args, "--seed", cfg.seed);
  cfg.base_port = ParseU32(args, "--base_port", cfg.base_port);
  cfg.warmup = ParseU32(args, "--warmup", cfg.warmup);
  cfg.repeat = ParseU32(args, "--repeat", cfg.repeat);
  cfg.sync_timeout_sec = ParseU32(args, "--sync_timeout_sec", cfg.sync_timeout_sec);
  if (args.count("--segment_size") != 0U) cfg.segment_size = ParseSize(args.at("--segment_size"));
  if (args.count("--pool_memory") != 0U) cfg.pool_memory = args.at("--pool_memory");
  if (args.count("--model") != 0U) cfg.model = args.at("--model");
  if (args.count("--model_config") != 0U) cfg.model_config = args.at("--model_config");
  if (args.count("--key_counts") != 0U) cfg.key_counts = args.at("--key_counts");
  if (args.count("--token_lengths") != 0U) cfg.token_lengths = args.at("--token_lengths");
  if (args.count("--op_type") != 0U) cfg.op_type = args.at("--op_type");
  if (args.count("--transport") != 0U) cfg.transport = args.at("--transport");
  if (args.count("--output_dir") != 0U) cfg.output_dir = args.at("--output_dir");
  if (args.count("--run_id") != 0U) cfg.run_id = args.at("--run_id");
  if (args.count("--listen_host") != 0U) cfg.listen_host = args.at("--listen_host");
  if (args.count("--connect_host") != 0U) cfg.connect_host = args.at("--connect_host");
  if (cfg.key_counts.empty() && cfg.token_lengths.empty()) {
    cfg.key_counts = "16,32,48,64";
  }
  return cfg;
}

bool ValidateConfig(const KvBenchConfig &cfg) {
  const bool transport_ok = cfg.transport == "hccs" || cfg.transport == "rdma" || cfg.transport == "fabric_mem";
  const bool workload_ok =
      !(cfg.key_counts.empty() && cfg.token_lengths.empty()) && (cfg.key_counts.empty() || cfg.token_lengths.empty());
  return cfg.num_processes > 0U && cfg.rank < cfg.num_processes && cfg.batch_size > 0U && cfg.repeat > 0U &&
         cfg.pool_memory == "host" && transport_ok &&
         (cfg.op_type == "put" || cfg.op_type == "get" || cfg.op_type == "put_get") && workload_ok;
}

void ApplyTransportEnvironment(const KvBenchConfig &cfg) {
  if (cfg.transport != "rdma") {
    return;
  }
  if (setenv("HCCL_INTRA_ROCE_ENABLE", "1", 1) != 0) {
    throw std::runtime_error("set HCCL_INTRA_ROCE_ENABLE=1 failed");
  }
}

std::map<AscendString, AscendString> BuildInitializeOptions(const KvBenchConfig &cfg) {
  std::map<AscendString, AscendString> options;
  options[AscendString(hixl::OPTION_BUFFER_POOL)] = AscendString("0:0");
  if (cfg.transport == "fabric_mem") {
    options[AscendString(hixl::OPTION_ENABLE_USE_FABRIC_MEM)] = AscendString("1");
  }
  return options;
}

SegmentManager BuildSegmentManager(const KvBenchConfig &cfg) {
  SegmentManager manager;
  for (std::uint32_t i = 0; i < cfg.num_processes; ++i) {
    manager.AddSegment(i, cfg.segment_size);
  }
  return manager;
}

std::vector<std::string> BuildKeys(std::uint32_t rank, std::uint64_t token_length, std::uint64_t key_count,
                                   bool shared) {
  std::vector<std::string> keys;
  keys.reserve(static_cast<std::size_t>(key_count));
  for (std::uint64_t i = 0U; i < key_count; ++i) {
    if (shared) {
      keys.push_back("tokens" + std::to_string(token_length) + "_key" + std::to_string(i));
    } else {
      keys.push_back("rank" + std::to_string(rank) + "_tokens" + std::to_string(token_length) + "_key" +
                     std::to_string(i));
    }
  }
  return keys;
}

std::vector<BufferView> BuildBuffers(std::uintptr_t base, std::uint64_t key_count, std::uint64_t key_size) {
  std::vector<BufferView> buffers;
  buffers.reserve(static_cast<std::size_t>(key_count));
  for (std::uint64_t i = 0U; i < key_count; ++i) {
    buffers.push_back(BufferView{base + static_cast<std::uintptr_t>(i * key_size), key_size});
  }
  return buffers;
}

std::vector<KvWorkload> BuildWorkloads(const KvBenchConfig &cfg, const ModelSpec &model) {
  std::vector<KvWorkload> workloads;
  const auto key_size = static_cast<std::uint64_t>(model.BytesPerKey());
  if (!cfg.key_counts.empty()) {
    for (const auto &key_count_text : SplitComma(cfg.key_counts)) {
      const auto key_count = ParseTokenLength(key_count_text);
      if (key_count == 0U) {
        throw std::invalid_argument("key_count must be greater than zero");
      }
      workloads.push_back(KvWorkload{key_count * model.tokens_per_key, key_count, key_size, key_count * key_size});
    }
    return workloads;
  }

  for (const auto &token_text : SplitComma(cfg.token_lengths)) {
    const auto token_length = ParseTokenLength(token_text);
    if (token_length == 0U || token_length % model.tokens_per_key != 0U) {
      throw std::invalid_argument("token_length must be divisible by tokens_per_key");
    }
    const auto key_count = token_length / model.tokens_per_key;
    workloads.push_back(KvWorkload{token_length, key_count, key_size, key_count * key_size});
  }
  return workloads;
}

std::vector<std::uint64_t> ComputeMaxSegmentUsage(const KvBenchConfig &cfg, const ModelSpec &model,
                                                  const std::vector<KvWorkload> &workloads) {
  std::vector<std::uint64_t> max_usage(cfg.num_processes, 0U);
  for (const auto &workload : workloads) {
    const auto keys = BuildKeys(cfg.rank, workload.token_length, workload.key_count, model.IsShared());
    const auto buffers = BuildBuffers(kFakeBufferBase, workload.key_count, workload.key_size);
    KvStore store(cfg.seed, BuildSegmentManager(cfg));
    if (!store.ensure_placements(keys, buffers)) {
      throw std::runtime_error("failed to place KV blocks; check segment_size");
    }
    for (const auto &item : store.Placements()) {
      const auto &placement = item.second;
      if (placement.segment_id >= max_usage.size()) {
        throw std::runtime_error("invalid placement segment id");
      }
      max_usage[placement.segment_id] = std::max(max_usage[placement.segment_id], placement.offset + placement.size);
    }
  }
  return max_usage;
}

std::uint64_t MaxLocalBytes(const std::vector<KvWorkload> &workloads) {
  std::uint64_t value = 0U;
  for (const auto &workload : workloads) {
    value = std::max(value, workload.total_bytes);
  }
  return value;
}

std::filesystem::path SyncDir(const KvBenchConfig &cfg) {
  return std::filesystem::path(cfg.output_dir) / ".kv_sync" / cfg.run_id;
}

std::filesystem::path RankMetaPath(const KvBenchConfig &cfg, std::uint32_t rank) {
  return SyncDir(cfg) / ("rank" + std::to_string(rank) + ".meta");
}

std::string LocalListenEndpoint(const KvBenchConfig &cfg) {
  return cfg.listen_host + ":" + std::to_string(cfg.base_port + cfg.rank);
}

std::string LocalConnectEndpoint(const KvBenchConfig &cfg) {
  return cfg.connect_host + ":" + std::to_string(cfg.base_port + cfg.rank);
}

void WriteTextFileAtomically(const std::filesystem::path &path, const std::string &text) {
  std::filesystem::create_directories(path.parent_path());
  const auto tmp = path.string() + ".tmp";
  {
    std::ofstream out(tmp);
    if (!out.good()) {
      throw std::runtime_error("failed to open " + tmp);
    }
    out << text;
  }
  std::filesystem::rename(tmp, path);
}

void WriteRankMeta(const KvBenchConfig &cfg, const KvRuntime &runtime, std::uint64_t pool_size) {
  std::string text;
  text += "rank=" + std::to_string(cfg.rank) + "\n";
  text += "endpoint=" + LocalConnectEndpoint(cfg) + "\n";
  text += "pool_addr=" + std::to_string(reinterpret_cast<std::uintptr_t>(runtime.pool_buffer)) + "\n";
  text += "pool_size=" + std::to_string(pool_size) + "\n";
  WriteTextFileAtomically(RankMetaPath(cfg, cfg.rank), text);
}

RankMeta ReadRankMeta(const std::filesystem::path &path) {
  std::ifstream in(path);
  if (!in.good()) {
    throw std::runtime_error("failed to open rank meta: " + path.string());
  }
  std::map<std::string, std::string> kv;
  std::string line;
  while (std::getline(in, line)) {
    const auto pos = line.find('=');
    if (pos == std::string::npos) {
      continue;
    }
    kv[line.substr(0, pos)] = line.substr(pos + 1U);
  }
  RankMeta meta;
  meta.rank = static_cast<std::uint32_t>(std::stoul(kv.at("rank")));
  meta.endpoint = kv.at("endpoint");
  meta.pool_addr = static_cast<std::uintptr_t>(std::stoull(kv.at("pool_addr")));
  meta.pool_size = static_cast<std::uint64_t>(std::stoull(kv.at("pool_size")));
  return meta;
}

void WaitForFiles(const std::vector<std::filesystem::path> &paths, std::uint32_t timeout_sec) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout_sec);
  while (std::chrono::steady_clock::now() < deadline) {
    bool all_exist = true;
    for (const auto &path : paths) {
      if (!std::filesystem::exists(path)) {
        all_exist = false;
        break;
      }
    }
    if (all_exist) {
      return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  throw std::runtime_error("timeout waiting for benchmark peers");
}

std::vector<RankMeta> LoadAllRankMeta(const KvBenchConfig &cfg) {
  std::vector<std::filesystem::path> paths;
  for (std::uint32_t rank = 0U; rank < cfg.num_processes; ++rank) {
    paths.push_back(RankMetaPath(cfg, rank));
  }
  WaitForFiles(paths, cfg.sync_timeout_sec);

  std::vector<RankMeta> metas(cfg.num_processes);
  for (std::uint32_t rank = 0U; rank < cfg.num_processes; ++rank) {
    metas[rank] = ReadRankMeta(RankMetaPath(cfg, rank));
    if (metas[rank].rank != rank) {
      throw std::runtime_error("rank meta mismatch");
    }
  }
  return metas;
}

void Barrier(const KvBenchConfig &cfg, const std::string &name) {
  const auto dir = SyncDir(cfg) / name;
  const auto path = dir / ("rank" + std::to_string(cfg.rank));
  WriteTextFileAtomically(path, "ready\n");

  std::vector<std::filesystem::path> paths;
  for (std::uint32_t rank = 0U; rank < cfg.num_processes; ++rank) {
    paths.push_back(dir / ("rank" + std::to_string(rank)));
  }
  WaitForFiles(paths, cfg.sync_timeout_sec);
}

void AllocHostBuffer(const KvBenchConfig &cfg, std::uint64_t size, void **buffer) {
  if (cfg.transport == "fabric_mem") {
    const auto status =
        FabricMemTransferService::MallocMem(MemType::MEM_HOST, static_cast<size_t>(size), buffer);
    if (status != SUCCESS) {
      throw std::runtime_error("fabric_mem host allocation failed");
    }
    return;
  }
  const auto ret = aclrtMallocHost(buffer, static_cast<size_t>(size));
  if (ret != ACL_ERROR_NONE) {
    throw std::runtime_error("aclrtMallocHost failed");
  }
}

void FreeHostBuffer(const KvBenchConfig &cfg, void *buffer) {
  if (buffer == nullptr) {
    return;
  }
  if (cfg.transport == "fabric_mem") {
    (void)FabricMemTransferService::FreeMem(buffer);
  } else {
    (void)aclrtFreeHost(buffer);
  }
}

void RegisterMem(Hixl &hixl, void *buffer, std::uint64_t size, MemType type, MemHandle *handle) {
  MemDesc desc{};
  desc.addr = reinterpret_cast<std::uintptr_t>(buffer);
  desc.len = static_cast<size_t>(size);
  const auto ret = hixl.RegisterMem(desc, type, *handle);
  if (ret != SUCCESS) {
    throw std::runtime_error("RegisterMem failed, ret=" + std::to_string(ret) + ", errmsg: " + RecentErrMsg());
  }
}

void InitRuntime(const KvBenchConfig &cfg, std::uint64_t local_size, std::uint64_t pool_size, KvRuntime *runtime) {
  if (aclrtSetDevice(cfg.device_id) != ACL_ERROR_NONE) {
    throw std::runtime_error("aclrtSetDevice failed");
  }
  runtime->device_bound = true;

  const auto init_options = BuildInitializeOptions(cfg);
  const auto init_ret = runtime->hixl.Initialize(AscendString(LocalListenEndpoint(cfg).c_str()), init_options);
  if (init_ret != SUCCESS) {
    throw std::runtime_error("Hixl Initialize failed, ret=" + std::to_string(init_ret) + ", errmsg: " + RecentErrMsg());
  }
  runtime->hixl_initialized = true;

  if (aclrtMalloc(&runtime->local_buffer, static_cast<size_t>(local_size), ACL_MEM_MALLOC_HUGE_ONLY) !=
      ACL_ERROR_NONE) {
    throw std::runtime_error("aclrtMalloc device buffer failed");
  }
  RegisterMem(runtime->hixl, runtime->local_buffer, local_size, MemType::MEM_DEVICE, &runtime->local_handle);
  runtime->local_registered = true;

  AllocHostBuffer(cfg, pool_size, &runtime->pool_buffer);
  RegisterMem(runtime->hixl, runtime->pool_buffer, pool_size, MemType::MEM_HOST, &runtime->pool_handle);
  runtime->pool_registered = true;
}

void CleanupRuntime(const KvBenchConfig &cfg, KvRuntime *runtime, const std::vector<RankMeta> &metas) {
  if (runtime->hixl_initialized) {
    for (const auto &meta : metas) {
      if (meta.rank == cfg.rank) {
        continue;
      }
      (void)runtime->hixl.Disconnect(AscendString(meta.endpoint.c_str()));
    }
    if (runtime->local_registered) {
      (void)runtime->hixl.DeregisterMem(runtime->local_handle);
      runtime->local_registered = false;
    }
    if (runtime->pool_registered) {
      (void)runtime->hixl.DeregisterMem(runtime->pool_handle);
      runtime->pool_registered = false;
    }
    runtime->hixl.Finalize();
    runtime->hixl_initialized = false;
  }
  if (runtime->local_buffer != nullptr) {
    (void)aclrtFree(runtime->local_buffer);
    runtime->local_buffer = nullptr;
  }
  FreeHostBuffer(cfg, runtime->pool_buffer);
  runtime->pool_buffer = nullptr;
  if (runtime->device_bound) {
    (void)aclrtResetDevice(cfg.device_id);
    runtime->device_bound = false;
  }
}

void ConnectPeers(const KvBenchConfig &cfg, KvRuntime *runtime, const std::vector<RankMeta> &metas) {
  for (const auto &meta : metas) {
    if (meta.rank == cfg.rank) {
      continue;
    }
    const auto ret = runtime->hixl.Connect(AscendString(meta.endpoint.c_str()), kDefaultConnectTimeoutMs);
    if (ret != SUCCESS && ret != hixl::ALREADY_CONNECTED) {
      throw std::runtime_error("Connect failed to " + meta.endpoint + ", ret=" + std::to_string(ret) +
                               ", errmsg: " + RecentErrMsg());
    }
  }
}

std::vector<TransferOpDesc> SliceDescs(const std::vector<TransferOpDesc> &descs, std::size_t begin, std::size_t end) {
  return std::vector<TransferOpDesc>(descs.begin() + static_cast<std::ptrdiff_t>(begin),
                                     descs.begin() + static_cast<std::ptrdiff_t>(end));
}

void TransferRemoteBatches(Hixl &hixl, const RankMeta &meta, TransferOp op, const std::vector<TransferOpDesc> &descs,
                           std::uint32_t batch_size) {
  for (std::size_t begin = 0U; begin < descs.size(); begin += batch_size) {
    const auto end = std::min<std::size_t>(begin + batch_size, descs.size());
    const auto batch = SliceDescs(descs, begin, end);
    const auto ret = hixl.TransferSync(AscendString(meta.endpoint.c_str()), op, batch, kDefaultTransferTimeoutMs);
    if (ret != SUCCESS) {
      throw std::runtime_error("TransferSync failed to " + meta.endpoint + ", ret=" + std::to_string(ret) +
                               ", errmsg: " + RecentErrMsg());
    }
  }
}

void CopySelf(const KvRuntime &runtime, const std::vector<TransferOpDesc> &descs, TransferOp op) {
  for (const auto &desc : descs) {
    void *dst = nullptr;
    const void *src = nullptr;
    aclrtMemcpyKind kind = ACL_MEMCPY_DEVICE_TO_HOST;
    if (op == hixl::WRITE) {
      dst = reinterpret_cast<void *>(desc.remote_addr);
      src = reinterpret_cast<const void *>(desc.local_addr);
      kind = ACL_MEMCPY_DEVICE_TO_HOST;
    } else {
      dst = reinterpret_cast<void *>(desc.local_addr);
      src = reinterpret_cast<const void *>(desc.remote_addr);
      kind = ACL_MEMCPY_HOST_TO_DEVICE;
    }
    const auto ret = aclrtMemcpy(dst, desc.len, src, desc.len, kind);
    if (ret != ACL_ERROR_NONE) {
      throw std::runtime_error("local aclrtMemcpy failed");
    }
  }
  (void)runtime;
}

void ExecuteKvTransfer(const KvBenchConfig &cfg, KvRuntime *runtime, const std::vector<RankMeta> &metas,
                       const KvWorkload &workload, const ModelSpec &model, TransferOp op) {
  const auto keys = BuildKeys(cfg.rank, workload.token_length, workload.key_count, model.IsShared());
  const auto local_base = reinterpret_cast<std::uintptr_t>(runtime->local_buffer);
  const auto buffers = BuildBuffers(local_base, workload.key_count, workload.key_size);
  KvStore store(cfg.seed, BuildSegmentManager(cfg));
  if (!store.ensure_placements(keys, buffers)) {
    throw std::runtime_error("failed to place KV blocks; check segment_size");
  }

  std::map<std::uint32_t, std::vector<TransferOpDesc>> descs_by_rank;
  for (std::size_t i = 0U; i < keys.size(); ++i) {
    const auto placement_it = store.Placements().find(keys[i]);
    if (placement_it == store.Placements().end()) {
      throw std::runtime_error("missing KV placement");
    }
    const auto &placement = placement_it->second;
    const auto &meta = metas.at(placement.segment_id);
    if (placement.offset + placement.size > meta.pool_size) {
      throw std::runtime_error("KV placement exceeds registered remote pool");
    }
    TransferOpDesc desc{};
    desc.local_addr = buffers[i].addr;
    desc.remote_addr = meta.pool_addr + static_cast<std::uintptr_t>(placement.offset);
    desc.len = static_cast<size_t>(placement.size);
    descs_by_rank[placement.segment_id].push_back(desc);
  }

  for (const auto &item : descs_by_rank) {
    if (item.first == cfg.rank) {
      CopySelf(*runtime, item.second, op);
      continue;
    }
    TransferRemoteBatches(runtime->hixl, metas.at(item.first), op, item.second, cfg.batch_size);
  }
}

double Percentile99(std::vector<double> values) {
  if (values.empty()) {
    return 0.0;
  }
  std::sort(values.begin(), values.end());
  const auto idx = static_cast<std::size_t>((values.size() * 99U + 99U) / 100U - 1U);
  return values[std::min(idx, values.size() - 1U)];
}

TimingStats MeasureRepeated(const KvBenchConfig &cfg, const std::string &name, const std::function<void()> &fn,
                            bool sync_all_ranks) {
  std::vector<double> samples;
  const auto total = cfg.warmup + cfg.repeat;
  for (std::uint32_t i = 0U; i < total; ++i) {
    if (sync_all_ranks) {
      Barrier(cfg, name + "_ready_" + std::to_string(i));
    }
    const auto start = std::chrono::steady_clock::now();
    fn();
    const auto end = std::chrono::steady_clock::now();
    if (sync_all_ranks) {
      Barrier(cfg, name + "_done_" + std::to_string(i));
    }
    if (i >= cfg.warmup) {
      samples.push_back(static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count()) /
                        1000.0);
    }
  }
  const double sum = std::accumulate(samples.begin(), samples.end(), 0.0);
  return TimingStats{sum / static_cast<double>(samples.size()), Percentile99(samples)};
}

double BandwidthGbps(std::uint64_t bytes, double us) {
  return us > 0.0 ? static_cast<double>(bytes) / us / 1000.0 : 0.0;
}

KvBenchResult RunWorkload(const KvBenchConfig &cfg, KvRuntime *runtime, const std::vector<RankMeta> &metas,
                          const ModelSpec &model, const KvWorkload &workload, std::size_t index) {
  TimingStats put;
  TimingStats get;

  Barrier(cfg, "workload_" + std::to_string(index) + "_ready");
  if (cfg.op_type == "put" || cfg.op_type == "put_get") {
    if (!model.IsShared() || cfg.rank == 0U) {
      put = MeasureRepeated(
          cfg, "workload_" + std::to_string(index) + "_put",
          [&]() { ExecuteKvTransfer(cfg, runtime, metas, workload, model, hixl::WRITE); }, false);
    }
  }
  Barrier(cfg, "workload_" + std::to_string(index) + "_put_done");

  if (cfg.op_type == "get" || cfg.op_type == "put_get") {
    get = MeasureRepeated(
        cfg, "workload_" + std::to_string(index) + "_get",
        [&]() { ExecuteKvTransfer(cfg, runtime, metas, workload, model, hixl::READ); }, true);
  }
  Barrier(cfg, "workload_" + std::to_string(index) + "_done");

  return KvBenchResult{model.name,
                       workload.token_length,
                       workload.key_count,
                       model.tokens_per_key,
                       workload.key_size,
                       workload.total_bytes,
                       BandwidthGbps(workload.total_bytes, put.avg_us),
                       BandwidthGbps(workload.total_bytes, get.avg_us),
                       put.avg_us,
                       get.avg_us,
                       put.p99_us,
                       get.p99_us};
}

std::vector<KvBenchResult> RunBenchmark(const KvBenchConfig &cfg, KvRuntime *runtime,
                                        const std::vector<RankMeta> &metas, const ModelSpec &model,
                                        const std::vector<KvWorkload> &workloads) {
  std::vector<KvBenchResult> results;
  for (std::size_t i = 0U; i < workloads.size(); ++i) {
    results.push_back(RunWorkload(cfg, runtime, metas, model, workloads[i], i));
  }
  return results;
}

void WriteCsv(const KvBenchConfig &cfg, const std::vector<KvBenchResult> &results) {
  std::filesystem::create_directories(cfg.output_dir);
  std::ofstream out(cfg.output_dir + "/kv_result_rank" + std::to_string(cfg.rank) + ".csv");
  out << "rank,model,token_length,key_count,tokens_per_key,key_size_bytes,total_bytes,batch_size,process_count,"
         "device_count,segment_count,segment_size,pool_memory,put_transfer_type,get_transfer_type,transport,"
         "warmup,repeat,put_bandwidth_gbps,get_bandwidth_gbps,put_avg_us,get_avg_us,put_p99_us,get_p99_us\n";
  for (const auto &result : results) {
    out << cfg.rank << ',' << result.model << ',' << result.token_length << ',' << result.key_count << ','
        << result.tokens_per_key << ',' << result.key_size << ',' << result.total_bytes << ',' << cfg.batch_size << ','
        << cfg.num_processes << ',' << cfg.num_processes << ',' << cfg.num_processes << ',' << cfg.segment_size << ','
        << cfg.pool_memory << ",d2rh,rh2d," << cfg.transport << ',' << cfg.warmup << ',' << cfg.repeat << ','
        << result.put_bandwidth_gbps << ',' << result.get_bandwidth_gbps << ',' << result.put_avg_us << ','
        << result.get_avg_us << ',' << result.put_p99_us << ',' << result.get_p99_us << '\n';
  }
}

void WriteJson(const KvBenchConfig &cfg, const std::vector<KvBenchResult> &results) {
  std::ofstream out(cfg.output_dir + "/kv_result_rank" + std::to_string(cfg.rank) + ".json");
  out << "{\"benchmark_name\":\"hixl_kv_bench\",\"rank\":" << cfg.rank << ",\"results\":[";
  for (std::size_t i = 0U; i < results.size(); ++i) {
    const auto &r = results[i];
    out << (i == 0U ? "" : ",") << "{\"model\":\"" << r.model << "\",\"token_length\":" << r.token_length
        << ",\"key_count\":" << r.key_count << ",\"key_size_bytes\":" << r.key_size
        << ",\"total_bytes\":" << r.total_bytes << ",\"put_avg_us\":" << r.put_avg_us
        << ",\"get_avg_us\":" << r.get_avg_us << ",\"put_p99_us\":" << r.put_p99_us
        << ",\"get_p99_us\":" << r.get_p99_us << ",\"put_transfer_type\":\"d2rh\",\"get_transfer_type\":\"rh2d\"}";
  }
  out << "]}\n";
}

void PrintSummary(const KvBenchConfig &cfg, const std::vector<KvBenchResult> &results) {
  for (const auto &result : results) {
    std::cout << "[INFO] rank=" << cfg.rank << " model=" << result.model << " key_count=" << result.key_count
              << " token_length=" << result.token_length << " key_size=" << result.key_size << " put=d2rh get=rh2d"
              << " put_avg_us=" << result.put_avg_us << " get_avg_us=" << result.get_avg_us
              << " put_p99_us=" << result.put_p99_us << " get_p99_us=" << result.get_p99_us << std::endl;
  }
}

std::string JoinNames(const std::vector<std::string> &names) {
  std::string text;
  for (std::size_t i = 0U; i < names.size(); ++i) {
    if (i != 0U) {
      text += ",";
    }
    text += names[i];
  }
  return text;
}

}  // namespace

int main(int argc, char **argv) {
  KvBenchConfig cfg;
  KvRuntime runtime;
  std::vector<RankMeta> metas;
  try {
    cfg = ParseConfig(argc, argv);
    if (!ValidateConfig(cfg)) {
      std::cerr << "[ERROR] invalid kv benchmark config\n";
      return 1;
    }
    ApplyTransportEnvironment(cfg);

    const auto models = LoadModelSpecsFromJson(cfg.model_config);
    const ModelSpec *model = FindModelSpec(models, cfg.model);
    if (model == nullptr) {
      std::cerr << "[ERROR] unsupported model: " << cfg.model
                << " (supported: " << JoinNames(SupportedModelNames(models)) << ")" << std::endl;
      return 1;
    }
    const auto workloads = BuildWorkloads(cfg, *model);
    const auto max_segment_usage = ComputeMaxSegmentUsage(cfg, *model, workloads);
    const auto local_size = MaxLocalBytes(workloads);
    const auto pool_size = std::max<std::uint64_t>(max_segment_usage.at(cfg.rank), 1U);

    std::cout << "[INFO] rank=" << cfg.rank << " device_id=" << cfg.device_id
              << " local_engine=" << LocalListenEndpoint(cfg) << " connect_endpoint=" << LocalConnectEndpoint(cfg)
              << " local_buffer_size=" << local_size << " pool_size=" << pool_size << std::endl;

    InitRuntime(cfg, local_size, pool_size, &runtime);
    WriteRankMeta(cfg, runtime, pool_size);
    metas = LoadAllRankMeta(cfg);
    ConnectPeers(cfg, &runtime, metas);
    Barrier(cfg, "all_connected");

    const auto results = RunBenchmark(cfg, &runtime, metas, *model, workloads);
    WriteCsv(cfg, results);
    WriteJson(cfg, results);
    PrintSummary(cfg, results);
    Barrier(cfg, "all_done");
    CleanupRuntime(cfg, &runtime, metas);
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "[ERROR] " << e.what() << std::endl;
    CleanupRuntime(cfg, &runtime, metas);
    return 1;
  }
}
