/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "endpoint_generator.h"

#include <cstdlib>
#include <set>
#include <string>

#include "nlohmann/json.hpp"
#include "acl/acl.h"
#include "common/hixl_checker.h"
#include "common/hixl_log.h"
#include "common/hixl_utils.h"

namespace hixl {
namespace {
constexpr const char kConfigVersion[] = "1.3";

const std::set<std::string> kSocV2 = {"Ascend910B1", "Ascend910B2", "Ascend910B3",
                                      "Ascend910B4", "Ascend910B2C", "Ascend910B4-1"};
const std::set<std::string> kSocV3 = {"Ascend910_9391", "Ascend910_9381", "Ascend910_9392",
                                      "Ascend910_9382", "Ascend910_9372", "Ascend910_9362"};

enum class LocalCommResMode { kManualParse, kAutoGenerate };

struct LocalCommResContext {
  SocType soc_type = SocType::kOther;
  LocalCommResMode mode = LocalCommResMode::kManualParse;
};

bool IsIntraRoceEnabled() {
  const char *env_ret = std::getenv("HCCL_INTRA_ROCE_ENABLE");
  HIXL_CHK_BOOL_RET_SPECIAL_STATUS(env_ret == nullptr, false, "HCCL_INTRA_ROCE_ENABLE is not set");
  return std::string(env_ret) == "1";
}

bool IsVersionOnlyLocalCommRes(const nlohmann::json &j) {
  const bool has_version = j.contains("version") && j["version"].is_string();
  const bool has_net_instance_id = j.contains("net_instance_id");
  const bool has_endpoint_list =
      j.contains("endpoint_list") && j["endpoint_list"].is_array() && !j["endpoint_list"].empty();
  return has_version && j.size() == 1 && !has_net_instance_id && !has_endpoint_list;
}

Status ResolveLocalCommResContext(const std::string &local_comm_res, LocalCommResContext &context) {
  nlohmann::json j;
  try {
    j = nlohmann::json::parse(local_comm_res);
  } catch (const nlohmann::json::exception &e) {
    HIXL_LOGE(PARAM_INVALID, "Parse local_comm_res failed, exception:%s", e.what());
    return PARAM_INVALID;
  }

  if (!j.contains("version") || !j["version"].is_string()) {
    HIXL_LOGE(PARAM_INVALID, "local_comm_res missing version");
    return PARAM_INVALID;
  }

  const auto version = j["version"].get<std::string>();
  HIXL_CHK_BOOL_RET_STATUS(version == kConfigVersion, PARAM_INVALID,
                           "HixlEngine only supports local_comm_res version %s, current version:%s",
                           kConfigVersion, version.c_str());
  HIXL_CHK_STATUS_RET(EndpointGenerator::GetSocType(context.soc_type), "GetSocType failed");
  if ((context.soc_type == SocType::kV2 || context.soc_type == SocType::kV3) && IsVersionOnlyLocalCommRes(j)) {
    context.mode = LocalCommResMode::kAutoGenerate;
  }
  return SUCCESS;
}

void ConvertLocCommResInfoToEndpointList(const loc_comm_res::LocCommResInfo &loc_comm_res_info,
                                         std::vector<EndpointConfig> &endpoint_list) {
  endpoint_list.clear();
  for (const auto &ep_info : loc_comm_res_info.endpoint_list) {
    EndpointConfig ep;
    ep.protocol = ep_info.protocol;
    ep.comm_id = ep_info.comm_id;
    ep.placement = ep_info.placement;
    ep.net_instance_id = loc_comm_res_info.net_instance_id;
    endpoint_list.emplace_back(ep);
  }
}

Status ParseRequiredJsonField(const nlohmann::json &json_obj, const std::string &field_name, std::string &field_value) {
  if (!json_obj.contains(field_name)) {
    HIXL_LOGE(PARAM_INVALID, "Missing required field '%s' in EndpointConfig", field_name.c_str());
    return PARAM_INVALID;
  }

  try {
    field_value = json_obj[field_name].get<std::string>();
    return SUCCESS;
  } catch (const nlohmann::json::exception &e) {
    HIXL_LOGE(PARAM_INVALID, "Failed to parse field '%s', exception: %s", field_name.c_str(), e.what());
    return PARAM_INVALID;
  }
}

Status ParseOptionalJsonField(const nlohmann::json &json_obj, const std::string &field_name, std::string &field_value) {
  if (!json_obj.contains(field_name)) {
    return SUCCESS;
  }

  try {
    field_value = json_obj[field_name].get<std::string>();
    return SUCCESS;
  } catch (const nlohmann::json::exception &e) {
    HIXL_LOGE(PARAM_INVALID, "Failed to parse field '%s', exception: %s", field_name.c_str(), e.what());
    return PARAM_INVALID;
  }
}

void ParseDeviceInfo(const nlohmann::json &item, EndpointConfig &endpoint) {
  if (!item.contains("device_info") || !item["device_info"].is_object()) {
    return;
  }

  const auto &device_info = item["device_info"];
  if (device_info.contains("phy_device_id") && device_info["phy_device_id"].is_number_integer()) {
    endpoint.device_info.phy_device_id = device_info["phy_device_id"].get<int32_t>();
  }
  if (device_info.contains("super_device_id") && device_info["super_device_id"].is_number_integer()) {
    endpoint.device_info.super_device_id = device_info["super_device_id"].get<int64_t>();
  }
  if (device_info.contains("super_pod_id") && device_info["super_pod_id"].is_number_integer()) {
    endpoint.device_info.super_pod_id = device_info["super_pod_id"].get<int64_t>();
  }
}
}  // namespace

Status EndpointGenerator::BuildEndpointListFromLocalCommRes(const std::string &local_comm_res,
                                                            const std::string &local_engine,
                                                            std::vector<EndpointConfig> &endpoint_list) {
  LocalCommResContext context{};
  HIXL_CHK_STATUS_RET(ResolveLocalCommResContext(local_comm_res, context), "ResolveLocalCommResContext failed");

  endpoint_list.clear();
  if (context.mode == LocalCommResMode::kAutoGenerate) {
    int32_t device_id = 0;
    HIXL_CHK_ACL_RET(aclrtGetDevice(&device_id));
    loc_comm_res::LocCommResInfo loc_comm_res_info{};
    HIXL_CHK_STATUS_RET(GenerateInfo(device_id, local_engine, loc_comm_res_info), "GenerateInfo failed");
    ConvertLocCommResInfoToEndpointList(loc_comm_res_info, endpoint_list);
  } else {
    HIXL_CHK_STATUS_RET(ParseLocalCommRes(local_comm_res, endpoint_list), "ParseLocalCommRes failed");
  }

  HIXL_CHK_STATUS_RET(FillDeviceInfoIfNeeded(context.soc_type, endpoint_list), "FillDeviceInfoIfNeeded failed");
  return SUCCESS;
}

Status EndpointGenerator::SerializeEndpointConfigList(const std::vector<EndpointConfig> &list, std::string &msg_str) {
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

      nlohmann::json device_info;
      device_info["phy_device_id"] = ep.device_info.phy_device_id;
      device_info["super_device_id"] = ep.device_info.super_device_id;
      device_info["super_pod_id"] = ep.device_info.super_pod_id;
      item["device_info"] = device_info;

      j.push_back(item);
    }
    msg_str = j.dump();
  } catch (const nlohmann::json::exception &e) {
    HIXL_LOGE(PARAM_INVALID, "Failed to dump endpoint list, exception:%s", e.what());
    return PARAM_INVALID;
  }
  return SUCCESS;
}

