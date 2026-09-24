/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_CS_UBMEM_UB_MEM_AICPU_PARAM_H_
#define CANN_HIXL_SRC_HIXL_CS_UBMEM_UB_MEM_AICPU_PARAM_H_

#include <cstdint>
#include <vector>

#include "cs/ubmem/ubmem_aicpu_types.h"
#include "cs/hixl_cs.h"
#include "cs/transfer_pool.h"

namespace hixl {

Status BuildUbMemTransferDescs(bool is_get, const HixlOneSideOpDesc *src, uint32_t list_num,
                               std::vector<UbMemAicpuTransferDesc> &dst);
Status FillUbMemKernelParam(const TransferPool::SlotHandle &slot, const void *desc_buf, uint32_t chunk_offset,
                            uint32_t chunk_count, bool is_get, bool emit_notify, uint32_t timeout_ms,
                            UbMemAicpuKernelParam &param);

}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_CS_UBMEM_UB_MEM_AICPU_PARAM_H_
