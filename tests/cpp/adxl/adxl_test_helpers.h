/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef HIXL_TESTS_CPP_ADXL_ADXL_TEST_HELPERS_H
#define HIXL_TESTS_CPP_ADXL_ADXL_TEST_HELPERS_H

#include <cstdint>
#include <vector>

#include <atomic>
#include <chrono>
#include <thread>

#include <gtest/gtest.h>

#include "adxl/adxl_engine.h"

namespace adxl {
namespace test_helpers {

inline void RegisterDeviceBufferMem(AdxlEngine &engine, const std::vector<int8_t> &buffer, MemHandle &handle) {
  adxl::MemDesc mem{};
  mem.addr = reinterpret_cast<uintptr_t>(buffer.data());
  mem.len = buffer.size();
  EXPECT_EQ(engine.RegisterMem(mem, MEM_DEVICE, handle), SUCCESS);
}

inline void WaitForAllAsyncTransfers(AdxlEngine &engine, TransferReq *req_list, int req_count,
                                     int poll_interval_ms = 10, int max_wait_sec = 5) {
  std::vector<std::thread> poll_threads;
  std::atomic<int> completed{0};
  std::atomic<bool> stop{false};
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(max_wait_sec);
  for (int i = 0; i < req_count; ++i) {
    poll_threads.emplace_back([&, i]() {
      TransferStatus status = TransferStatus::WAITING;
      while (!stop.load() && status == TransferStatus::WAITING) {
        engine.GetTransferStatus(req_list[i], status);
        if (status == TransferStatus::COMPLETED) {
          completed.fetch_add(1);
          return;
        }
        if (status == TransferStatus::FAILED) {
          return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(poll_interval_ms));
      }
    });
  }
  while (std::chrono::steady_clock::now() < deadline && completed.load() < req_count) {
    std::this_thread::sleep_for(std::chrono::milliseconds(poll_interval_ms));
  }
  stop = true;
  for (auto &t : poll_threads) {
    if (t.joinable()) {
      t.join();
    }
  }
  EXPECT_EQ(completed.load(), req_count);
}

}  // namespace test_helpers
}  // namespace adxl

#endif  // HIXL_TESTS_CPP_ADXL_ADXL_TEST_HELPERS_H