Status EndpointGenerator::DeserializeEndpointConfigList(const std::string &json_str,
                                                        std::vector<EndpointConfig> &endpoint_list) {
  nlohmann::json j;
  try {
    j = nlohmann::json::parse(json_str);
  } catch (const nlohmann::json::exception &e) {
    HIXL_LOGE(PARAM_INVALID, "Failed to parse json, exception:%s", e.what());
    return PARAM_INVALID;
  }
  if (!j.is_array()) {
    HIXL_LOGE(PARAM_INVALID, "Invalid json format, expect array");
    return PARAM_INVALID;
  }

  endpoint_list.clear();
  for (const auto &item : j) {
    EndpointConfig endpoint{};
    HIXL_CHK_STATUS_RET(ParseRequiredJsonField(item, "protocol", endpoint.protocol), "Failed to parse protocol");
    HIXL_CHK_STATUS_RET(ParseRequiredJsonField(item, "comm_id", endpoint.comm_id), "Failed to parse comm_id");
    HIXL_CHK_STATUS_RET(ParseRequiredJsonField(item, "net_instance_id", endpoint.net_instance_id),
                        "Failed to parse net_instance_id");
    HIXL_CHK_STATUS_RET(ParseRequiredJsonField(item, "placement", endpoint.placement), "Failed to parse placement");
    HIXL_CHK_STATUS_RET(ParseRequiredJsonField(item, "plane", endpoint.plane), "Failed to parse plane");
    HIXL_CHK_STATUS_RET(ParseRequiredJsonField(item, "dst_eid", endpoint.dst_eid), "Failed to parse dst_eid");
    ParseDeviceInfo(item, endpoint);
    endpoint_list.emplace_back(std::move(endpoint));
  }
  return SUCCESS;
}

