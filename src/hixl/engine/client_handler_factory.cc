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

bool IsUbCommType(CommType type) {
  return type == CommType::COMM_TYPE_UB_D2D || type == CommType::COMM_TYPE_UB_H2D ||
         type == CommType::COMM_TYPE_UB_D2H || type == CommType::COMM_TYPE_UB_H2H;
}

std::unique_ptr<IClientHandler> CreateClientHandler(std::map<CommType, HixlClientHandle> &handles) {
  if (handles.size() == 1 && !IsUbCommType(handles.begin()->first)) {
    return MakeUnique<DirectClientHandler>(std::move(handles));
  }
  return MakeUnique<UbClientHandler>(std::move(handles));
}

auto FindMatchingKey(const std::map<MatchKey, EndpointConfig> &map, const MatchKey &query) {
  for (auto it = map.begin(); it != map.end(); ++it) {
    if (it->first.Matches(query)) return it;
  }
  return map.end();
}

CommType ParseCommType(const std::string &local, const std::string &remote) {
  if (local == kPlacementDevice && remote == kPlacementDevice) return CommType::COMM_TYPE_UB_D2D;
  if (local == kPlacementDevice && remote == kPlacementHost)   return CommType::COMM_TYPE_UB_D2H;
  if (local == kPlacementHost   && remote == kPlacementHost)   return CommType::COMM_TYPE_UB_H2H;
  return CommType::COMM_TYPE_UB_H2D;
}

bool MustUseRoce(const std::vector<EndpointConfig> &local, const std::vector<EndpointConfig> &remote) {
  const char *env = std::getenv("HCCL_INTRA_ROCE_ENABLE");
  if (env != nullptr && std::string(env) == "1") return true;
  return local[0].net_instance_id != remote[0].net_instance_id;
}

void BuildEndpointsMatchMap(const std::vector<EndpointConfig> &eps,
                            std::map<MatchKey, EndpointConfig> &out) {
  for (const auto &ep : eps) {
    if (ep.protocol == kProtocolUbCtp || ep.protocol == kProtocolUbTp) {
      out[{ep.dst_eid, ep.plane, ep.placement}] = ep;
    }
  }
}

Status CreateCsClient(const EndpointConfig &local, const EndpointConfig &remote, CommType type,
                      std::map<CommType, HixlClientHandle> &handles,
                      const std::string &ip, uint32_t port, uint8_t tc, uint8_t sl) {
  int32_t dev_logic_id = 0, dev_phy_id = 0;
  HIXL_CHK_ACL_RET(aclrtGetDevice(&dev_logic_id));
  HIXL_CHK_ACL_RET(aclrtGetPhyDevIdByLogicDevId(dev_logic_id, &dev_phy_id));
  EndpointDesc le{}, re{};
  HIXL_CHK_STATUS_RET(EndpointGenerator::ConvertToEndpointDesc(local, le, static_cast<uint32_t>(dev_phy_id)));
  HIXL_CHK_STATUS_RET(EndpointGenerator::ConvertToEndpointDesc(remote, re));

  HixlClientDesc desc{};
  desc.server_ip = ip.c_str();
  desc.server_port = port;
  desc.local_endpoint = &le;
  desc.remote_endpoint = &re;
  desc.tc = tc;
  desc.sl = sl;
  HixlClientHandle handle = nullptr;
  const HixlClientConfig config{};
  HIXL_CHK_STATUS_RET(HixlCSClientCreate(&desc, &config, &handle),
                      "HixlCSClientCreate failed for type %s", CommTypeToString(type));
  handles[type] = handle;
  return SUCCESS;
}

Status TryMatchUboe(const std::vector<EndpointConfig> &local, const std::vector<EndpointConfig> &remote,
                    std::map<CommType, HixlClientHandle> &handles,
                    const std::string &ip, uint32_t port, uint8_t tc, uint8_t sl) {
  auto li = std::find_if(local.begin(), local.end(), [](auto &e) { return e.protocol == kProtocolUboe; });
  auto ri = std::find_if(remote.begin(), remote.end(), [](auto &e) { return e.protocol == kProtocolUboe; });
  if (li != local.end() && ri != remote.end()) {
    return CreateCsClient(*li, *ri, CommType::COMM_TYPE_UBOE, handles, ip, port, tc, sl);
  }
  return FAILED;
}

