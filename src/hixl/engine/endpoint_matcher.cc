/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "engine/endpoint_matcher.h"
#include <algorithm>
#include "common/hixl_checker.h"
#include "common/hixl_log.h"
#include "engine/client_handler_factory.h"

namespace hixl {
namespace {
constexpr uint32_t kMaxUbCsClientNum = 4U;
}  // namespace

CommType EndpointMatcher::ParseCommType(const std::string &local, const std::string &remote) {
  if (local == kPlacementDevice && remote == kPlacementDevice) {
    return CommType::COMM_TYPE_UB_D2D;
  }
  if (local == kPlacementDevice && remote == kPlacementHost) {
    return CommType::COMM_TYPE_UB_D2H;
  }
  if (local == kPlacementHost && remote == kPlacementHost) {
    return CommType::COMM_TYPE_UB_H2H;
  }
  return CommType::COMM_TYPE_UB_H2D;
}

bool EndpointMatcher::IsUbProtocol(const std::string &protocol) {
  return protocol == kProtocolUbCtp || protocol == kProtocolUbTp;
}

bool EndpointMatcher::IsDirectProtocol(const std::string &protocol) {
  return protocol == kProtocolRoce || protocol == kProtocolHccs || protocol == kProtocolUboe ||
         protocol == kProtocolUbg;
}

const char *EndpointMatcher::HandlerTypeToString(HandlerCreateArgs::HandlerType type) {
  switch (type) {
    case HandlerCreateArgs::HandlerType::DIRECT:
      return "DIRECT";
    case HandlerCreateArgs::HandlerType::UB:
      return "UB";
    default:
      return "UNKNOWN";
  }
}

const EndpointConfig *EndpointMatcher::FindByProtocol(const std::vector<EndpointConfig> &endpoints,
                                                      const std::string &protocol) {
  auto it =
      std::find_if(endpoints.begin(), endpoints.end(), [&protocol](const auto &e) { return e.protocol == protocol; });
  return it != endpoints.end() ? &(*it) : nullptr;
}

void EndpointMatcher::BuildMatchMap(const std::vector<EndpointConfig> &endpoints,
                                    std::map<MatchKey, EndpointConfig> &out) {
  for (const auto &ep : endpoints) {
    if (IsUbProtocol(ep.protocol)) {
      out[{ep.dst_eid, ep.plane, ep.placement}] = ep;
    }
  }
}

bool EndpointMatcher::IsForceRoceOnly() {
  return IsIntraRoceEnabled();
}

bool EndpointMatcher::IsCrossSuperNode(const std::vector<EndpointConfig> &local,
                                       const std::vector<EndpointConfig> &remote) {
  return local[0].net_instance_id != remote[0].net_instance_id;
}

std::map<MatchKey, EndpointConfig>::const_iterator EndpointMatcher::FindMatchingKey(
    const std::map<MatchKey, EndpointConfig> &map, const MatchKey &query) {
  for (auto it = map.begin(); it != map.end(); ++it) {
    if (it->first.Matches(query)) {
      return it;
    }
  }
  return map.end();
}

Status EndpointMatcher::MatchDirect(const std::vector<EndpointConfig> &local, const std::vector<EndpointConfig> &remote,
                                    const MatchEntry &entry, std::vector<HandlerCreateArgs::EndpointPair> &pairs) {
  auto is_matched = [&entry](const EndpointConfig &endpoint) {
    return endpoint.protocol == entry.protocol && endpoint.placement == entry.placement;
  };
  auto local_it = std::find_if(local.begin(), local.end(), is_matched);
  auto remote_it = std::find_if(remote.begin(), remote.end(), is_matched);
  if (local_it != local.end() && remote_it != remote.end()) {
    pairs.push_back({*local_it, *remote_it, entry.comm_type});
    return SUCCESS;
  }
  return FAILED;
}

Status EndpointMatcher::TryMatchUb(const EndpointConfig &local, const std::map<MatchKey, EndpointConfig> &peers,
                                   std::map<CommType, bool> &expected, uint32_t &count,
                                   std::vector<HandlerCreateArgs::EndpointPair> &pairs) {
  if (!IsUbProtocol(local.protocol)) {
    return SUCCESS;
  }
  for (const auto &placement : {kPlacementDevice, kPlacementHost}) {
    MatchKey key{local.comm_id, local.plane, placement};
    auto it = FindMatchingKey(peers, key);
    if (it != peers.end()) {
      CommType type = ParseCommType(local.placement, it->second.placement);
      if (!expected[type]) {
        pairs.push_back({local, it->second, type});
        expected[type] = true;
        count++;
      }
    }
  }
  return SUCCESS;
}

Status EndpointMatcher::TryMatchUbAll(const std::vector<EndpointConfig> &local,
                                      const std::vector<EndpointConfig> &remote,
                                      std::vector<HandlerCreateArgs::EndpointPair> &pairs) {
  std::map<CommType, bool> expected = {{CommType::COMM_TYPE_UB_D2D, false},
                                       {CommType::COMM_TYPE_UB_H2D, false},
                                       {CommType::COMM_TYPE_UB_D2H, false},
                                       {CommType::COMM_TYPE_UB_H2H, false}};
  uint32_t count = 0;
  std::map<MatchKey, EndpointConfig> peers;
  BuildMatchMap(remote, peers);
  auto sorted_local = local;
  std::sort(sorted_local.begin(), sorted_local.end(),
            [](const EndpointConfig &a, const EndpointConfig &b) { return !a.dst_eid.empty() && b.dst_eid.empty(); });
  for (const auto &ep : sorted_local) {
    HIXL_CHK_STATUS_RET(TryMatchUb(ep, peers, expected, count, pairs));
    if (count == kMaxUbCsClientNum) {
      break;
    }
  }
  return count > 0 ? SUCCESS : FAILED;
}

EndpointMatcher::MatchScenario EndpointMatcher::DetermineScenario(bool cross_supernode) {
  if (IsForceRoceOnly()) {
    return MatchScenario::kForceRoce;
  }
  return cross_supernode ? MatchScenario::kCrossSuperNode : MatchScenario::kSameSuperNode;
}

const std::vector<EndpointMatcher::MatchEntry> &EndpointMatcher::GetPriorityTable(MatchScenario scenario) {
  static const std::map<MatchScenario, std::vector<MatchEntry>> kPriorityTable = {
      {MatchScenario::kForceRoce,
       {{kProtocolRoce, kPlacementDevice, CommType::COMM_TYPE_ROCE},
        {kProtocolRoce, kPlacementHost, CommType::COMM_TYPE_ROCE}}},
      {MatchScenario::kCrossSuperNode,
       {{kProtocolUboe, kPlacementDevice, CommType::COMM_TYPE_UBOE},
        {kProtocolUbg, kPlacementDevice, CommType::COMM_TYPE_UBG},
        {kProtocolRoce, kPlacementDevice, CommType::COMM_TYPE_ROCE},
        {kProtocolRoce, kPlacementHost, CommType::COMM_TYPE_ROCE}}},
      {MatchScenario::kSameSuperNode,
       {{kProtocolUbCtp, kPlacementDevice, CommType::COMM_TYPE_UB_D2D},
        {kProtocolHccs, kPlacementDevice, CommType::COMM_TYPE_HCCS},
        {kProtocolUboe, kPlacementDevice, CommType::COMM_TYPE_UBOE},
        {kProtocolUbg, kPlacementDevice, CommType::COMM_TYPE_UBG},
        {kProtocolRoce, kPlacementDevice, CommType::COMM_TYPE_ROCE},
        {kProtocolRoce, kPlacementHost, CommType::COMM_TYPE_ROCE}}}};
  return kPriorityTable.at(scenario);
}

Status EndpointMatcher::MatchEndpoints(const std::vector<EndpointConfig> &local,
                                       const std::vector<EndpointConfig> &remote,
                                       std::vector<HandlerCreateArgs::EndpointPair> &matched_pairs,
                                       HandlerCreateArgs::HandlerType &handler_type) {
  matched_pairs.clear();
  HIXL_CHK_BOOL_RET_STATUS(!local.empty() && !remote.empty(), PARAM_INVALID,
                           "EndpointMatcher::MatchEndpoints got empty endpoint list");

  const auto &priority = GetPriorityTable(DetermineScenario(IsCrossSuperNode(local, remote)));
  for (const auto &entry : priority) {
    if (IsUbProtocol(entry.protocol)) {
      if (TryMatchUbAll(local, remote, matched_pairs) == SUCCESS) {
        handler_type = HandlerCreateArgs::HandlerType::UB;
        LogMatchedEndpoints(matched_pairs, handler_type);
        return SUCCESS;
      }
      continue;
    }
    if (MatchDirect(local, remote, entry, matched_pairs) == SUCCESS) {
      handler_type = HandlerCreateArgs::HandlerType::DIRECT;
      LogMatchedEndpoints(matched_pairs, handler_type);
      return SUCCESS;
    }
  }
  HIXL_LOGE(PARAM_INVALID, "Failed to find matched endpoints");
  return PARAM_INVALID;
}

void EndpointMatcher::LogMatchedEndpoints(const std::vector<HandlerCreateArgs::EndpointPair> &pairs,
                                          HandlerCreateArgs::HandlerType handler_type) {
  HIXL_EVENT("EndpointMatcher selected handler:%s, matched pairs:%zu", HandlerTypeToString(handler_type), pairs.size());
  for (size_t i = 0; i < pairs.size(); ++i) {
    const auto &pair = pairs[i];
    HIXL_EVENT("EndpointMatcher pair[%zu], protocol:%s, placement:%s, comm_type:%s", i, pair.local.protocol.c_str(),
               pair.local.placement.c_str(), CommTypeToString(pair.type));
  }
}

}  // namespace hixl
