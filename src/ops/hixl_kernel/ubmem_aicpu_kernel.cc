/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "ubmem_stars_sdma.h"

#include "common/hixl_log.h"
#include "cs/ubmem/ubmem_aicpu_types.h"
#include "hixl/hixl_types.h"
#include "transfer_context_manager.h"

namespace hixl {
namespace {
constexpr uint32_t kSuccess = 0U;
constexpr uint32_t kFailed = 1U;
constexpr uint32_t kMaxDescriptorsPerKernel = 128U;

uint32_t ExecuteBatch(UbMemAicpuKernelParam *param, UbMemAicpuTransferDirection expected_direction) {
  auto write_status = [](UbMemAicpuKernelParam *kernel_param, uint32_t result) {
    if (kernel_param != nullptr && kernel_param->status_addr != 0U) {
      *reinterpret_cast<volatile uint32_t *>(static_cast<uintptr_t>(kernel_param->status_addr)) = result;
    }
  };
  if (param == nullptr || param->desc_addr == 0U || param->desc_count == 0U ||
      param->desc_count > kMaxDescriptorsPerKernel || param->direction != static_cast<uint32_t>(expected_direction) ||
      param->transfer_ctx_key == 0U) {
    HIXL_LOGE(PARAM_INVALID,
              "[UbMem][AICPU] invalid kernel param. param=%p desc_addr=%llu desc_count=%u direction=%u expected=%u "
              "transfer_ctx_key=%llu",
              param, param != nullptr ? static_cast<uint64_t>(param->desc_addr) : 0ULL,
              param != nullptr ? param->desc_count : 0U, param != nullptr ? param->direction : 0U,
              static_cast<uint32_t>(expected_direction),
              param != nullptr ? static_cast<uint64_t>(param->transfer_ctx_key) : 0ULL);
    write_status(param, kFailed);
    return kFailed;
  }
  if (param->version != kUbMemKernelParamVersion) {
    HIXL_LOGE(PARAM_INVALID,
              "[UbMem][AICPU] kernel args version mismatch, expected:%u, got:%u. "
              "Host and device tar packages may be from different versions.",
              kUbMemKernelParamVersion, param->version);
    write_status(param, kFailed);
    return kFailed;
  }
  auto ctx = TransferContextManager::Instance().Get(static_cast<ThreadHandle>(param->transfer_ctx_key));
  if (ctx == nullptr || ctx->GetState() != TRANSFER_THREAD_STATE_INITIALIZED) {
    HIXL_LOGE(FAILED, "[UbMem][AICPU] transfer context unavailable, key=%llu",
              static_cast<uint64_t>(param->transfer_ctx_key));
    write_status(param, kFailed);
    return kFailed;
  }
  std::lock_guard<TransferContext> transfer_lock(*ctx);
  if (ctx->GetState() != TRANSFER_THREAD_STATE_INITIALIZED) {
    HIXL_LOGE(FAILED, "[UbMem][AICPU] transfer context deleting after lock, state=%u",
              static_cast<uint32_t>(ctx->GetState()));
    write_status(param, kFailed);
    return kFailed;
  }
  HIXL_LOGI(
      "[UbMem][AICPU] scheduled. direction=%u desc_count=%u device=%u sq=%u stream=%u logic_cq=%u notify=%u "
      "emit_notify=%u",
      param->direction, param->desc_count, param->device_id, param->rtsq_id, param->rtsq_stream_id,
      param->rtsq_logic_cq_id, param->notify_id, param->emit_notify_record);
  const auto *descs = reinterpret_cast<const UbMemAicpuTransferDesc *>(static_cast<uintptr_t>(param->desc_addr));
  const uint32_t result = UbMemStarsSdma::Submit(*param, descs) == kSuccess ? kSuccess : kFailed;
  write_status(param, result);
  return result;
}
}  // namespace
}  // namespace hixl

extern "C" {
uint32_t HixlUbMemBatchRead(hixl::UbMemAicpuKernelParam *param) {
  return hixl::ExecuteBatch(param, hixl::UbMemAicpuTransferDirection::kRead);
}

uint32_t HixlUbMemBatchWrite(hixl::UbMemAicpuKernelParam *param) {
  return hixl::ExecuteBatch(param, hixl::UbMemAicpuTransferDirection::kWrite);
}
}  // extern "C"
