/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_FABRIC_MEM_FABRIC_MEM_ADXL_CONTROL_H_
#define CANN_HIXL_SRC_HIXL_FABRIC_MEM_FABRIC_MEM_ADXL_CONTROL_H_

#include <cstdint>
#include <string>

#include "hixl/hixl_types.h"
#include "nlohmann/json.hpp"

namespace hixl {

constexpr uint32_t kFabricMemAdxlMagic = 0xA1B2C3D4U;

struct FabricMemAdxlProtocolHeader {
  uint32_t magic;
  uint64_t body_size;
};

enum class FabricMemAdxlMsgType : int32_t {
  kHeartBeat = 1,
};

struct FabricMemAdxlHeartBeatMsg {
  char msg;
};

inline void to_json(nlohmann::json &j, const FabricMemAdxlHeartBeatMsg &msg) {
  j = nlohmann::json{{"msg", msg.msg}};
}

class FabricMemAdxlControl {
 public:
  static Status SendHeartBeat(int32_t fd, uint64_t timeout_us = 3000000ULL);
  static Status SendMsg(int32_t fd, FabricMemAdxlMsgType msg_type, const std::string &payload, uint64_t timeout_us);
};

}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_FABRIC_MEM_FABRIC_MEM_ADXL_CONTROL_H_
