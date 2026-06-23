/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#include "hixl_sync_transfer_ctx.h"
#include "tsfw_notifier.h"
#include "common/hixl_log.h"
#include "common/hixl_checker.h"
#include "hccl/hccl_types.h"
#include <unordered_map>
#include <mutex>

namespace hixl {
namespace {

constexpr HcclResult HCOMM_API_SUCCESS = HCCL_SUCCESS;

struct TransferCtxEntry {
  uint32_t user_stream_id;
  uint32_t notify_id;
  uint64_t err_flag_dev_va;
};

static std::unordered_map<uint64_t, TransferCtxEntry> g_transfer_ctx_table;
static std::mutex g_table_mutex;
static bool g_callback_registered = false;

typedef void (*HcommTaskExceptionCallbackFn)(uint64_t thread_handle, uint32_t error_code, uint32_t exe_stream_id,
                                             uint32_t task_id);

extern "C" {
HcclResult __attribute__((weak)) HcommRegisterTaskExceptionCallback(const char *module_name,
                                                                    HcommTaskExceptionCallbackFn callback);
}

void HixlTaskExceptionCallback(uint64_t thread_handle, uint32_t error_code, uint32_t exe_stream_id, uint32_t task_id) {
  HIXL_LOGI("[HixlSyncTransferCtx] Exception callback triggered. thread=%lu, err=%u, stream=%u, task=%u", thread_handle,
            error_code, exe_stream_id, task_id);

  std::lock_guard<std::mutex> lock(g_table_mutex);
  auto it = g_transfer_ctx_table.find(thread_handle);
  if (it == g_transfer_ctx_table.end()) {
    HIXL_LOGI("[HixlSyncTransferCtx] Thread %lu not found in mapping table", thread_handle);
    return;
  }

  const TransferCtxEntry &entry = it->second;
  volatile uint8_t *err_flag_ptr = reinterpret_cast<volatile uint8_t *>(entry.err_flag_dev_va);
  *err_flag_ptr = static_cast<uint8_t>(error_code);
  HIXL_LOGI("[HixlSyncTransferCtx] Written err_flag=%u to va=%lu", error_code, entry.err_flag_dev_va);

  uint32_t notify_ret =
      NotifyTsfwTaskException(entry.notify_id, static_cast<int32_t>(entry.user_stream_id), error_code);
  if (notify_ret != TSFW_NOTIFY_SUCCESS) {
    HIXL_LOGI("[HixlSyncTransferCtx] NotifyTsfwTaskException failed, ret=%u", notify_ret);
  }
}

uint32_t ProcessSyncTypeAdd(const HixlSyncTransferCtxOp &op) {
  TransferCtxEntry entry;
  entry.user_stream_id = op.user_stream_id;
  entry.notify_id = op.notify_id;
  entry.err_flag_dev_va = op.err_flag_dev_va;

  {
    std::lock_guard<std::mutex> lock(g_table_mutex);
    g_transfer_ctx_table[op.thread_handle] = entry;
    HIXL_LOGI("[HixlSyncTransferCtx] ADD: thread=%lu, stream=%u, notify=%u, err_flag_va=%lu", op.thread_handle,
              op.user_stream_id, op.notify_id, op.err_flag_dev_va);

    if (!g_callback_registered && HcommRegisterTaskExceptionCallback != nullptr) {
      HcclResult ret = HcommRegisterTaskExceptionCallback("HIXL", HixlTaskExceptionCallback);
      if (ret == HCOMM_API_SUCCESS) {
        g_callback_registered = true;
        HIXL_LOGI("[HixlSyncTransferCtx] HcommRegisterTaskExceptionCallback success");
      } else {
        HIXL_LOGI("[HixlSyncTransferCtx] HcommRegisterTaskExceptionCallback failed, ret=%d", ret);
      }
    } else if (!g_callback_registered && HcommRegisterTaskExceptionCallback == nullptr) {
      HIXL_LOGI("[HixlSyncTransferCtx] HcommRegisterTaskExceptionCallback API unavailable");
    }
  }
  return SUCCESS;
}

uint32_t ProcessSyncTypeDelete(const HixlSyncTransferCtxOp &op) {
  {
    std::lock_guard<std::mutex> lock(g_table_mutex);
    auto it = g_transfer_ctx_table.find(op.thread_handle);
    if (it != g_transfer_ctx_table.end()) {
      g_transfer_ctx_table.erase(it);
      HIXL_LOGI("[HixlSyncTransferCtx] DELETE: thread=%lu removed", op.thread_handle);
    } else {
      HIXL_LOGI("[HixlSyncTransferCtx] DELETE: thread=%lu not found", op.thread_handle);
    }

    if (g_transfer_ctx_table.empty() && g_callback_registered && HcommRegisterTaskExceptionCallback != nullptr) {
      HcclResult ret = HcommRegisterTaskExceptionCallback("HIXL", nullptr);
      if (ret == HCOMM_API_SUCCESS) {
        g_callback_registered = false;
        HIXL_LOGI("[HixlSyncTransferCtx] Unregister callback success");
      } else {
        HIXL_LOGI("[HixlSyncTransferCtx] Unregister callback failed, ret=%d", ret);
      }
    } else if (g_transfer_ctx_table.empty() && g_callback_registered && HcommRegisterTaskExceptionCallback == nullptr) {
      HIXL_LOGI("[HixlSyncTransferCtx] HcommRegisterTaskExceptionCallback API unavailable");
    }
  }
  return SUCCESS;
}

uint32_t HixlSyncTransferCtxImpl(HixlSyncTransferCtxParam *param) {
  if (param == nullptr) {
    HIXL_LOGE(PARAM_INVALID, "[HixlSyncTransferCtx] param is nullptr");
    return PARAM_INVALID;
  }
  if (param->count == 0U) {
    HIXL_LOGI("[HixlSyncTransferCtx] count is 0, nothing to do");
    return SUCCESS;
  }
  if (param->entries_addr == 0U) {
    HIXL_LOGE(PARAM_INVALID, "[HixlSyncTransferCtx] entries_addr is null");
    return PARAM_INVALID;
  }

  auto *ops = reinterpret_cast<HixlSyncTransferCtxOp *>(static_cast<uintptr_t>(param->entries_addr));
  for (uint32_t i = 0U; i < param->count; i++) {
    const HixlSyncTransferCtxOp &op = ops[i];
    HIXL_LOGI("[HixlSyncTransferCtx] Processing op[%u]: sync_type=%u, thread=%lu", i, op.sync_type, op.thread_handle);

    uint32_t ret = SUCCESS;
    if (op.sync_type == SYNC_TYPE_ADD) {
      ret = ProcessSyncTypeAdd(op);
    } else if (op.sync_type == SYNC_TYPE_DELETE) {
      ret = ProcessSyncTypeDelete(op);
    } else {
      HIXL_LOGE(PARAM_INVALID, "[HixlSyncTransferCtx] Invalid sync_type=%u at index %u", op.sync_type, i);
      return PARAM_INVALID;
    }
    if (ret != SUCCESS) {
      HIXL_LOGE(ret, "[HixlSyncTransferCtx] Process op[%u] failed, ret=%u", i, ret);
      return ret;
    }
  }

  return SUCCESS;
}

}  // namespace
}  // namespace hixl

extern "C" {
uint32_t HixlSyncTransferCtx(HixlSyncTransferCtxParam *param) {
  return hixl::HixlSyncTransferCtxImpl(param);
}
}  // extern "C"
