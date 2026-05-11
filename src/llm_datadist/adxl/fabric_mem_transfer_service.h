/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_GRAPH_ENGINE_HCCS_TRANSFER_SERVICE_H
#define CANN_GRAPH_ENGINE_HCCS_TRANSFER_SERVICE_H

#include <vector>

#include "adxl/adxl_types.h"
#include "channel.h"
#include "fabric_mem/fabric_mem_statistic.h"
#include "fabric_mem/fabric_mem_transfer_service.h"

namespace adxl {
class FabricMemTransferService {
 public:
  FabricMemTransferService() = default;

  Status Initialize(size_t max_stream_num, size_t task_stream_num);
  void Finalize();

  Status RegisterMem(const MemDesc &mem, MemType type, MemHandle &mem_handle);
  Status DeregisterMem(MemHandle mem_handle);

  Status Transfer(const ChannelPtr &channel, TransferOp operation, const std::vector<TransferOpDesc> &op_descs,
                  int32_t timeout_in_millis);
  Status TransferAsync(const ChannelPtr &channel, TransferOp operation, const std::vector<TransferOpDesc> &op_descs,
                       TransferReq &req);
  Status GetTransferStatus(const ChannelPtr &channel, const TransferReq &req, TransferStatus &status);

  std::vector<ShareHandleInfo> GetShareHandles();
  void RemoveChannel(const std::string &channel_id);
  hixl::FabricMemTransferService *GetInnerService();

  static Status MallocMem(MemType type, size_t size, void **ptr);
  static Status FreeMem(void *ptr);

 private:
  static hixl::FabricMemTransferContext BuildContext(const ChannelPtr &channel);

  hixl::FabricMemStatistic statistic_;
  hixl::FabricMemTransferService service_;
};
}  // namespace adxl

#endif  // CANN_GRAPH_ENGINE_HCCS_TRANSFER_SERVICE_H
