/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef HIXL_KV_BENCHMARK_MODEL_CONFIG_H
#define HIXL_KV_BENCHMARK_MODEL_CONFIG_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace hixl_kv_benchmark {

constexpr std::uint32_t kDefaultTokensPerKey = 128U;

struct CacheSpec {
  std::string name;
  std::size_t bytes_per_key = 0U;
};

struct LayerSpec {
  std::vector<CacheSpec> caches;
};

struct ModelSpec {
  std::string name;
  std::vector<LayerSpec> layers;
  std::uint32_t tokens_per_key = kDefaultTokensPerKey;
  std::string kv_strategy = "independent";

  std::size_t BytesPerKey() const;
  bool IsShared() const { return kv_strategy == "shared"; }
};

std::vector<ModelSpec> LoadModelSpecsFromJson(const std::string &config_path);
const ModelSpec *FindModelSpec(const std::vector<ModelSpec> &models, const std::string &name);
std::vector<std::string> SupportedModelNames(const std::vector<ModelSpec> &models);
std::uint64_t ParseTokenLength(const std::string &token_length);

}  // namespace hixl_kv_benchmark

#endif  // HIXL_KV_BENCHMARK_MODEL_CONFIG_H