Status EndpointGenerator::GenerateInfo(int32_t device_id, const std::string &local_engine,
                                       loc_comm_res::LocCommResInfo &loc_comm_res_info) {
  int32_t phy_device_id = 0;
  HIXL_CHK_ACL_RET(aclrtGetPhyDevIdByLogicDevId(device_id, &phy_device_id),
                   "device_id:%d, failed to get physical device id", device_id);

  loc_comm_res_info = {};
  loc_comm_res_info.version = kConfigVersion;

  HIXL_CHK_STATUS_RET(BuildNetInstanceId(device_id, local_engine, loc_comm_res_info.net_instance_id),
                      "BuildNetInstanceId failed, device_id:%d, local_engine:%s", device_id, local_engine.c_str());
  HIXL_CHK_STATUS_RET(BuildEndpointList(phy_device_id, loc_comm_res_info.endpoint_list),
                      "BuildEndpointList failed, phy_device_id:%d", phy_device_id);

  return SUCCESS;
}

Status EndpointGenerator::GetSocName(std::string &soc_name) {
  const char *soc_name_cstr = aclrtGetSocName();
  HIXL_CHK_BOOL_RET_STATUS(soc_name_cstr != nullptr, FAILED, "aclrtGetSocName returned nullptr");
  soc_name = soc_name_cstr;
  HIXL_CHK_BOOL_RET_STATUS(!soc_name.empty(), FAILED, "soc_name is empty");
  return SUCCESS;
}

SocType EndpointGenerator::GetSocTypeByName(const std::string &soc_name) {
  if (kSocV2.find(soc_name) != kSocV2.end()) {
    return SocType::kV2;
  }

  if (kSocV3.find(soc_name) != kSocV3.end()) {
    return SocType::kV3;
  }

  return SocType::kOther;
}

Status EndpointGenerator::GetSocType(SocType &soc_type) {
  std::string soc_name;
  HIXL_CHK_STATUS_RET(GetSocName(soc_name), "GetSocName failed");
  soc_type = GetSocTypeByName(soc_name);
  return SUCCESS;
}

Status EndpointGenerator::BuildNetInstanceId(int32_t device_id, const std::string &local_engine,
                                             std::string &net_instance_id) {
  SocType soc_type = SocType::kOther;
  HIXL_CHK_STATUS_RET(GetSocType(soc_type), "GetSocType failed");

  if (soc_type == SocType::kV3) {
    int64_t super_pod_id = 0;
    HIXL_CHK_ACL_RET(aclrtGetDeviceInfo(static_cast<uint32_t>(device_id), ACL_DEV_ATTR_SUPER_POD_ID, &super_pod_id),
                     "device_id:%d, failed to get super pod id", device_id);
    net_instance_id = std::to_string(super_pod_id);
    return SUCCESS;
  }

  HIXL_CHK_BOOL_RET_STATUS(soc_type == SocType::kV2, PARAM_INVALID,
                           "Unsupported soc_type:%d for auto-generated net_instance_id",
                           static_cast<int32_t>(soc_type));

  return GetHostIpFromLocalEngine(local_engine, net_instance_id);
}

