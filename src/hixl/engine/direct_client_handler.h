/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_ENGINE_DIRECT_CLIENT_HANDLER_H_
#define CANN_HIXL_SRC_HIXL_ENGINE_DIRECT_CLIENT_HANDLER_H_

#include <map>
#include <mutex>
#include <vector>
#include "engine/client_handler.h"

namespace hixl {

class DirectClientHandler : public IClientHandler {
 public:
  explicit DirectClientHandler(std::map<CommType, HixlClientHandle> handles);
  ~DirectClientHandler() override = default;

  Status Connect(uint32_t timeout_ms) override;
  Status RegisterMem(const MemInfo &mem_info) override;
  Status Transfer(const std::vector<TransferOpDesc> &op_descs, TransferOp operation,
                  std::vector<TransferCompleteInfo> &complete_handle_list) override;
  Status TransferSync(const std::vector<TransferOpDesc> &op_descs, TransferOp operation,
                      uint32_t timeout_ms) override;
  Status QueryStatus(CommType type, CompleteHandle handle, HixlCompleteStatus &status) override;
  Status Finalize() override;

 private:
  HixlClientHandle SingleHandle() const;
  CommType SingleType() const;

  std::map<CommType, HixlClientHandle> handles_;
  std::map<CommType, std::vector<MemHandle>> mem_handles_;
  std::mutex mutex_;
};

}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_ENGINE_DIRECT_CLIENT_HANDLER_H_
