/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#include "task_exception_handler.h"

#include "common/hixl_log.h"
#include "common/hixl_checker.h"
#include "hixl/hixl_types.h"
#include "hccl/hccl_types.h"
#include "transfer_context_manager.h"
#include "ascend_hal.h"
#include "aicpu/aicpu_schedule/aicpu_context.h"

namespace hixl {
namespace {

constexpr uint8_t AICPU_RECORD = 12U;
constexpr uint8_t AICPU_MSG_NOTIFY_RECORD = 2U;

struct TsAicpuRecord {
  volatile uint32_t record_id;
  volatile uint8_t record_type;
  volatile uint8_t reserved;
  volatile uint16_t ret_code;
  volatile uint16_t fault_task_id;
  volatile uint16_t fault_stream_id;
};

struct TsAicpuSqe {
  volatile uint32_t pid;
  volatile uint8_t cmd_type;
  volatile uint8_t vf_id;
  volatile uint8_t tid;
  volatile uint8_t ts_id;
  TsAicpuRecord aicpu_record;
};

Status NotifyTsfwTaskException(uint32_t notify_id, int32_t user_stream_id, uint32_t error_code) {
  if (halEschedSubmitEvent == nullptr) {
    HIXL_LOGI("[TaskExceptionHandler] halEschedSubmitEvent is null, API unavailable");
    return FAILED;
  }

  aicpu::aicpuContext_t ctx = {};
  if (aicpu::aicpuGetContext == nullptr) {
    HIXL_LOGI("[TaskExceptionHandler] aicpuGetContext is null, API unavailable");
    return FAILED;
  }
  aicpu::status_t ctx_ret = aicpu::aicpuGetContext(&ctx);
  if (ctx_ret != aicpu::AICPU_ERROR_NONE) {
    HIXL_LOGI("[TaskExceptionHandler] aicpuGetContext failed, ret=%d", ctx_ret);
    return FAILED;
  }

  TsAicpuSqe aicpu_sqe = {};
  aicpu_sqe.pid = static_cast<uint32_t>(ctx.hostPid);
  aicpu_sqe.cmd_type = AICPU_RECORD;
  aicpu_sqe.vf_id = static_cast<uint8_t>(ctx.vfId);
  aicpu_sqe.tid = 0U;
  aicpu_sqe.ts_id = static_cast<uint8_t>(ctx.tsId);
  aicpu_sqe.aicpu_record.record_type = AICPU_MSG_NOTIFY_RECORD;
  aicpu_sqe.aicpu_record.record_id = notify_id;
  aicpu_sqe.aicpu_record.fault_stream_id = static_cast<uint16_t>(user_stream_id);
  aicpu_sqe.aicpu_record.ret_code = static_cast<uint16_t>(error_code);

  struct event_summary event = {};
  event.dst_engine = TS_CPU;
  event.policy = ONLY;
  event.pid = 0;
  event.grp_id = 0;
  event.event_id = EVENT_TS_CTRL_MSG;
  event.subevent_id = 0U;
  event.msg_len = static_cast<uint32_t>(sizeof(TsAicpuSqe));
  event.msg = reinterpret_cast<char *>(&aicpu_sqe);

  drvError_t ret = halEschedSubmitEvent(ctx.deviceId, &event);
  if (ret != DRV_ERROR_NONE) {
    HIXL_LOGI("[TaskExceptionHandler] halEschedSubmitEvent failed, ret=%d, streamId=%d, notifyId=%u", ret,
              user_stream_id, notify_id);
    return FAILED;
  }

  HIXL_LOGI("[TaskExceptionHandler] Submit to TSFW success. deviceId=%u, streamId=%d, notifyId=%u, errCode=%u",
            ctx.deviceId, user_stream_id, notify_id, error_code);
  return SUCCESS;
}

typedef void (*HcommTaskExceptionCallbackFn)(uint64_t thread_handle, uint32_t error_code, uint32_t exe_stream_id,
                                             uint32_t task_id);

extern "C" {
HcclResult __attribute__((weak)) HcommRegisterTaskExceptionCallback(const char *module_name,
                                                                    HcommTaskExceptionCallbackFn callback);
}

void HixlTaskExceptionCallback(uint64_t thread_handle, uint32_t error_code, uint32_t exe_stream_id, uint32_t task_id) {
  HIXL_LOGI("[TaskExceptionHandler] Exception callback triggered. thread=%lu, err=%u, stream=%u, task=%u",
            thread_handle, error_code, exe_stream_id, task_id);

  auto ctx = TransferContextManager::Instance().Get(thread_handle);
  if (ctx == nullptr) {
    HIXL_LOGI("[TaskExceptionHandler] Thread %lu not found in mapping table", thread_handle);
    return;
  }

  if (ctx->GetState() != TRANSFER_THREAD_STATE_INITIALIZED) {
    HIXL_LOGI("[TaskExceptionHandler] Thread %lu state is not INITIALIZED, state=%u", thread_handle,
              static_cast<uint32_t>(ctx->GetState()));
    return;
  }

  if (ctx->err_flag_dev_va != 0U) {
    volatile uint8_t *err_flag_ptr = reinterpret_cast<volatile uint8_t *>(ctx->err_flag_dev_va);
    *err_flag_ptr = static_cast<uint8_t>(error_code);
    HIXL_LOGI("[TaskExceptionHandler] Written err_flag=%u to va=%lu", error_code, ctx->err_flag_dev_va);
  }

  Status notify_ret = NotifyTsfwTaskException(ctx->notify_id, static_cast<int32_t>(ctx->user_stream_id), error_code);
  if (notify_ret != SUCCESS) {
    HIXL_LOGI("[TaskExceptionHandler] NotifyTsfwTaskException failed, ret=%u", notify_ret);
  }
}

}  // namespace

TaskExceptionHandler &TaskExceptionHandler::Instance() {
  static TaskExceptionHandler handler;
  return handler;
}

void TaskExceptionHandler::EnableExceptionCallback() {
  if (HcommRegisterTaskExceptionCallback != nullptr) {
    HcclResult ret = HcommRegisterTaskExceptionCallback("HIXL", HixlTaskExceptionCallback);
    if (ret != HCCL_SUCCESS) {
      HIXL_LOGI("[TaskExceptionHandler] HcommRegisterTaskExceptionCallback failed, ret=%d", ret);
    }
  } else {
    HIXL_LOGI("[TaskExceptionHandler] HcommRegisterTaskExceptionCallback API unavailable");
  }
}

void TaskExceptionHandler::DisableExceptionCallback() {
  if (HcommRegisterTaskExceptionCallback != nullptr) {
    HcclResult ret = HcommRegisterTaskExceptionCallback("HIXL", nullptr);
    if (ret != HCCL_SUCCESS) {
      HIXL_LOGI("[TaskExceptionHandler] Unregister callback failed, ret=%d", ret);
    }
  }
}

}  // namespace hixl
