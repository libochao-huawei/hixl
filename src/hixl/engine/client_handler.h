/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_ENGINE_CLIENT_HANDLER_H_
#define CANN_HIXL_SRC_HIXL_ENGINE_CLIENT_HANDLER_H_

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include "cs/hixl_cs.h"
#include "common/hixl_inner_types.h"
#include "common/segment.h"

namespace hixl {

enum class CommType : uint32_t {
  COMM_TYPE_UB_D2D = 0U,
  COMM_TYPE_UB_H2D = 1U,
  COMM_TYPE_UB_D2H = 2U,
  COMM_TYPE_UB_H2H = 3U,
  COMM_TYPE_ROCE = 4U,
  COMM_TYPE_HCCS = 5U,
  COMM_TYPE_UBOE = 6U
};

struct TransferCompleteInfo {
  CommType type;
  void *complete_handle;
};

class IClientHandler {
 public:
  virtual ~IClientHandler() = default;

  virtual Status Connect(uint32_t timeout_ms) = 0;
  virtual Status FetchRemoteMem(uint32_t timeout_ms) = 0;
  virtual Status RegisterMem(const MemInfo &mem_info) = 0;

  virtual Status Transfer(const std::vector<TransferOpDesc> &op_descs, TransferOp operation,
                          std::vector<TransferCompleteInfo> &complete_handle_list) = 0;

  virtual Status TransferSync(const std::vector<TransferOpDesc> &op_descs, TransferOp operation,
                              uint32_t timeout_ms) = 0;

  virtual Status QueryStatus(CommType type, CompleteHandle handle, HixlCompleteStatus &status) = 0;

  virtual Status Finalize() = 0;
};

}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_ENGINE_CLIENT_HANDLER_H_
