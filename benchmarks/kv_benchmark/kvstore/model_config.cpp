/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "model_config.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <stdexcept>

#include "nlohmann/json.hpp"

namespace hixl_kv_benchmark {
namespace {

constexpr std::size_t kKiB = 1024U;
constexpr std::size_t kMiB = kKiB * 1024U;
constexpr std::size_t kGiB = kMiB * 1024U;

std::string RemoveSpaces(std::string value) {
  value.erase(std::remove_if(value.begin(), value.end(), [](unsigned char ch) {
                return std::isspace(ch) != 0;
              }),
              value.end());
  return value;
}

std::string ToUpper(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
    return static_cast<char>(std::toupper(ch));
  });
  return value;
}

std::size_t ParseByteSize(const nlohmann::json &value) {
  if (value.is_number_unsigned()) {
    return value.get<std::size_t>();
  }
  if (!value.is_string()) {
    throw std::invalid_argument("bytes_per_key must be an unsigned integer or size string");
  }

  std::string text = RemoveSpaces(value.get<std::string>());
  if (text.empty()) {
    throw std::invalid_argument("empty bytes_per_key");
  }
  std::size_t multiplier = 1U;
  const std::string upper = ToUpper(text);
  if (upper.size() > 3U && upper.compare(upper.size() - 3U, 3U, "KIB") == 0) {
    multiplier = kKiB;
    text.resize(text.size() - 3U);
  } else if (upper.size() > 3U && upper.compare(upper.size() - 3U, 3U, "MIB") == 0) {
    multiplier = kMiB;
    text.resize(text.size() - 3U);
  } else if (upper.size() > 3U && upper.compare(upper.size() - 3U, 3U, "GIB") == 0) {
    multiplier = kGiB;
    text.resize(text.size() - 3U);
  } else if (!upper.empty() && upper.back() == 'K') {
    multiplier = kKiB;
    text.pop_back();
  } else if (!upper.empty() && upper.back() == 'M') {
    multiplier = kMiB;
    text.pop_back();
  } else if (!upper.empty() && upper.back() == 'G') {
    multiplier = kGiB;
    text.pop_back();
  }

  std::size_t pos = 0U;
  const auto parsed = std::stoull(text, &pos, 10);
  if (pos != text.size()) {
    throw std::invalid_argument("invalid bytes_per_key: " + value.get<std::string>());
  }
  return static_cast<std::size_t>(parsed) * multiplier;
}

CacheSpec ParseCacheSpec(const nlohmann::json &cache) {
  CacheSpec spec;
  spec.name = cache.at("name").get<std::string>();
  spec.bytes_per_key = ParseByteSize(cache.at("bytes_per_key"));
  if (spec.name.empty() || spec.bytes_per_key == 0U) {
    throw std::invalid_argument("cache name and bytes_per_key must be non-empty");
  }
  return spec;
}

LayerSpec ParseLayerTemplate(const nlohmann::json &caches) {
  if (!caches.is_array() || caches.empty()) {
    throw std::invalid_argument("caches_per_layer must be a non-empty array");
  }
  LayerSpec layer;
  for (const auto &cache : caches) {
    layer.caches.push_back(ParseCacheSpec(cache));
  }
  return layer;
}

ModelSpec ParseRepeatedLayerModel(const nlohmann::json &item) {
  const auto layer_count = item.at("layer_count").get<std::uint32_t>();
  if (layer_count == 0U) {
    throw std::invalid_argument("layer_count must be greater than zero");
  }
  const LayerSpec layer_template = ParseLayerTemplate(item.at("caches_per_layer"));
  ModelSpec model;
  model.name = item.at("name").get<std::string>();
  model.tokens_per_key = item.value("tokens_per_key", kDefaultTokensPerKey);
  model.kv_strategy = item.value("kv_strategy", "independent");
  model.layers.assign(layer_count, layer_template);
  return model;
}

ModelSpec ParseExplicitLayerModel(const nlohmann::json &item) {
  ModelSpec model;
  model.name = item.at("name").get<std::string>();
  model.tokens_per_key = item.value("tokens_per_key", kDefaultTokensPerKey);
  model.kv_strategy = item.value("kv_strategy", "independent");
  const auto &layers = item.at("layers");
  if (!layers.is_array() || layers.empty()) {
    throw std::invalid_argument("layers must be a non-empty array");
  }
  for (const auto &layer : layers) {
    model.layers.push_back(ParseLayerTemplate(layer.at("caches")));
  }
  return model;
}

ModelSpec ParseModelSpec(const nlohmann::json &item) {
  ModelSpec model = item.contains("layers") ? ParseExplicitLayerModel(item) : ParseRepeatedLayerModel(item);
  if (model.name.empty() || model.tokens_per_key == 0U || model.layers.empty() || model.BytesPerKey() == 0U) {
    throw std::invalid_argument("invalid model spec");
  }
  return model;
}

}  // namespace

std::size_t ModelSpec::BytesPerKey() const {
  std::size_t total = 0U;
  for (const auto &layer : layers) {
    for (const auto &cache : layer.caches) {
      total += cache.bytes_per_key;
    }
  }
  return total;
}

std::vector<ModelSpec> LoadModelSpecsFromJson(const std::string &config_path) {
  std::ifstream in(config_path);
  if (!in.good()) {
    throw std::runtime_error("failed to open model config: " + config_path);
  }
  nlohmann::json root;
  in >> root;
  const auto &models_json = root.at("models");
  if (!models_json.is_array() || models_json.empty()) {
    throw std::invalid_argument("model config must contain a non-empty models array");
  }
  std::vector<ModelSpec> models;
  for (const auto &item : models_json) {
    models.push_back(ParseModelSpec(item));
  }
  return models;
}

const ModelSpec *FindModelSpec(const std::vector<ModelSpec> &models, const std::string &name) {
  const auto it = std::find_if(models.begin(), models.end(), [&name](const ModelSpec &spec) {
    return spec.name == name;
  });
  return it == models.end() ? nullptr : &(*it);
}

std::vector<std::string> SupportedModelNames(const std::vector<ModelSpec> &models) {
  std::vector<std::string> names;
  for (const auto &model : models) {
    names.push_back(model.name);
  }
  return names;
}

std::uint64_t ParseTokenLength(const std::string &token_length) {
  if (token_length.empty()) {
    throw std::invalid_argument("empty token length");
  }
  std::string digits = token_length;
  std::uint64_t multiplier = 1U;
  const char suffix = static_cast<char>(std::toupper(static_cast<unsigned char>(digits.back())));
  if (suffix == 'K' || suffix == 'M') {
    digits.pop_back();
    multiplier = suffix == 'K' ? 1024ULL : static_cast<std::uint64_t>(kMiB);
  }
  std::size_t pos = 0U;
  const auto value = std::stoull(digits, &pos, 10);
  if (pos != digits.size()) {
    throw std::invalid_argument("invalid token length");
  }
  return value * multiplier;
}

}  // namespace hixl_kv_benchmark
