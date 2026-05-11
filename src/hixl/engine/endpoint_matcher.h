/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_ENGINE_ENDPOINT_MATCHER_H_
#define CANN_HIXL_SRC_HIXL_ENGINE_ENDPOINT_MATCHER_H_

#include <map>
#include <string>
#include <vector>
#include "common/hixl_inner_types.h"
#include "engine/client_handler.h"
#include "engine/client_handler_factory.h"

namespace hixl {

class EndpointMatcher {
 public:
  // ---------- Primary matching API ----------
  static Status MatchEndpoints(
      const std::vector<EndpointConfig> &local,
      const std::vector<EndpointConfig> &remote,
      std::vector<HandlerCreateArgs::EndpointPair> &matched_pairs,
      HandlerCreateArgs::HandlerType &handler_type);

  // ---------- Type utility functions ----------
  static CommType ParseCommType(const std::string &local_placement, const std::string &remote_placement);
  static bool IsUbProtocol(const std::string &protocol);
  static bool IsDirectProtocol(const std::string &protocol);
  static const char *HandlerTypeToString(HandlerCreateArgs::HandlerType type);

  // ---------- Endpoint lookup tools ----------
  static const EndpointConfig *FindByProtocol(const std::vector<EndpointConfig> &endpoints,
                                              const std::string &protocol);
  static void BuildMatchMap(const std::vector<EndpointConfig> &endpoints,
                            std::map<MatchKey, EndpointConfig> &out);

 private:
  EndpointMatcher() = delete;

  static bool MustUseRoce(const std::vector<EndpointConfig> &local,
                          const std::vector<EndpointConfig> &remote);

  static std::map<MatchKey, EndpointConfig>::const_iterator FindMatchingKey(
      const std::map<MatchKey, EndpointConfig> &map, const MatchKey &query);

  static Status TryMatchUboe(const std::vector<EndpointConfig> &local,
                             const std::vector<EndpointConfig> &remote,
                             std::vector<HandlerCreateArgs::EndpointPair> &pairs);

  static Status TryMatchRoce(const std::vector<EndpointConfig> &local,
                             const std::vector<EndpointConfig> &remote,
                             std::vector<HandlerCreateArgs::EndpointPair> &pairs);

  static Status TryMatchHccs(const std::vector<EndpointConfig> &local,
                             const std::vector<EndpointConfig> &remote,
                             std::vector<HandlerCreateArgs::EndpointPair> &pairs);

  static Status TryMatchUb(const EndpointConfig &local,
                           const std::map<MatchKey, EndpointConfig> &peers,
                           std::map<CommType, bool> &expected, uint32_t &count,
                           std::vector<HandlerCreateArgs::EndpointPair> &pairs);
};
}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_ENGINE_ENDPOINT_MATCHER_H_
