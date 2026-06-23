/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#include "tsfw_notifier.h"
#include "common/hixl_log.h"
#include "ascend_hal.h"
#include "aicpu/aicpu_schedule/aicpu_context.h"

namespace hixl {

namespace {

constexpr uint8_t AICPU_RECORD = 12U;
constexpr uint8_t AICPU_MSG_NOTIFY_RECORD = 2U;

constexpr uint16_t TS_ERROR_SDMA_LINK_ERROR = 0x222U;
constexpr uint16_t TS_ERROR_SDMA_POISON_ERROR = 0x221U;
constexpr uint16_t TS_ERROR_SDMA_DDRC_ERROR = 0x220U;
constexpr uint16_t TS_ERROR_HCCL_OTHER_ERROR = 0x223U;
constexpr uint16_t TS_ERROR_RETRY_CONSTRAINT = 1000U;

constexpr uint32_t RT_SDMA_COMPERR = 1U;
constexpr uint32_t RT_SDMA_COMPDATAERR = 2U;
constexpr uint32_t RT_SDMA_DATAERR = 3U;

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

uint16_t ConvertSdmaCqeErrCodeToTsErrCode(uint32_t cqe_err_code) {
  switch (cqe_err_code) {
    case RT_SDMA_COMPERR:
      return TS_ERROR_SDMA_LINK_ERROR;
    case RT_SDMA_COMPDATAERR:
      return TS_ERROR_SDMA_POISON_ERROR;
    case RT_SDMA_DATAERR:
      return TS_ERROR_SDMA_DDRC_ERROR;
    case TS_ERROR_RETRY_CONSTRAINT:
      return TS_ERROR_RETRY_CONSTRAINT;
    default:
      return TS_ERROR_HCCL_OTHER_ERROR;
  }
}

}  // namespace

uint32_t NotifyTsfwTaskException(uint32_t notify_id, int32_t user_stream_id, uint32_t error_code) {
  if (halEschedSubmitEvent == nullptr) {
    HIXL_LOGI("[TsfwNotifier] halEschedSubmitEvent is null, API unavailable");
    return TSFW_NOTIFY_API_UNAVAILABLE;
  }

  aicpu::aicpuContext_t ctx = {};
  if (aicpu::aicpuGetContext == nullptr) {
    HIXL_LOGI("[TsfwNotifier] aicpuGetContext is null, API unavailable");
    return TSFW_NOTIFY_API_UNAVAILABLE;
  }
  aicpu::status_t ctx_ret = aicpu::aicpuGetContext(&ctx);
  if (ctx_ret != aicpu::AICPU_ERROR_NONE) {
    HIXL_LOGI("[TsfwNotifier] aicpuGetContext failed, ret=%d", ctx_ret);
    return TSFW_NOTIFY_API_UNAVAILABLE;
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
  aicpu_sqe.aicpu_record.ret_code = ConvertSdmaCqeErrCodeToTsErrCode(error_code);

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
    HIXL_LOGI("[TsfwNotifier] halEschedSubmitEvent failed, ret=%d, streamId=%d, notifyId=%u", ret, user_stream_id,
              notify_id);
    return TSFW_NOTIFY_DRV_ERROR;
  }

  HIXL_LOGI("[TsfwNotifier] Submit to TSFW success. deviceId=%u, streamId=%d, notifyId=%u, errCode=%u", ctx.deviceId,
            user_stream_id, notify_id, error_code);
  return TSFW_NOTIFY_SUCCESS;
}

}  // namespace hixl
