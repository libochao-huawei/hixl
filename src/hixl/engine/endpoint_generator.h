/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_ENGINE_ENDPOINT_GENERATOR_H_
#define CANN_HIXL_SRC_HIXL_ENGINE_ENDPOINT_GENERATOR_H_

#include <map>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"
#include "cs/hixl_cs.h"
#include "hixl/hixl_types.h"
#include "common/hixl_inner_types.h"

namespace hixl {
class EndpointGenerator {
 public:
  enum class SocType { kV2, kV3, kV5, kOther };

  struct LocalDeviceResource {
    SocType soc_type = SocType::kOther;
    int32_t logic_device_id = -1;
    int32_t phy_device_id = -1;
    int64_t super_device_id = -1;
    int64_t super_pod_id = -1;
  };

  enum class LocalRuntimeMode {
    kHostOnly,
    kDevice,
  };

  struct LocalRuntimeContext {
    LocalRuntimeMode mode = LocalRuntimeMode::kHostOnly;
    bool has_local_device_endpoint = false;
    bool need_device_context = false;
    LocalDeviceResource device_resource{};
  };

  static Status BuildEndpointListFromOptions(const std::map<AscendString, AscendString> &options,
                                             const std::string &local_engine,
                                             std::string &local_comm_res,
                                             std::vector<EndpointConfig> &endpoint_list);
  static Status ConvertToEndpointDesc(const EndpointConfig &endpoint_config,
                                      EndpointDesc &endpoint,
                                      uint32_t dev_phy_id = 0);
  static Status SerializeEndpointConfigList(const std::vector<EndpointConfig> &list, std::string &msg_str);
  static Status DeserializeEndpointConfigList(const std::string &json_str,
                                              std::vector<EndpointConfig> &endpoint_list);
  static bool HasLocalDeviceEndpoint(const std::vector<EndpointConfig> &endpoint_list);
  static bool HasLocalDeviceEndpoint(const EndpointDesc *endpoint_list, uint32_t list_num);
  static Status ResolveLocalRuntimeContext(const std::vector<EndpointConfig> &local_endpoints,
                                           LocalRuntimeContext &ctx);

 private:
  struct EndpointInfo {
    std::string protocol;
    std::string comm_id;
    std::string placement;
  };

  struct LocCommResInfo {
    std::string version;
    std::string net_instance_id;
    std::vector<EndpointInfo> endpoint_list;
  };

  static Status GenerateInfo(int32_t device_id, const std::string &local_engine, LocCommResInfo &loc_comm_res_info);
  static Status GetDeviceIp(int32_t phy_device_id, std::string &device_ip);
  static Status GetSocName(std::string &soc_name);
  static SocType GetSocTypeByName(const std::string &soc_name);
  static Status GetSocType(SocType &soc_type);
  static void ConvertLocCommResInfoToEndpointList(const LocCommResInfo &loc_comm_res_info,
                                                  std::vector<EndpointConfig> &endpoint_list);
  static Status BuildNetInstanceId(int32_t device_id,
                                   const std::string &local_engine,
                                   std::string &net_instance_id);
  static Status BuildEndpointListFromLocalCommRes(const nlohmann::json &config,
                                                  bool has_endpoint_list,
                                                  const std::string &local_engine,
                                                  std::vector<EndpointConfig> &endpoint_list);
  static Status ParseLocalCommRes(const nlohmann::json &config, std::vector<EndpointConfig> &endpoint_list);
  static Status QueryLocalDeviceCount(uint32_t &count);
  static Status QueryLocalDeviceResource(LocalDeviceResource &resource);
  static Status FillDeviceInfoIfNeeded(const LocalDeviceResource &resource, std::vector<EndpointConfig> &endpoint_list);
  static Status BuildEndpointList(int32_t phy_device_id, std::vector<EndpointInfo> &endpoint_list);
  static Status BuildRoceEndpoint(int32_t phy_device_id, EndpointInfo &endpoint);
  static Status BuildHccsEndpoint(int32_t phy_device_id, EndpointInfo &endpoint);
  static Status GetHostIpFromLocalEngine(const std::string &local_engine, std::string &host_ip);
};
}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_ENGINE_ENDPOINT_GENERATOR_H_
