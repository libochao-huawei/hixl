/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef HIXL_TESTS_CPP_HIXL_ENGINE_HIXL_TEST_HELPERS_H
#define HIXL_TESTS_CPP_HIXL_ENGINE_HIXL_TEST_HELPERS_H

#include <arpa/inet.h>
#include <cerrno>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "hixl/hixl.h"

namespace hixl {
namespace test_helpers {

inline bool CheckIpv6Supported() {
  int fd = socket(AF_INET6, SOCK_STREAM, 0);
  if (fd < 0) {
    return false;
  }
  struct sockaddr_in6 addr {};
  addr.sin6_family = AF_INET6;
  (void)inet_pton(AF_INET6, "::1", &addr.sin6_addr);
  addr.sin6_port = htons(0U);
  bool ok = (connect(fd, (sockaddr *)&addr, sizeof(addr)) != -1 || errno != EADDRNOTAVAIL);
  close(fd);
  return ok;
}

inline void RegisterInt32DeviceMem(Hixl &engine, int32_t &value, MemHandle &handle) {
  MemDesc mem{};
  mem.addr = reinterpret_cast<uintptr_t>(&value);
  mem.len = sizeof(int32_t);
  EXPECT_EQ(engine.RegisterMem(mem, MEM_DEVICE, handle), SUCCESS);
}

template <typename Engine>
inline void WaitForAllAsyncTransfers(Engine &engine, TransferReq *req_list, int req_count, int poll_interval_ms = 10,
                                     int max_wait_sec = 5) {
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
}  // namespace hixl

#endif  // HIXL_TESTS_CPP_HIXL_ENGINE_HIXL_TEST_HELPERS_H