Status TryMatchRoce(const std::vector<EndpointConfig> &local, const std::vector<EndpointConfig> &remote,
                    std::map<CommType, HixlClientHandle> &handles,
                    const std::string &ip, uint32_t port, uint8_t tc, uint8_t sl) {
  auto li = std::find_if(local.begin(), local.end(), [](auto &e) { return e.protocol == kProtocolRoce; });
  auto ri = std::find_if(remote.begin(), remote.end(), [](auto &e) { return e.protocol == kProtocolRoce; });
  if (li != local.end() && ri != remote.end()) {
    return CreateCsClient(*li, *ri, CommType::COMM_TYPE_ROCE, handles, ip, port, tc, sl);
  }
  HIXL_LOGE(FAILED, "Failed to find matched ROCE endpoints");
  return FAILED;
}

Status TryMatchHccs(const std::vector<EndpointConfig> &local, const std::vector<EndpointConfig> &remote,
                    std::map<CommType, HixlClientHandle> &handles,
                    const std::string &ip, uint32_t port, uint8_t tc, uint8_t sl) {
  auto li = std::find_if(local.begin(), local.end(), [](auto &e) { return e.protocol == kProtocolHccs; });
  auto ri = std::find_if(remote.begin(), remote.end(), [](auto &e) { return e.protocol == kProtocolHccs; });
  if (li != local.end() && ri != remote.end()) {
    return CreateCsClient(*li, *ri, CommType::COMM_TYPE_HCCS, handles, ip, port, tc, sl);
  }
  HIXL_LOGE(FAILED, "Failed to find matched HCCS endpoints");
  return FAILED;
}

Status TryMatchUb(const EndpointConfig &local, const std::map<MatchKey, EndpointConfig> &peers,
                  std::map<CommType, bool> &expected, uint32_t &count,
                  std::map<CommType, HixlClientHandle> &handles,
                  const std::string &ip, uint32_t port, uint8_t tc, uint8_t sl) {
  if (local.protocol != kProtocolUbCtp && local.protocol != kProtocolUbTp) return SUCCESS;
  for (const auto &placement : {kPlacementDevice, kPlacementHost}) {
    MatchKey key{local.comm_id, local.plane, placement};
    auto it = FindMatchingKey(peers, key);
    if (it != peers.end()) {
      CommType type = ParseCommType(local.placement, it->second.placement);
      if (!expected[type]) {
        HIXL_CHK_STATUS_RET(CreateCsClient(local, it->second, type, handles, ip, port, tc, sl));
        expected[type] = true;
        count++;
      }
    }
  }
  return SUCCESS;
}

Status FindMatchedEndpoints(const HandlerCreateArgs &args,
                            std::map<CommType, HixlClientHandle> &handles) {
  if (TryMatchUboe(args.local_endpoints, args.remote_endpoints, handles,
                    args.server_ip, args.server_port, args.rdma_tc, args.rdma_sl) == SUCCESS) {
    return SUCCESS;
  }
  if (MustUseRoce(args.local_endpoints, args.remote_endpoints)) {
    return TryMatchRoce(args.local_endpoints, args.remote_endpoints, handles,
                         args.server_ip, args.server_port, args.rdma_tc, args.rdma_sl);
  }
  std::map<CommType, bool> expected = {{CommType::COMM_TYPE_UB_D2D, false},
                                       {CommType::COMM_TYPE_UB_H2D, false},
                                       {CommType::COMM_TYPE_UB_D2H, false},
                                       {CommType::COMM_TYPE_UB_H2H, false}};
  uint32_t count = 0;
  std::map<MatchKey, EndpointConfig> peers;
  BuildEndpointsMatchMap(args.remote_endpoints, peers);
  for (const auto &ep : args.local_endpoints) {
    HIXL_CHK_STATUS_RET(TryMatchUb(ep, peers, expected, count, handles,
                                    args.server_ip, args.server_port, args.rdma_tc, args.rdma_sl));
    if (count == kMaxUbCsClientNum) return SUCCESS;
  }
  if (count > 0) return SUCCESS;

  if (TryMatchHccs(args.local_endpoints, args.remote_endpoints, handles,
                    args.server_ip, args.server_port, args.rdma_tc, args.rdma_sl) == SUCCESS) {
    return SUCCESS;
  }
  HIXL_LOGE(FAILED, "Failed to find matched endpoints");
  return FAILED;
}

}  // namespace

std::unique_ptr<IClientHandler> ClientHandlerFactory::Create(const HandlerCreateArgs &args) {
  std::map<CommType, HixlClientHandle> temp_handles;
  Status ret = FindMatchedEndpoints(args, temp_handles);
  if (ret != SUCCESS) return nullptr;
  return CreateClientHandler(temp_handles);
}

}  // namespace hixl