Status EndpointGenerator::ParseLocalCommRes(const std::string &local_comm_res,
                                            std::vector<EndpointConfig> &endpoint_list) {
  try {
    auto config = nlohmann::json::parse(local_comm_res);
    HIXL_CHK_BOOL_RET_STATUS(config.contains("net_instance_id") && config["net_instance_id"].is_string(), PARAM_INVALID,
                             "local_comm_res missing net_instance_id");
    HIXL_CHK_BOOL_RET_STATUS(config.contains("endpoint_list") && config["endpoint_list"].is_array(), PARAM_INVALID,
                             "local_comm_res missing endpoint_list");

    const std::string net_instance_id = config["net_instance_id"].get<std::string>();
    endpoint_list.clear();
    for (const auto &item : config["endpoint_list"]) {
      EndpointConfig endpoint{};
      HIXL_CHK_STATUS_RET(ParseRequiredJsonField(item, "protocol", endpoint.protocol), "Failed to parse protocol");
      HIXL_CHK_STATUS_RET(ParseRequiredJsonField(item, "comm_id", endpoint.comm_id), "Failed to parse comm_id");
      HIXL_CHK_STATUS_RET(ParseRequiredJsonField(item, "placement", endpoint.placement), "Failed to parse placement");
      HIXL_CHK_STATUS_RET(ParseOptionalJsonField(item, "plane", endpoint.plane), "Failed to parse plane");
      HIXL_CHK_STATUS_RET(ParseOptionalJsonField(item, "dst_eid", endpoint.dst_eid), "Failed to parse dst_eid");
      endpoint.net_instance_id = net_instance_id;
      ParseDeviceInfo(item, endpoint);
      endpoint_list.emplace_back(std::move(endpoint));
    }
    HIXL_CHK_BOOL_RET_STATUS(!endpoint_list.empty(), PARAM_INVALID,
                             "[HixlEngine] endpoint_list is empty, please check options, local_comm_res:%s",
                             local_comm_res.c_str());
  } catch (const nlohmann::json::exception &e) {
    HIXL_LOGE(PARAM_INVALID,
              "[HixlEngine] Failed to parse local_comm_res, exception:%s, local_comm_res:%s",
              e.what(), local_comm_res.c_str());
    return PARAM_INVALID;
  }
  return SUCCESS;
}

Status EndpointGenerator::FillDeviceInfoIfNeeded(SocType soc_type, std::vector<EndpointConfig> &endpoint_list) {
  if (soc_type == SocType::kOther) {
    return SUCCESS;
  }

  int32_t device_id = 0;
  HIXL_CHK_ACL_RET(aclrtGetDevice(&device_id));
  int32_t phy_device_id = -1;
  aclError acl_ret = aclrtGetPhyDevIdByLogicDevId(device_id, &phy_device_id);
  if (acl_ret != ACL_SUCCESS) {
    HIXL_LOGE(FAILED, "aclrtGetPhyDevIdByLogicDevId failed, device_id=%d, acl_ret=%d", device_id, acl_ret);
    return FAILED;
  }

  int64_t super_device_id = -1;
  int64_t super_pod_id = -1;
  if (soc_type == SocType::kV3) {
    acl_ret = aclrtGetDeviceInfo(static_cast<uint32_t>(device_id), ACL_DEV_ATTR_SUPER_POD_ID, &super_pod_id);
    if (acl_ret != ACL_SUCCESS) {
      HIXL_LOGE(FAILED, "aclrtGetDeviceInfo SUPER_POD_ID failed, device_id=%d, acl_ret=%d", device_id, acl_ret);
      return FAILED;
    }
    acl_ret = aclrtGetDeviceInfo(static_cast<uint32_t>(device_id), ACL_DEV_ATTR_SUPER_POD_DEVIDE_ID, &super_device_id);
    if (acl_ret != ACL_SUCCESS) {
      HIXL_LOGE(FAILED, "aclrtGetDeviceInfo SUPER_DEVICE_ID failed, device_id=%d, acl_ret=%d", device_id, acl_ret);
      return FAILED;
    }
  }

  for (auto &ep : endpoint_list) {
    if (ep.placement != kPlacementDevice) {
      continue;
    }
    ep.device_info.phy_device_id = phy_device_id;
    ep.device_info.super_device_id = super_device_id;
    ep.device_info.super_pod_id = super_pod_id;
  }
  return SUCCESS;
}

