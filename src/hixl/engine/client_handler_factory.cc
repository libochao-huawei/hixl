/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "engine/client_handler_factory.h"
#include <algorithm>
#include <cstdlib>
#include "common/hixl_checker.h"
#include "common/hixl_log.h"
#include "common/hixl_utils.h"
#include "engine/direct_client_handler.h"
#include "engine/endpoint_generator.h"
#include "engine/ub_client_handler.h"

namespace hixl {
namespace {

constexpr uint32_t kMaxUbCsClientNum = 4U;

const char *CommTypeToString(CommType type) {
  switch (type) {
    case CommType::COMM_TYPE_UB_D2D:
      return "UB_D2D";
    case CommType::COMM_TYPE_UB_H2D:
      return "UB_H2D";
    case CommType::COMM_TYPE_UB_D2H:
      return "UB_D2H";
    case CommType::COMM_TYPE_UB_H2H:
      return "UB_H2H";
    case CommType::COMM_TYPE_ROCE:
      return "ROCE";
    case CommType::COMM_TYPE_HCCS:
      return "HCCS";
    case CommType::COMM_TYPE_UBOE:
      return "UBOE";
    default:
      return "UNKNOWN";
  }
}

bool IsUbCommType(CommType type) {
  return type == CommType::COMM_TYPE_UB_D2D || type == CommType::COMM_TYPE_UB_H2D ||
         type == CommType::COMM_TYPE_UB_D2H || type == CommType::COMM_TYPE_UB_H2H;
}

std::unique_ptr<IClientHandler> CreateClientHandler(std::map<CommType, HixlClientHandle> &handles) {
  if (handles.size() == 1) {
    auto type = handles.begin()->first;
    if (!IsUbCommType(type)) {
      auto handler = MakeUnique<DirectClientHandler>();
      handler->GetHandles() = std::move(handles);
      return handler;
    }
  }
  auto handler = MakeUnique<UbClientHandler>();
  handler->GetHandles() = std::move(handles);
  return handler;
}

std::map<MatchKey, EndpointConfig>::const_iterator FindMatchingKey(
    const std::map<MatchKey, EndpointConfig> &map, const MatchKey &query_key) {
  for (auto it = map.begin(); it != map.end(); ++it) {
    if (it->first.Matches(query_key)) {
      return it;
    }
  }
  return map.end();
}

CommType ParseCommType(const std::string &local_placement, const std::string &remote_placement) {
  if (local_placement == kPlacementDevice && remote_placement == kPlacementDevice) {
    return CommType::COMM_TYPE_UB_D2D;
  } else if (local_placement == kPlacementDevice && remote_placement == kPlacementHost) {
    return CommType::COMM_TYPE_UB_D2H;
  } else if (local_placement == kPlacementHost && remote_placement == kPlacementHost) {
    return CommType::COMM_TYPE_UB_H2H;
  } else {
    return CommType::COMM_TYPE_UB_H2D;
  }
}

bool MustUseRoce(const std::vector<EndpointConfig> &local_endpoint_list,
                 const std::vector<EndpointConfig> &remote_endpoint_list) {
  std::string env_roce_enable;
  const char *env_ret = std::getenv("HCCL_INTRA_ROCE_ENABLE");
  if (env_ret != nullptr) {
    env_roce_enable = env_ret;
  }
  const bool is_env_roce_enabled = (env_roce_enable == "1");
  const bool is_net_instance_different =
      local_endpoint_list[0].net_instance_id != remote_endpoint_list[0].net_instance_id;
  return is_env_roce_enabled || is_net_instance_different;
}

Status CreateCsClient(const EndpointConfig &local_endpoint_config, const EndpointConfig &remote_endpoint_config,
                      CommType type, std::map<CommType, HixlClientHandle> &handles,
                      const std::string &server_ip, uint32_t server_port, uint8_t rdma_tc, uint8_t rdma_sl) {
  int32_t dev_logic_id = 0;
  int32_t dev_phy_id = 0;
  HIXL_CHK_ACL_RET(aclrtGetDevice(&dev_logic_id));
  HIXL_CHK_ACL_RET(aclrtGetPhyDevIdByLogicDevId(dev_logic_id, &dev_phy_id));
  EndpointDesc local_endpoint{};
  EndpointDesc remote_endpoint{};
  HIXL_CHK_STATUS_RET(EndpointGenerator::ConvertToEndpointDesc(local_endpoint_config, local_endpoint,
                                                               static_cast<uint32_t>(dev_phy_id)),
                      "Convert EndpointConfig to EndpointInfo failed, local_endpoint_config:%s",
                      local_endpoint_config.ToString().c_str());
  HIXL_LOGI("Local_endpoint dev_phy_id: %u", local_endpoint.loc.device.devPhyId);
  HIXL_CHK_STATUS_RET(EndpointGenerator::ConvertToEndpointDesc(remote_endpoint_config, remote_endpoint),
                      "Convert EndpointConfig to EndpointInfo failed, remote_endpoint_config:%s",
                      remote_endpoint_config.ToString().c_str());
  HIXL_LOGI("Remote_endpoint dev_phy_id: %u", remote_endpoint.loc.device.devPhyId);
  HixlClientHandle handle = nullptr;
  HixlClientDesc desc{};
  desc.server_ip = server_ip.c_str();
  desc.server_port = server_port;
  desc.local_endpoint = &local_endpoint;
  desc.remote_endpoint = &remote_endpoint;
  desc.tc = rdma_tc;
  desc.sl = rdma_sl;
  const HixlClientConfig config{};
  HIXL_CHK_STATUS_RET(HixlCSClientCreate(&desc, &config, &handle), "HixlCSClientCreate failed for type %s",
                      CommTypeToString(type));
  HIXL_LOGI("HixlCSClientCreate success for type %s, handle:%p", CommTypeToString(type), handle);
  handles[type] = handle;
  return SUCCESS;
}

Status TryMatchUboeEndpoints(const std::vector<EndpointConfig> &local_endpoint_list,
                              const std::vector<EndpointConfig> &remote_endpoint_list,
                              std::map<CommType, HixlClientHandle> &handles,
                              const std::string &server_ip, uint32_t server_port, uint8_t rdma_tc, uint8_t rdma_sl) {
  auto local_it = std::find_if(local_endpoint_list.begin(), local_endpoint_list.end(),
                                [](const EndpointConfig &endpoint) { return endpoint.protocol == kProtocolUboe; });
  auto remote_it = std::find_if(remote_endpoint_list.begin(), remote_endpoint_list.end(),
                                 [](const EndpointConfig &endpoint) { return endpoint.protocol == kProtocolUboe; });
  if (local_it != local_endpoint_list.end() && remote_it != remote_endpoint_list.end()) {
    return CreateCsClient(*local_it, *remote_it, CommType::COMM_TYPE_UBOE, handles,
                          server_ip, server_port, rdma_tc, rdma_sl);
  }
  return FAILED;
}

Status TryMatchRoceEndpoints(const std::vector<EndpointConfig> &local_endpoint_list,
                              const std::vector<EndpointConfig> &remote_endpoint_list,
                              std::map<CommType, HixlClientHandle> &handles,
                              const std::string &server_ip, uint32_t server_port, uint8_t rdma_tc, uint8_t rdma_sl) {
  auto local_it = std::find_if(local_endpoint_list.begin(), local_endpoint_list.end(),
                               [](const EndpointConfig &endpoint) { return endpoint.protocol == kProtocolRoce; });
  auto remote_it = std::find_if(remote_endpoint_list.begin(), remote_endpoint_list.end(),
                                [](const EndpointConfig &endpoint) { return endpoint.protocol == kProtocolRoce; });
  if (local_it != local_endpoint_list.end() && remote_it != remote_endpoint_list.end()) {
    return CreateCsClient(*local_it, *remote_it, CommType::COMM_TYPE_ROCE, handles,
                          server_ip, server_port, rdma_tc, rdma_sl);
  }
  HIXL_LOGE(FAILED, "Failed to find matched ROCE endpoints");
  return FAILED;
}

Status TryMatchHccsEndpoints(const std::vector<EndpointConfig> &local_endpoint_list,
                              const std::vector<EndpointConfig> &remote_endpoint_list,
                              std::map<CommType, HixlClientHandle> &handles,
                              const std::string &server_ip, uint32_t server_port, uint8_t rdma_tc, uint8_t rdma_sl) {
  auto local_it = std::find_if(local_endpoint_list.begin(), local_endpoint_list.end(),
                               [](const EndpointConfig &endpoint) { return endpoint.protocol == kProtocolHccs; });
  auto remote_it = std::find_if(remote_endpoint_list.begin(), remote_endpoint_list.end(),
                                [](const EndpointConfig &endpoint) { return endpoint.protocol == kProtocolHccs; });
  if (local_it != local_endpoint_list.end() && remote_it != remote_endpoint_list.end()) {
    return CreateCsClient(*local_it, *remote_it, CommType::COMM_TYPE_HCCS, handles,
                          server_ip, server_port, rdma_tc, rdma_sl);
  }
  HIXL_LOGE(FAILED, "Failed to find matched HCCS endpoints");
  return FAILED;
}

void BuildEndpointsMatchMap(const std::vector<EndpointConfig> &endpoint_list,
                            std::map<MatchKey, EndpointConfig> &peer_match_endpoints) {
  for (const auto &endpoint : endpoint_list) {
    if (endpoint.protocol == kProtocolUbCtp || endpoint.protocol == kProtocolUbTp) {
      MatchKey key = {endpoint.dst_eid, endpoint.plane, endpoint.placement};
      peer_match_endpoints[key] = endpoint;
    }
  }
}

Status TryMatchUbEndpoints(const EndpointConfig &local_endpoint,
                           const std::map<MatchKey, EndpointConfig> &peer_match_endpoints,
                           std::map<CommType, bool> &expected_pairs, uint32_t &count,
                           std::map<CommType, HixlClientHandle> &handles,
                           const std::string &server_ip, uint32_t server_port, uint8_t rdma_tc, uint8_t rdma_sl) {
  if (local_endpoint.protocol != kProtocolUbCtp && local_endpoint.protocol != kProtocolUbTp) {
    return SUCCESS;
  }
  for (const auto &placement : {kPlacementDevice, kPlacementHost}) {
    MatchKey key = {local_endpoint.comm_id, local_endpoint.plane, placement};
    HIXL_LOGI("TryMatchUbEndpoints: key:%s", key.ToString().c_str());
    auto it = FindMatchingKey(peer_match_endpoints, key);
    if (it != peer_match_endpoints.end()) {
      HIXL_LOGI("Found matched endpoint, remote_endpoint:%s", it->second.ToString().c_str());
      CommType type = ParseCommType(local_endpoint.placement, it->second.placement);
      if (!expected_pairs[type]) {
        HIXL_CHK_STATUS_RET(CreateCsClient(local_endpoint, it->second, type, handles,
                                            server_ip, server_port, rdma_tc, rdma_sl),
                            "CreateCsClient failed for type %s", CommTypeToString(type));
        expected_pairs[type] = true;
        count++;
        HIXL_LOGI("CreateCsClient success for type %s", CommTypeToString(type));
      }
    }
  }
  return SUCCESS;
}

Status FindMatchedEndpoints(const std::vector<EndpointConfig> &local_endpoint_list,
                            const std::vector<EndpointConfig> &remote_endpoint_list,
                            std::map<CommType, HixlClientHandle> &handles,
                            const std::string &server_ip, uint32_t server_port, uint8_t rdma_tc, uint8_t rdma_sl) {
  if (TryMatchUboeEndpoints(local_endpoint_list, remote_endpoint_list, handles,
                             server_ip, server_port, rdma_tc, rdma_sl) == SUCCESS) {
    return SUCCESS;
  }
  if (MustUseRoce(local_endpoint_list, remote_endpoint_list)) {
    return TryMatchRoceEndpoints(local_endpoint_list, remote_endpoint_list, handles,
                                  server_ip, server_port, rdma_tc, rdma_sl);
  }
  std::map<CommType, bool> expected_pairs = {{CommType::COMM_TYPE_UB_D2D, false},
                                             {CommType::COMM_TYPE_UB_H2D, false},
                                             {CommType::COMM_TYPE_UB_D2H, false},
                                             {CommType::COMM_TYPE_UB_H2H, false}};
  uint32_t count = 0;
  std::map<MatchKey, EndpointConfig> peer_match_endpoints;
  BuildEndpointsMatchMap(remote_endpoint_list, peer_match_endpoints);
  for (const auto &local_endpoint : local_endpoint_list) {
    HIXL_LOGI("local_endpoint:%s", local_endpoint.ToString().c_str());
    HIXL_CHK_STATUS_RET(TryMatchUbEndpoints(local_endpoint, peer_match_endpoints, expected_pairs, count,
                                             handles, server_ip, server_port, rdma_tc, rdma_sl),
                        "TryMatchUbEndpoints failed");
    if (count == kMaxUbCsClientNum) {
      HIXL_LOGI("Created all %u expected UB CS clients", count);
      return SUCCESS;
    }
  }
  if (count > 0) {
    HIXL_LOGW("Found only %u/%u expected UB endpoint pairs", count, kMaxUbCsClientNum);
    return SUCCESS;
  }

  HIXL_LOGI("No matched UB endpoints found, try HCCS matching");
  if (TryMatchHccsEndpoints(local_endpoint_list, remote_endpoint_list, handles,
                             server_ip, server_port, rdma_tc, rdma_sl) == SUCCESS) {
    return SUCCESS;
  }

  HIXL_LOGE(FAILED, "Failed to find matched UB/HCCS endpoints");
  return FAILED;
}

}  // namespace

std::unique_ptr<IClientHandler> ClientHandlerFactory::Create(
    const std::string &server_ip, uint32_t server_port, uint8_t rdma_tc, uint8_t rdma_sl,
    const std::vector<EndpointConfig> &local_endpoints,
    const std::vector<EndpointConfig> &remote_endpoints) {
  std::map<CommType, HixlClientHandle> temp_handles;
  Status ret = FindMatchedEndpoints(local_endpoints, remote_endpoints, temp_handles,
                                     server_ip, server_port, rdma_tc, rdma_sl);
  if (ret != SUCCESS) {
    return nullptr;
  }
  return CreateClientHandler(temp_handles);
}

}  // namespace hixl
