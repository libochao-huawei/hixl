/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hixl_utils.h"
#include <securec.h>
#include <arpa/inet.h>
#include "nlohmann/json.hpp"
#include "hixl_log.h"
#include "hixl_checker.h"

namespace hixl {
Status HcclError2Status(HcclResult ret) {
  static const std::map<HcclResult, Status> result2status = {
      {HCCL_SUCCESS, SUCCESS},
      {HCCL_E_PARA, PARAM_INVALID},
      {HCCL_E_TIMEOUT, TIMEOUT},
      {HCCL_E_NOT_SUPPORT, UNSUPPORTED},
  };
  const auto &it = result2status.find(ret);
  if (it != result2status.cend()) {
    return it->second;
  }
  return FAILED;
}

Status ParseIpAddress(const std::string &ip_str, CommAddr &addr) {
  struct in_addr ipv4_addr;
  (void)memset_s(&ipv4_addr, sizeof(ipv4_addr), 0, sizeof(ipv4_addr));
  if (inet_pton(AF_INET, ip_str.c_str(), &ipv4_addr) == 1) {
    addr.type = COMM_ADDR_TYPE_IP_V4;
    addr.addr = ipv4_addr;
    return SUCCESS;
  }

  struct in6_addr ipv6_addr;
  (void)memset_s(&ipv6_addr, sizeof(ipv6_addr), 0, sizeof(ipv6_addr));
  if (inet_pton(AF_INET6, ip_str.c_str(), &ipv6_addr) == 1) {
    addr.type = COMM_ADDR_TYPE_IP_V6;
    addr.addr6 = ipv6_addr;
    return SUCCESS;
  }

  HIXL_LOGE(PARAM_INVALID, "Invalid IP address: %s", ip_str.c_str());
  return PARAM_INVALID;
}

Status ConvertToEndPointInfo(const EndPointConfig &endpoint_config, EndPointInfo &endpoint) {
  static const std::map<std::string, EndPointLocation> placement_map = {{"host", END_POINT_LOCATION_HOST},
                                                                        {"device", END_POINT_LOCATION_DEVICE}};

  static const std::map<std::string, CommProtocol> protocol_map = {{"hccs", COMM_PROTOCOL_HCCS},
                                                                   {"tcp", COMM_PROTOCOL_TCP},
                                                                   {"roce", COMM_PROTOCOL_ROCE},
                                                                   {"ub_ctp", COMM_PROTOCOL_UB_CTP},
                                                                   {"ub_tp", COMM_PROTOCOL_UB_TP}};

  // 处理placement
  auto placement_it = placement_map.find(endpoint_config.placement);
  if (placement_it == placement_map.end()) {
    HIXL_LOGE(PARAM_INVALID, "Unsupported placement: %s", endpoint_config.placement.c_str());
    return PARAM_INVALID;
  }
  endpoint.location = placement_it->second;

  // 处理protocol
  auto protocol_it = protocol_map.find(endpoint_config.protocol);
  if (protocol_it == protocol_map.end()) {
    HIXL_LOGE(PARAM_INVALID, "Unsupported protocol: %s", endpoint_config.protocol.c_str());
    return PARAM_INVALID;
  }
  endpoint.protocol = protocol_it->second;

  // 处理ROCE协议的comm_id
  if (endpoint_config.protocol == "roce") {
    HIXL_CHK_STATUS_RET(ParseIpAddress(endpoint_config.comm_id, endpoint.addr), "ParseIpAddress failed");
    return SUCCESS;
  }
  HIXL_LOGW("Unsupported protocol: %s", endpoint_config.protocol.c_str());
  return SUCCESS;
}

Status SerializeEndPointConfigList(const std::vector<EndPointConfig> &list, std::string &msg_str) {
  nlohmann::json j = nlohmann::json::array();
  try {
    for (const auto &ep : list) {
      nlohmann::json item;
      item["protocol"] = ep.protocol;
      item["comm_id"] = ep.comm_id;
      item["placement"] = ep.placement;
      item["plane"] = ep.plane;
      item["dst_eid"] = ep.dst_eid;
      item["net_instance_id"] = ep.net_instance_id;
      j.push_back(item);
    }
    msg_str = j.dump();
  } catch (const nlohmann::json::exception &e) {
    HIXL_LOGE(PARAM_INVALID,
              "Failed to dump endpoint list, exception:%s",
              e.what());
    return PARAM_INVALID;
  }
  return SUCCESS;
}

}  // namespace hixl
