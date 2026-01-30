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
#include <arpa/inet.h>
#include "securec.h"
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

Status ParseEidAddress(const std::string &eid_str, CommAddr &addr) {
  // 检查字符串长度是否为32
  if (eid_str.length() != 32) {
    HIXL_LOGE(PARAM_INVALID, "Invalid EID format: %s. Expected 32 hexadecimal characters without colons.",
              eid_str.c_str());
    return PARAM_INVALID;
  }

  // 检查字符串是否只包含十六进制字符
  if (!std::all_of(eid_str.begin(), eid_str.end(), [](unsigned char c) { return std::isxdigit(c); })) {
    HIXL_LOGE(PARAM_INVALID, "Invalid EID: %s. Only hexadecimal characters are allowed.", eid_str.c_str());
    return PARAM_INVALID;
  }

  (void)memset_s(addr.eid, COMM_ADDR_EID_LEN, 0, COMM_ADDR_EID_LEN);
  // 每两个字符转换为一个uint8_t值
  for (size_t i = 0; i < COMM_ADDR_EID_LEN; ++i) {
    std::string segment = eid_str.substr(i * 2, 2);
    try {
      unsigned long value = std::stoul(segment, nullptr, 16);
      if (value > UINT8_MAX) {
        HIXL_LOGE(PARAM_INVALID, "Invalid segment %zu in EID: %s. Maximum value is 0xFF.", i, segment.c_str());
        return PARAM_INVALID;
      }
      addr.eid[i] = static_cast<uint8_t>(value);
    } catch (const std::invalid_argument &) {
      HIXL_LOGE(PARAM_INVALID, "Failed to convert segment %zu of EID: %s to integer.", i, segment.c_str());
      return PARAM_INVALID;
    } catch (const std::out_of_range &) {
      HIXL_LOGE(PARAM_INVALID, "Segment %zu of EID: %s is out of range.", i, segment.c_str());
      return PARAM_INVALID;
    }
  }
  addr.type = COMM_ADDR_TYPE_EID;
  return SUCCESS;
}

Status ConvertToEndPointInfo(const EndPointConfig &endpoint_config, EndpointDesc &endpoint, uint32_t devPhyId) {
  static const std::map<std::string, EndpointLocType> placement_map = {{"host", ENDPOINT_LOC_TYPE_HOST},
                                                                       {"device", ENDPOINT_LOC_TYPE_DEVICE}};

  static const std::map<std::string, CommProtocol> protocol_map = {{"hccs", COMM_PROTOCOL_HCCS},
                                                                   {"roce", COMM_PROTOCOL_ROCE},
                                                                   {"ub_ctp", COMM_PROTOCOL_UBC_CTP},
                                                                   {"ub_tp", COMM_PROTOCOL_UBC_TP}};

  // 处理placement
  auto placement_it = placement_map.find(endpoint_config.placement);
  if (placement_it == placement_map.end()) {
    HIXL_LOGE(PARAM_INVALID, "Unsupported placement: %s", endpoint_config.placement.c_str());
    return PARAM_INVALID;
  }
  endpoint.loc.locType = placement_it->second;

  // 处理protocol
  auto protocol_it = protocol_map.find(endpoint_config.protocol);
  if (protocol_it == protocol_map.end()) {
    HIXL_LOGE(PARAM_INVALID, "Unsupported protocol: %s", endpoint_config.protocol.c_str());
    return PARAM_INVALID;
  }
  endpoint.protocol = protocol_it->second;

  // 处理ROCE协议的comm_id
  if (endpoint_config.protocol == "roce") {
    HIXL_CHK_STATUS_RET(ParseIpAddress(endpoint_config.comm_id, endpoint.commAddr), "ParseIpAddress failed");
  }

  // 处理UB协议的comm_id
  if (endpoint_config.protocol == "ub_ctp" || endpoint_config.protocol == "ub_tp") {
    HIXL_CHK_STATUS_RET(ParseEidAddress(endpoint_config.comm_id, endpoint.commAddr), "ParseEidAddress failed");
    // placement 为device则需要填写device结构体中的物理id
    if (endpoint.loc.locType == ENDPOINT_LOC_TYPE_DEVICE) {
      endpoint.loc.device.devPhyId = devPhyId;
    }
  }
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
    HIXL_LOGE(PARAM_INVALID, "Failed to dump endpoint list, exception:%s", e.what());
    return PARAM_INVALID;
  }
  return SUCCESS;
}

Status CheckIp(const std::string &ip) {
  struct in_addr addr;
  struct sockaddr_in6 ipv6_addr;
  HIXL_CHK_BOOL_RET_STATUS(
      inet_pton(AF_INET, ip.c_str(), &addr) == 1 || inet_pton(AF_INET6, ip.c_str(), &ipv6_addr.sin6_addr) == 1,
      hixl::PARAM_INVALID, "%s is not a valid ip address", ip.c_str());
  return hixl::SUCCESS;
}

std::vector<std::string, std::allocator<std::string>> Split(const std::string &str, const char delim) {
  std::vector<std::string, std::allocator<std::string>> elems;
  if (str.empty()) {
    (void)elems.emplace_back("");
    return elems;
  }

  std::stringstream ss(str);
  std::string item;
  while (getline(ss, item, delim)) {
    (void)elems.push_back(item);
  }

  const auto str_size = str.size();
  if ((str_size > 0U) && (str[str_size - 1U] == delim)) {
    (void)elems.emplace_back("");
  }
  return elems;
}

Status ParseListenInfo(const std::string &listen_info, std::string &listen_ip, int32_t &listen_port) {
  std::vector<std::string> listen_infos;
  size_t left = listen_info.find('[');
  size_t right = listen_info.find(']');
  if (left != std::string::npos && right != std::string::npos && left < right) {
    listen_infos.emplace_back(listen_info.substr(left + 1, right - left - 1));
    size_t colon = listen_info.find(':', right);
    if (colon != std::string::npos) {
      listen_infos.emplace_back(listen_info.substr(colon + 1));
    }
  } else {
    listen_infos = Split(listen_info, ':');
  }
  HIXL_CHK_BOOL_RET_STATUS(listen_infos.size() >= 1U, PARAM_INVALID,
                           "listen info is invalid: %s, expect ${ip}:${port} or ${ip}", listen_info.c_str());
  listen_ip = listen_infos[0];
  HIXL_CHK_STATUS_RET(CheckIp(listen_ip), "IP is invalid: %s, listen info = %s", listen_ip.c_str(),
                      listen_info.c_str());
  if (listen_infos.size() > 1U) {
    HIXL_CHK_STATUS_RET(ToNumber(listen_infos[1], listen_port), "Port:%s is invalid.", listen_infos[1].c_str());
  }
  return SUCCESS;
}
}  // namespace hixl
