/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_OPS_HIXL_KERNEL_UBMEM_STARS_SDMA_H_
#define CANN_HIXL_SRC_OPS_HIXL_KERNEL_UBMEM_STARS_SDMA_H_

#include <cstdint>

#include "cs/ubmem/ubmem_aicpu_types.h"
#include "ubmem_a3_rtsq.h"

namespace hixl {

// Layout-compatible with driver drvResIdKey. Do not include ascend_hal_base.h here:
// it defines MEM_HOST as a macro and breaks hixl::MemType in hixl_types.h.
struct UbMemDrvResIdKey {
  uint32_t ruDevId = 0U;
  uint32_t tsId = 0U;
  uint32_t resType = 0U;
  uint32_t resId = 0U;
  uint32_t flag = 0U;
  uint32_t rsv[3]{};
};

struct UbMemRtsqState {
  uint32_t device_id = 0U;
  uint32_t sq_id = 0U;
  uint32_t stream_id = 0U;
  uint32_t logic_cq_id = 0U;
  uint32_t next_task_id = 0U;
  uint64_t base_addr = 0U;
  uint32_t depth = 0U;
  uint32_t head = 0U;
  uint32_t tail = 0U;
};

// Leading fields of driver trs_logic_cqe / runtime rtLogicCqReport_t for logging.
struct __attribute__((packed)) UbMemLogicCqeView {
  uint16_t stream_id = 0U;
  uint16_t task_id = 0U;
  uint32_t error_code = 0U;
  uint8_t error_type = 0U;
  uint8_t sqe_type = 0U;
  uint16_t sq_id = 0U;
  uint16_t sq_head = 0U;
};

union UbMemRtsqEntry {
  UbMemA3SdmaSqe sdma;
  UbMemA3NotifySqe notify;
  uint8_t bytes[kUbMemA3RtsqEntryBytes];
};

// Capacity must match kMaxRtsqEntriesPerPublish in ubmem_stars_sdma.cc.
struct UbMemRtsqBatch {
  UbMemRtsqEntry entries[128U]{};
  uint32_t count = 0U;
};

enum class UbMemLogicCqRecvResult { kEmpty, kReports, kError };

// HIXL-owned A3 RTSQ submission wrapper. It mirrors HComm's A3 SDMA SQE
// construction while consuming only UbMem VMM addresses and a HIXL-owned
// worker RTSQ passed by the dispatcher.
class UbMemStarsSdma {
 public:
  static uint32_t Submit(const UbMemAicpuKernelParam &param, const UbMemAicpuTransferDesc *descs);

 private:
  static bool ValidateSubmitArgs(const UbMemAicpuKernelParam &param, const UbMemAicpuTransferDesc *descs);
  static bool BuildSubmitDeadline(uint64_t timeout_ms, uint64_t &deadline);
  static bool AppendAndPublishSdmaTasks(const UbMemAicpuTransferDesc *descs, uint32_t desc_count, UbMemRtsqState &state,
                                        UbMemRtsqBatch &batch, uint64_t deadline);
  static bool EmitNotifyRecord(uint32_t notify_id, UbMemRtsqState &state, UbMemRtsqBatch &batch, uint64_t deadline);

  static bool IsValidDescriptor(const UbMemAicpuTransferDesc &desc);
  static bool AreValidDescriptors(const UbMemAicpuTransferDesc *descs, uint32_t desc_count);
  static bool RestoreRtsqStream(const UbMemRtsqState &state);
  static bool ResolveLocalDeviceId(uint32_t host_device_id, uint32_t &local_device_id);
  static uint64_t MonotonicNs();
  static bool DeadlineExceeded(uint64_t deadline_ns);
  static bool IsExceptionLogicCqe(const UbMemLogicCqeView &report);
  static void LogLogicCqe(const UbMemRtsqState &state, const UbMemLogicCqeView &report, uint32_t idx,
                          uint32_t report_count, uint32_t total_reports, bool is_exception);
  static bool InspectLogicCqReports(const UbMemRtsqState &state, const uint8_t *reports, uint32_t report_count,
                                    uint32_t total_reports);
  static UbMemLogicCqRecvResult RecvLogicCqBatch(const UbMemRtsqState &state, uint8_t *reports, uint32_t &report_count);
  static bool PollLogicCqUntilEmpty(const UbMemRtsqState &state, uint64_t deadline);
  static bool LoadRtsqQueueState(UbMemRtsqState &state);
  static bool InitializeRtsq(const UbMemAicpuKernelParam &param, UbMemRtsqState &state);
  static bool HasRtsqCapacity(const UbMemRtsqState &state, uint32_t count);
  static bool EnsureRtsqCapacity(UbMemRtsqState &state, uint32_t count, uint64_t deadline);
  static bool BuildSdmaSqe(uint64_t source, uint64_t destination, uint64_t size, uint32_t task_id,
                           const UbMemRtsqState &state, UbMemA3SdmaSqe &sqe);
  static bool BuildNotifySqe(uint32_t notify_id, uint32_t task_id, const UbMemRtsqState &state, UbMemA3NotifySqe &sqe);
  static bool CopySqeBatchToRing(const UbMemRtsqState &state, const UbMemRtsqBatch &batch);
  static bool CommitRtsqTail(UbMemRtsqState &state, uint32_t new_tail, uint32_t batch_count);
  static bool PublishRtsqBatch(UbMemRtsqState &state, UbMemRtsqBatch &batch, uint64_t deadline);
  static bool AppendDescriptorTasks(const UbMemAicpuTransferDesc &desc, UbMemRtsqState &state, UbMemRtsqBatch &batch,
                                    uint64_t deadline);
  static bool AppendNotifyTask(uint32_t notify_id, UbMemRtsqState &state, UbMemRtsqBatch &batch, uint64_t deadline);
};

}  // namespace hixl

#endif  // CANN_HIXL_SRC_OPS_HIXL_KERNEL_UBMEM_STARS_SDMA_H_
