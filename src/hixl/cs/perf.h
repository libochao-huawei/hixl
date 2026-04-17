/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_CS_PERF_H_
#define CANN_HIXL_SRC_HIXL_CS_PERF_H_

#include <chrono>
#include <cinttypes>
#include <cstdint>
#include <string>
#include "common/hixl_log.h"
#include "statistic_manager.h"

namespace hixl {

using PerfClock = std::chrono::steady_clock;

inline uint64_t ElapsedUs(const PerfClock::time_point &start) {
  return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(PerfClock::now() - start).count());
}

inline uint64_t SumTransferBytes(const uint64_t *len_list, uint32_t list_num) {
  if (len_list == nullptr) {
    return 0UL;
  }
  uint64_t total_bytes = 0UL;
  for (uint32_t i = 0U; i < list_num; ++i) {
    total_bytes += len_list[i];
  }
  return total_bytes;
}

inline void LogPerfStage(const char *stage, const std::string &role, const std::string &channel_id, uint64_t cost_us,
                         uint32_t result, int32_t fd = -1, uint64_t channel_handle = 0UL, uint32_t list_num = 0U,
                         uint64_t total_bytes = 0UL, uint32_t timeout_ms = 0U) {
  HIXL_EVENT("HIXL CS PERF stage:%s role:%s channel:%s fd:%d ch:%" PRIu64 " list_num:%u bytes:%" PRIu64
             " timeout_ms:%u cost:%" PRIu64 " us result:%u",
             stage, role.c_str(), channel_id.c_str(), fd, channel_handle, list_num, total_bytes, timeout_ms, cost_us,
             result);
}

inline void RecordPerfStage(const std::string &channel_id, StatisticStage stage, const char *stage_name,
                            const std::string &role, const PerfClock::time_point &start, uint32_t result, int32_t fd = -1,
                            uint64_t channel_handle = 0UL, uint32_t list_num = 0U, uint64_t total_bytes = 0UL,
                            uint32_t timeout_ms = 0U) {
  const uint64_t cost_us = ElapsedUs(start);
  HixlCSStatisticManager::GetInstance().UpdateStageCost(channel_id, stage, cost_us);
  LogPerfStage(stage_name, role, channel_id, cost_us, result, fd, channel_handle, list_num, total_bytes, timeout_ms);
}

}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_CS_PERF_H_
