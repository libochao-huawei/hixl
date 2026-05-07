/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <chrono>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

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
  std::string token_lengths = "16K,32K,64K,128K";
  std::uint32_t batch_size = kDefaultBatchSize;
  std::string op_type = "put_get";
  std::uint64_t seed = kDefaultSeed;
  std::string transport = "fabric_mem";
  std::string output_dir = "kv_benchmark/output";
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
  double put_latency_us = 0.0;
  double get_latency_us = 0.0;
};

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
    current.push_back(ch);
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
  if (args.count("--segment_size") != 0U) cfg.segment_size = ParseSize(args.at("--segment_size"));
  if (args.count("--pool_memory") != 0U) cfg.pool_memory = args.at("--pool_memory");
  if (args.count("--model") != 0U) cfg.model = args.at("--model");
  if (args.count("--model_config") != 0U) cfg.model_config = args.at("--model_config");
  if (args.count("--token_lengths") != 0U) cfg.token_lengths = args.at("--token_lengths");
  if (args.count("--op_type") != 0U) cfg.op_type = args.at("--op_type");
  if (args.count("--transport") != 0U) cfg.transport = args.at("--transport");
  if (args.count("--output_dir") != 0U) cfg.output_dir = args.at("--output_dir");
  return cfg;
}

bool ValidateConfig(const KvBenchConfig &cfg) {
  return cfg.num_processes > 0U && cfg.rank < cfg.num_processes && cfg.batch_size > 0U && cfg.pool_memory == "host" &&
         (cfg.op_type == "put" || cfg.op_type == "get" || cfg.op_type == "put_get");
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

std::vector<BufferView> BuildBuffers(std::uint64_t key_count, std::uint64_t key_size) {
  std::vector<BufferView> buffers;
  buffers.reserve(static_cast<std::size_t>(key_count));
  for (std::uint64_t i = 0U; i < key_count; ++i) {
    buffers.push_back(BufferView{kFakeBufferBase + i * key_size, key_size});
  }
  return buffers;
}

double MeasureUs(const std::function<bool()> &fn, bool *ok) {
  const auto start = std::chrono::steady_clock::now();
  *ok = fn();
  const auto end = std::chrono::steady_clock::now();
  return static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count()) / 1000.0;
}

KvBenchResult RunTokenLength(const KvBenchConfig &cfg, const ModelSpec &model, std::uint64_t token_length) {
  const auto key_count = token_length / model.tokens_per_key;
  const auto key_size = static_cast<std::uint64_t>(model.BytesPerKey());
  const bool shared = model.IsShared();
  const auto keys = BuildKeys(cfg.rank, token_length, key_count, shared);
  const auto buffers = BuildBuffers(key_count, key_size);
  KvStore store(cfg.seed, BuildSegmentManager(cfg));
  bool put_ok = true;
  bool get_ok = true;
  double put_us = 0.0;
  double get_us = 0.0;

  if (cfg.op_type == "put" || cfg.op_type == "put_get") {
    if (!shared || cfg.rank == 0U) {
      put_us = MeasureUs([&]() { return store.batch_put_from_multi_buffers(keys, buffers); }, &put_ok);
    } else {
      // Shared strategy, non-rank-0: populate placements for get without put requests
      store.ensure_placements(keys, buffers);
    }
  }
  if (cfg.op_type == "get" || cfg.op_type == "put_get") {
    get_us = MeasureUs([&]() { return store.batch_get_into_multi_buffers(keys, buffers); }, &get_ok);
  }
  if (!put_ok || !get_ok) {
    throw std::runtime_error("failed to generate kvstore transfer requests");
  }
  const auto total_bytes = key_count * key_size;
  return KvBenchResult{model.name, token_length, key_count, model.tokens_per_key, key_size, total_bytes,
                       put_us > 0.0 ? static_cast<double>(total_bytes) / put_us / 1000.0 : 0.0,
                       static_cast<double>(total_bytes) / get_us / 1000.0, put_us, get_us};
}

