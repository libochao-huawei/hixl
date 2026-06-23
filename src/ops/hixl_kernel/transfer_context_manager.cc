/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#include "transfer_context_manager.h"

#include "common/hixl_checker.h"
#include "common/hixl_log.h"
#include "tsfw_notifier.h"
#include "hccl/hccl_types.h"

namespace hixl {
namespace {

typedef void (*HcommTaskExceptionCallbackFn)(uint64_t thread_handle, uint32_t error_code, uint32_t exe_stream_id,
                                             uint32_t task_id);

extern "C" {
HcclResult __attribute__((weak)) HcommRegisterTaskExceptionCallback(const char *module_name,
                                                                    HcommTaskExceptionCallbackFn callback);
}

void HixlTaskExceptionCallback(uint64_t thread_handle, uint32_t error_code, uint32_t exe_stream_id, uint32_t task_id) {
  HIXL_LOGI("[HixlSyncTransferContext] Exception callback triggered. thread=%lu, err=%u, stream=%u, task=%u",
            thread_handle, error_code, exe_stream_id, task_id);

  auto ctx = TransferContextManager::Instance().Get(thread_handle);
  if (ctx == nullptr) {
    HIXL_LOGI("[HixlSyncTransferContext] Thread %lu not found in mapping table", thread_handle);
    return;
  }

  if (ctx->GetState() != TRANSFER_THREAD_STATE_INITIALIZED) {
    HIXL_LOGI("[HixlSyncTransferContext] Thread %lu state is not INITIALIZED, state=%u", thread_handle,
              static_cast<uint32_t>(ctx->GetState()));
    return;
  }

  if (ctx->err_flag_dev_va != 0U) {
    volatile uint8_t *err_flag_ptr = reinterpret_cast<volatile uint8_t *>(ctx->err_flag_dev_va);
    *err_flag_ptr = static_cast<uint8_t>(error_code);
    HIXL_LOGI("[HixlSyncTransferContext] Written err_flag=%u to va=%lu", error_code, ctx->err_flag_dev_va);
  }

  uint32_t notify_ret = NotifyTsfwTaskException(ctx->notify_id, static_cast<int32_t>(ctx->user_stream_id), error_code);
  if (notify_ret != TSFW_NOTIFY_SUCCESS) {
    HIXL_LOGI("[HixlSyncTransferContext] NotifyTsfwTaskException failed, ret=%u", notify_ret);
  }
}

Status ValidateSyncTransferContextParam(const TransferContextSyncParam *param) {
  if (param == nullptr) {
    HIXL_LOGE(PARAM_INVALID, "[HixlSyncTransferContext] param is nullptr");
    return PARAM_INVALID;
  }
  if (param->entry_num == 0U) {
    HIXL_LOGE(PARAM_INVALID, "[HixlSyncTransferContext] entry_num is 0");
    return PARAM_INVALID;
  }
  if (param->entry_list_addr == 0U) {
    HIXL_LOGE(PARAM_INVALID, "[HixlSyncTransferContext] entry_list_addr is 0");
    return PARAM_INVALID;
  }
  if (param->state_list_addr == 0U) {
    HIXL_LOGE(PARAM_INVALID, "[HixlSyncTransferContext] state_list_addr is 0");
    return PARAM_INVALID;
  }
  return SUCCESS;
}

}  // namespace

TransferContextManager &TransferContextManager::Instance() {
  static TransferContextManager manager;
  return manager;
}

std::shared_ptr<TransferContext> TransferContextManager::Get(ThreadHandle thread) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = contexts_.find(thread);
  if (it == contexts_.end()) {
    return nullptr;
  }
  return it->second;
}

TransferThreadState TransferContextManager::Add(ThreadHandle thread, uint32_t user_stream_id, uint32_t notify_id,
                                                uint64_t err_flag_dev_va) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto &ctx = contexts_[thread];
  if (ctx == nullptr) {
    ctx = std::make_shared<TransferContext>();
  }
  ctx->SetState(TRANSFER_THREAD_STATE_INITIALIZED);
  ctx->user_stream_id = user_stream_id;
  ctx->notify_id = notify_id;
  ctx->err_flag_dev_va = err_flag_dev_va;

  if (HcommRegisterTaskExceptionCallback != nullptr) {
    HcclResult ret = HcommRegisterTaskExceptionCallback("HIXL", HixlTaskExceptionCallback);
    if (ret != HCCL_SUCCESS) {
      HIXL_LOGI("[HixlSyncTransferContext] HcommRegisterTaskExceptionCallback failed, ret=%d", ret);
    }
  } else {
    HIXL_LOGI("[HixlSyncTransferContext] HcommRegisterTaskExceptionCallback API unavailable");
  }

  return TRANSFER_THREAD_STATE_INITIALIZED;
}

TransferThreadState TransferContextManager::Delete(ThreadHandle thread) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = contexts_.find(thread);
  if (it == contexts_.end() || it->second == nullptr) {
    return TRANSFER_THREAD_STATE_DELETED;
  }

  auto ctx = it->second;
  ctx->SetState(TRANSFER_THREAD_STATE_DELETING);
  if (!ctx->try_lock()) {
    return TRANSFER_THREAD_STATE_DELETING;
  }
  ctx->SetState(TRANSFER_THREAD_STATE_DELETED);
  contexts_.erase(it);
  ctx->unlock();

  if (contexts_.empty() && HcommRegisterTaskExceptionCallback != nullptr) {
    HcclResult ret = HcommRegisterTaskExceptionCallback("HIXL", nullptr);
    if (ret != HCCL_SUCCESS) {
      HIXL_LOGI("[HixlSyncTransferContext] Unregister callback failed, ret=%d", ret);
    }
  }

  return TRANSFER_THREAD_STATE_DELETED;
}

uint32_t DoSyncTransferContext(TransferContextSyncParam *param) {
  HIXL_CHK_STATUS_RET(ValidateSyncTransferContextParam(param), "[HixlSyncTransferContext] validate param failed");
  HIXL_LOGI("[HixlSyncTransferContext] device execute start. entry_num=%u", param->entry_num);
  auto *entries = reinterpret_cast<TransferContextSyncEntry *>(static_cast<uintptr_t>(param->entry_list_addr));
  auto *states = reinterpret_cast<uint32_t *>(static_cast<uintptr_t>(param->state_list_addr));
  TransferThreadState state = TRANSFER_THREAD_STATE_DELETED;
  for (uint32_t i = 0U; i < param->entry_num; ++i) {
    if (entries[i].op == TRANSFER_CONTEXT_OP_ADD) {
      state = TransferContextManager::Instance().Add(entries[i].thread, entries[i].user_stream_id, entries[i].notify_id,
                                                     entries[i].err_flag_dev_va);
    } else if (entries[i].op == TRANSFER_CONTEXT_OP_DELETE) {
      state = TransferContextManager::Instance().Delete(entries[i].thread);
    } else {
      HIXL_LOGE(PARAM_INVALID, "[HixlSyncTransferContext] invalid op=%u, index=%u", entries[i].op, i);
      return PARAM_INVALID;
    }
    states[i] = static_cast<uint32_t>(state);
  }
  HIXL_LOGI("[HixlSyncTransferContext] device execute end. entry_num=%u last_state=%u", param->entry_num,
            static_cast<uint32_t>(state));
  return SUCCESS;
}

}  // namespace hixl
