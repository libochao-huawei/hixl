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

namespace hixl {
namespace {

Status ValidateSyncTransferContextParam(TransferContextSyncParam *param) {
  if (param == nullptr) {
    HIXL_LOGE(PARAM_INVALID, "[SyncTransferContext] param is nullptr");
    return PARAM_INVALID;
  }
  if (param->entry_num == 0U) {
    HIXL_LOGE(PARAM_INVALID, "[SyncTransferContext] entry_num is 0");
    return PARAM_INVALID;
  }
  if (param->entry_list_addr == 0U) {
    HIXL_LOGE(PARAM_INVALID, "[SyncTransferContext] entry_list_addr is 0");
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

TransferThreadState TransferContextManager::Add(ThreadHandle thread) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto &ctx = contexts_[thread];
  if (ctx == nullptr) {
    ctx = std::make_shared<TransferContext>();
  }
  ctx->SetState(TRANSFER_THREAD_STATE_INITIALIZED);
  return TRANSFER_THREAD_STATE_INITIALIZED;
}

TransferThreadState TransferContextManager::Destroy(ThreadHandle thread) {
  std::shared_ptr<TransferContext> ctx;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = contexts_.find(thread);
    if (it == contexts_.end() || it->second == nullptr) {
      return TRANSFER_THREAD_STATE_ABORTED;
    }
    ctx = it->second;
    ctx->SetState(TRANSFER_THREAD_STATE_ABORTING);
  }

  std::unique_lock<std::mutex> lock(ctx->mutex, std::try_to_lock);
  if (!lock.owns_lock()) {
    return TRANSFER_THREAD_STATE_ABORTING;
  }
  ctx->SetState(TRANSFER_THREAD_STATE_ABORTED);

  std::lock_guard<std::mutex> manager_lock(mutex_);
  auto it = contexts_.find(thread);
  if (it != contexts_.end() && it->second == ctx) {
    contexts_.erase(it);
  }
  return TRANSFER_THREAD_STATE_ABORTED;
}

uint32_t DoSyncTransferContext(TransferContextSyncParam *param) {
  HIXL_CHK_STATUS_RET(ValidateSyncTransferContextParam(param), "[SyncTransferContext] validate param failed");
  auto *entries = reinterpret_cast<TransferContextSyncEntry *>(static_cast<uintptr_t>(param->entry_list_addr));
  for (uint32_t i = 0U; i < param->entry_num; ++i) {
    TransferThreadState state = TRANSFER_THREAD_STATE_ABORTED;
    if (entries[i].op == TRANSFER_CONTEXT_OP_ADD) {
      state = TransferContextManager::Instance().Add(entries[i].thread);
    } else if (entries[i].op == TRANSFER_CONTEXT_OP_DESTROY) {
      state = TransferContextManager::Instance().Destroy(entries[i].thread);
    } else {
      HIXL_LOGE(PARAM_INVALID, "[SyncTransferContext] invalid op=%u, index=%u", entries[i].op, i);
      return PARAM_INVALID;
    }
    entries[i].state = static_cast<uint32_t>(state);
  }
  return SUCCESS;
}

}  // namespace hixl
