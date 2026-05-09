/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_ENGINE_CLIENT_HANDLER_FACTORY_H_
#define CANN_HIXL_SRC_HIXL_ENGINE_CLIENT_HANDLER_FACTORY_H_

#include <map>
#include <memory>
#include <string>
#include <sstream>
#include <vector>
#include "common/hixl_inner_types.h"
#include "engine/client_handler.h"

namespace hixl {

struct MatchKey {
  std::string dst_eid;
  std::string plane;
  std::string placement;

  std::string ToString() const {
    std::ostringstream oss;
    oss << "MatchKey{";
    oss << "dst_eid: " << dst_eid << ", ";
    oss << "plane: " << plane << ", ";
    oss << "placement: " << placement;
    oss << "}";
    return oss.str();
  }

  bool operator<(const MatchKey &other) const {
    if (dst_eid != other.dst_eid) {
      return dst_eid < other.dst_eid;
    } else if (plane != other.plane) {
      return plane < other.plane;
    } else {
      return placement < other.placement;
    }
  }

  bool Matches(const MatchKey &query) const {
    if (!dst_eid.empty() && !query.dst_eid.empty() && (dst_eid != query.dst_eid)) {
      return false;
    }
    if (plane != query.plane) {
      return false;
    }
    if (placement != query.placement) {
      return false;
    }
    return true;
  }
};

class ClientHandlerFactory {
 public:
  static std::unique_ptr<IClientHandler> Create(
      const std::string &server_ip, uint32_t server_port, uint8_t rdma_tc, uint8_t rdma_sl,
      const std::vector<EndpointConfig> &local_endpoints,
      const std::vector<EndpointConfig> &remote_endpoints);
};

}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_ENGINE_CLIENT_HANDLER_FACTORY_H_