std::vector<KvBenchResult> RunBenchmark(const KvBenchConfig &cfg, const ModelSpec &model) {
  std::vector<KvBenchResult> results;
  for (const auto &token_text : SplitComma(cfg.token_lengths)) {
    const auto token_length = ParseTokenLength(token_text);
    if (token_length == 0U || token_length % model.tokens_per_key != 0U) {
      throw std::invalid_argument("token_length must be divisible by tokens_per_key");
    }
    results.push_back(RunTokenLength(cfg, model, token_length));
  }
  return results;
}

void WriteCsv(const KvBenchConfig &cfg, const std::vector<KvBenchResult> &results) {
  std::filesystem::create_directories(cfg.output_dir);
  std::ofstream out(cfg.output_dir + "/kv_result_rank" + std::to_string(cfg.rank) + ".csv");
  out << "model,token_length,key_count,tokens_per_key,key_size_bytes,total_bytes,batch_size,process_count,"
         "device_count,segment_count,segment_size,pool_memory,put_transfer_type,get_transfer_type,transport,"
         "put_bandwidth_gbps,get_bandwidth_gbps,put_p99_us,get_p99_us\n";
  for (const auto &result : results) {
    out << result.model << ',' << result.token_length << ',' << result.key_count << ',' << result.tokens_per_key << ','
        << result.key_size << ',' << result.total_bytes << ',' << cfg.batch_size << ',' << cfg.num_processes << ','
        << cfg.num_processes << ',' << cfg.num_processes << ',' << cfg.segment_size << ',' << cfg.pool_memory
        << ",d2rh,rh2d," << cfg.transport << ',' << result.put_bandwidth_gbps << ',' << result.get_bandwidth_gbps
        << ',' << result.put_latency_us << ',' << result.get_latency_us << '\n';
  }
}

void WriteJson(const KvBenchConfig &cfg, const std::vector<KvBenchResult> &results) {
  std::ofstream out(cfg.output_dir + "/kv_result_rank" + std::to_string(cfg.rank) + ".json");
  out << "{\"benchmark_name\":\"hixl_kv_bench\",\"rank\":" << cfg.rank << ",\"results\":[";
  for (std::size_t i = 0U; i < results.size(); ++i) {
    const auto &r = results[i];
    out << (i == 0U ? "" : ",") << "{\"model\":\"" << r.model << "\",\"token_length\":" << r.token_length
        << ",\"key_count\":" << r.key_count << ",\"key_size_bytes\":" << r.key_size
        << ",\"total_bytes\":" << r.total_bytes << ",\"put_transfer_type\":\"d2rh\",\"get_transfer_type\":\"rh2d\"}";
  }
  out << "]}\n";
}

void PrintSummary(const KvBenchConfig &cfg, const std::vector<KvBenchResult> &results) {
  for (const auto &result : results) {
    std::cout << "[INFO] rank=" << cfg.rank << " model=" << result.model << " token_length=" << result.token_length
              << " key_count=" << result.key_count << " key_size=" << result.key_size << " put=d2rh get=rh2d"
              << " request_gen_put_us=" << result.put_latency_us << " request_gen_get_us=" << result.get_latency_us
              << std::endl;
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
  try {
    const KvBenchConfig cfg = ParseConfig(argc, argv);
    if (!ValidateConfig(cfg)) {
      std::cerr << "[ERROR] invalid kv benchmark config\n";
      return 1;
    }
    const auto models = LoadModelSpecsFromJson(cfg.model_config);
    const ModelSpec *model = FindModelSpec(models, cfg.model);
    if (model == nullptr) {
      std::cerr << "[ERROR] unsupported model: " << cfg.model << " (supported: " << JoinNames(SupportedModelNames(models))
                << ")" << std::endl;
      return 1;
    }
    const auto results = RunBenchmark(cfg, *model);
    WriteCsv(cfg, results);
    WriteJson(cfg, results);
    PrintSummary(cfg, results);
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "[ERROR] " << e.what() << std::endl;
    return 1;
  }
}
