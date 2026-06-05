/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "global_config.h"

#include "nlohmann/json.hpp"
#include "common/hixl_log.h"

namespace hixl {
namespace {
constexpr const char *kListenPort = "comm_resource_config.listen_port";
constexpr int64_t kMinListenPort = 1;
constexpr int64_t kMaxListenPort = 65535;

void ParseListenPort(const nlohmann::json &json, CommResourceConfig &config) {
  const auto it = json.find(kListenPort);
  if (it == json.end()) {
    return;
  }

  const auto val = it->get<int64_t>();
  if (val < kMinListenPort || val > kMaxListenPort) {
    HIXL_LOGW("[GlobalConfig] listen_port out of range: %ld, must be in [%ld, %ld], ignored", val, kMinListenPort,
              kMaxListenPort);
    return;
  }

  config.listen_port = static_cast<uint32_t>(val);
  HIXL_LOGI("[GlobalConfig] listen_port=%u", *config.listen_port);
}
}  // namespace

void from_json(const nlohmann::json &json, CommResourceConfig &config) {
  ParseListenPort(json, config);
}

void from_json(const nlohmann::json &json, GlobalConfig &config) {
  json.get_to(config.comm_resource_config_);
}

Status GlobalConfig::Parse(const char *config_str, GlobalConfig &result) {
  if (config_str == nullptr || config_str[0] == '\0') {
    return SUCCESS;
  }

  try {
    auto json = nlohmann::json::parse(config_str);
    if (!json.is_object()) {
      HIXL_LOGE(PARAM_INVALID, "[GlobalConfig] config must be a JSON object");
      return PARAM_INVALID;
    }

    json.get_to(result);
    return SUCCESS;
  } catch (const nlohmann::json::exception &e) {
    HIXL_LOGE(PARAM_INVALID, "[GlobalConfig] Failed to parse config: %s", e.what());
    return PARAM_INVALID;
  }
}

std::optional<uint32_t> GlobalConfig::ListenPort() const {
  return comm_resource_config_.listen_port;
}

}  // namespace hixl
