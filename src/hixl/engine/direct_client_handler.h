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
  DirectClientHandler() = default;
  ~DirectClientHandler() override = default;

  std::mutex &GetHandleMutex() override { return handle_mutex_; }
  std::mutex &GetMemHandleMutex() override { return mem_handle_mutex_; }
  std::map<CommType, HixlClientHandle> &GetHandles() override { return handles_; }
  std::map<CommType, std::vector<MemHandle>> &GetMemHandles() override { return mem_handles_; }

  Status ClassifyTransfers(const std::vector<TransferOpDesc> &op_descs,
                           std::map<CommType, std::vector<TransferOpDesc>> &table) override;

  Status ProcessLocalMem(const MemInfo &mem_info, const std::string &server_ip,
                         uint32_t server_port, uint8_t rdma_tc, uint8_t rdma_sl) override;

  Status AddRemoteMem(CommMem *remote_mem_list, uint32_t list_num) override;

  void Clear() override;

 private:
  std::map<CommType, HixlClientHandle> handles_;
  std::map<CommType, std::vector<MemHandle>> mem_handles_;
  std::mutex handle_mutex_;
  std::mutex mem_handle_mutex_;
};

}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_ENGINE_DIRECT_CLIENT_HANDLER_H_
