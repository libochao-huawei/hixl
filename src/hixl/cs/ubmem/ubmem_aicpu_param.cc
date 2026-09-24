/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "cs/ubmem/ubmem_aicpu_param.h"

#include <algorithm>
#include <limits>
#include <mutex>
#include <unordered_map>

#include "acl/acl_rt.h"
#include "common/hixl_checker.h"
#include "common/hixl_log.h"
#include "runtime/rt_external_stream.h"

namespace hixl {
namespace {
constexpr uint64_t kMaxSdmaTransferBytes = std::numeric_limits<uint32_t>::max();

std::mutex g_task_id_mutex;
std::unordered_map<uint32_t, uint32_t> g_rtsq_next_task_ids;

uint32_t ReserveRtsqTaskIds(uint32_t sq_id, uint32_t task_count) {
  std::lock_guard<std::mutex> lock(g_task_id_mutex);
  const uint32_t first_task_id = g_rtsq_next_task_ids[sq_id];
  g_rtsq_next_task_ids[sq_id] = first_task_id + task_count;
  return first_task_id;
}

Status AppendSplitDescs(uint64_t src_addr, uint64_t dst_addr, uint64_t len, std::vector<UbMemAicpuTransferDesc> &dst) {
  HIXL_CHK_BOOL_RET_STATUS(src_addr != 0U && dst_addr != 0U && len > 0U, PARAM_INVALID,
                           "[UbMemAicpuParam] Invalid transfer descriptor.");
  HIXL_CHK_BOOL_RET_STATUS(
      len <= std::numeric_limits<uint64_t>::max() - src_addr && len <= std::numeric_limits<uint64_t>::max() - dst_addr,
      PARAM_INVALID, "[UbMemAicpuParam] Transfer descriptor address range overflows.");
  while (len > 0U) {
    const uint64_t block_size = std::min(len, kMaxSdmaTransferBytes);
    UbMemAicpuTransferDesc desc{};
    desc.src_addr = src_addr;
    desc.dst_addr = dst_addr;
    desc.length = block_size;
    dst.emplace_back(desc);
    src_addr += block_size;
    dst_addr += block_size;
    len -= block_size;
  }
  return SUCCESS;
}

Status FillRtsqMeta(aclrtStream worker_stream, uint32_t task_count, int32_t logic_device_id,
                    UbMemAicpuKernelParam &param) {
  HIXL_CHECK_NOTNULL(worker_stream, "[UbMemAicpuParam] ubmem_stream is null");
  HIXL_CHK_BOOL_RET_STATUS(task_count > 0U, PARAM_INVALID, "[UbMemAicpuParam] RTSQ task count is 0");
  int32_t stream_id = -1;
  HIXL_CHK_ACL_RET(aclrtStreamGetId(worker_stream, &stream_id), "[UbMemAicpuParam] aclrtStreamGetId failed");
  HIXL_CHK_BOOL_RET_STATUS(stream_id >= 0 && stream_id <= std::numeric_limits<uint16_t>::max(), UNSUPPORTED,
                           "[UbMemAicpuParam] RTSQ stream id is outside the A3 SQE ABI range");
  uint32_t sq_id = 0U;
  HIXL_CHK_BOOL_RET_STATUS(rtStreamGetSqid(reinterpret_cast<rtStream_t>(worker_stream), &sq_id) == RT_ERROR_NONE,
                           UNSUPPORTED, "[UbMemAicpuParam] rtStreamGetSqid failed");
  uint32_t cq_id = 0U;
  uint32_t logic_cq_id = 0U;
  HIXL_CHK_BOOL_RET_STATUS(
      rtStreamGetCqid(reinterpret_cast<rtStream_t>(worker_stream), &cq_id, &logic_cq_id) == RT_ERROR_NONE, UNSUPPORTED,
      "[UbMemAicpuParam] rtStreamGetCqid failed");
  (void)cq_id;
  int32_t physical_device_id = -1;
  HIXL_CHK_BOOL_RET_STATUS(
      aclrtGetPhyDevIdByLogicDevId(logic_device_id, &physical_device_id) == ACL_SUCCESS && physical_device_id >= 0,
      UNSUPPORTED, "[UbMemAicpuParam] aclrtGetPhyDevIdByLogicDevId failed, logic_device_id:%d", logic_device_id);
  param.device_id = static_cast<uint32_t>(physical_device_id);
  param.rtsq_id = sq_id;
  param.rtsq_stream_id = static_cast<uint32_t>(stream_id);
  param.rtsq_logic_cq_id = logic_cq_id;
  param.rtsq_task_id = static_cast<uint16_t>(ReserveRtsqTaskIds(sq_id, task_count));
  return SUCCESS;
}
}  // namespace

Status BuildUbMemTransferDescs(bool is_get, const HixlOneSideOpDesc *src, uint32_t list_num,
                               std::vector<UbMemAicpuTransferDesc> &dst) {
  HIXL_CHECK_NOTNULL(src);
  HIXL_CHK_BOOL_RET_STATUS(list_num > 0U, PARAM_INVALID, "[UbMemAicpuParam] list_num must be > 0");
  dst.clear();
  dst.reserve(list_num);
  for (uint32_t i = 0U; i < list_num; ++i) {
    const uint64_t local_addr = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(src[i].local_buf));
    const uint64_t remote_addr = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(src[i].remote_buf));
    const uint64_t src_addr = is_get ? remote_addr : local_addr;
    const uint64_t dst_addr = is_get ? local_addr : remote_addr;
    HIXL_CHK_STATUS_RET(AppendSplitDescs(src_addr, dst_addr, src[i].len, dst),
                        "[UbMemAicpuParam] Split descriptor failed, index:%u", i);
  }
  return SUCCESS;
}

Status FillUbMemKernelParam(const TransferPool::SlotHandle &slot, const void *desc_buf, uint32_t chunk_offset,
                            uint32_t chunk_count, bool is_get, bool emit_notify, uint32_t timeout_ms,
                            UbMemAicpuKernelParam &param) {
  HIXL_CHECK_NOTNULL(desc_buf);
  HIXL_CHK_BOOL_RET_STATUS(chunk_count > 0U, PARAM_INVALID, "[UbMemAicpuParam] chunk_count must be > 0");
  HIXL_CHK_BOOL_RET_STATUS(slot.thread != 0U, PARAM_INVALID,
                           "[UbMemAicpuParam] transfer context is not registered, slot:%u", slot.slot_index);
  param = UbMemAicpuKernelParam{};
  auto *base = static_cast<const uint8_t *>(desc_buf);
  param.desc_addr =
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(base + chunk_offset * sizeof(UbMemAicpuTransferDesc)));
  param.desc_count = chunk_count;
  param.direction =
      static_cast<uint32_t>(is_get ? UbMemAicpuTransferDirection::kRead : UbMemAicpuTransferDirection::kWrite);
  param.timeout_ms = timeout_ms;
  param.notify_id = slot.notify_id;
  param.emit_notify_record = emit_notify ? 1U : 0U;
  param.transfer_ctx_key = static_cast<uint64_t>(slot.thread);
  param.version = kUbMemKernelParamVersion;
  const uint32_t rtsq_task_count = chunk_count + (emit_notify ? 1U : 0U);
  HIXL_CHK_STATUS_RET(FillRtsqMeta(slot.ubmem_stream, rtsq_task_count, slot.device_id, param),
                      "[UbMemAicpuParam] Fill RTSQ metadata failed, slot:%u", slot.slot_index);
  return SUCCESS;
}

}  // namespace hixl