Status EndpointGenerator::BuildEndpointList(int32_t phy_device_id,
                                            std::vector<loc_comm_res::EndpointInfo> &endpoint_list) {
  endpoint_list.clear();

  loc_comm_res::EndpointInfo roce_endpoint{};
  HIXL_CHK_STATUS_RET(BuildRoceEndpoint(phy_device_id, roce_endpoint), "BuildRoceEndpoint failed, phy_device_id:%d",
                      phy_device_id);
  endpoint_list.emplace_back(roce_endpoint);

  if (IsIntraRoceEnabled()) {
    HIXL_LOGI("HCCL_INTRA_ROCE_ENABLE=1, only generate ROCE endpoint");
    return SUCCESS;
  }

  loc_comm_res::EndpointInfo hccs_endpoint{};
  HIXL_CHK_STATUS_RET(BuildHccsEndpoint(phy_device_id, hccs_endpoint), "BuildHccsEndpoint failed, phy_device_id:%d",
                      phy_device_id);
  endpoint_list.emplace_back(hccs_endpoint);

  return SUCCESS;
}

Status EndpointGenerator::BuildRoceEndpoint(int32_t phy_device_id, loc_comm_res::EndpointInfo &endpoint) {
  std::string device_ip;
  HIXL_CHK_STATUS_RET(GetDeviceIp(phy_device_id, device_ip), "GetDeviceIp failed, phy_device_id:%d", phy_device_id);
  HIXL_CHK_BOOL_RET_STATUS(!device_ip.empty(), FAILED, "Failed to get device ip, phy_device_id:%d", phy_device_id);

  endpoint.protocol = kProtocolRoce;
  endpoint.comm_id = device_ip;
  endpoint.placement = kPlacementDevice;
  return SUCCESS;
}

Status EndpointGenerator::BuildHccsEndpoint(int32_t phy_device_id, loc_comm_res::EndpointInfo &endpoint) {
  endpoint.protocol = kProtocolHccs;
  endpoint.comm_id = std::to_string(phy_device_id);
  endpoint.placement = kPlacementDevice;
  return SUCCESS;
}

Status EndpointGenerator::GetHostIpFromLocalEngine(const std::string &local_engine, std::string &host_ip) {
  int32_t host_port = 0;
  HIXL_CHK_STATUS_RET(ParseListenInfo(local_engine, host_ip, host_port), "Failed to parse host ip from local_engine:%s",
                      local_engine.c_str());
  HIXL_CHK_BOOL_RET_STATUS(!host_ip.empty(), FAILED, "Failed to get host ip from local_engine:%s",
                           local_engine.c_str());
  return SUCCESS;
}

Status EndpointGenerator::GetDeviceIp(int32_t phy_device_id, std::string &device_ip) {
  HIXL_CHK_STATUS_RET(hixl::GetDeviceIp(phy_device_id, device_ip), "GetDeviceIp failed, phy_device_id:%d",
                      phy_device_id);
  HIXL_CHK_BOOL_RET_STATUS(!device_ip.empty(), FAILED,
                           "Failed to get device ip from hccn.conf and hccn_tool, phy_device_id:%d", phy_device_id);
  return SUCCESS;
}
}  // namespace hixl
