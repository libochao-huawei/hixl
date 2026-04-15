/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef HIXL_CLIENT_RUNNER_H
#define HIXL_CLIENT_RUNNER_H

#include <memory>
#include <thread>
#include <vector>

#include "benchmark_config.h"
#include "hixl/hixl.h"
#include "tcp_client_server.h"

namespace hixl_benchmark {

namespace detail {

/// Per-lane client state (multi local engine). Owned by `ClientRunner` until `Shutdown` / post-join cleanup.
struct LaneState {
  hixl::Hixl hixl;
  void *buffer = nullptr;
  hixl::MemHandle mem_handle = nullptr;
  bool is_host = false;
  bool need_register = false;
  bool hixl_initialized = false;
  bool hixl_connected = false;
  TCPClient tcp_client;
};

}  // namespace detail

/// Client benchmark: `Init()` 绑定 device（多 lane 拓扑除外）；`Run()` 执行业务；`Shutdown()` 统一回收并复位 device。
class ClientRunner {
 public:
  explicit ClientRunner(const BenchmarkConfig &cfg) : cfg_(cfg) {}
  ~ClientRunner();

  ClientRunner(const ClientRunner &) = delete;
  ClientRunner &operator=(const ClientRunner &) = delete;

  bool Init();
  int Run();
  void Shutdown();

 private:
  const BenchmarkConfig &cfg_;
  bool device_bound_ = false;
  int32_t device_id_ = 0;

  hixl::Hixl lane_hixl_;
  void *lane_buffer_ = nullptr;
  hixl::MemHandle lane_mem_handle_ = nullptr;
  bool lane_is_host_ = false;
  bool lane_need_register_ = false;
  bool lane_hixl_initialized_ = false;
  bool lane_hixl_connected_ = false;
  bool lane_resources_active_ = false;

  TCPClient lane_tcp_;
  bool lane_tcp_handshake_ok_ = false;
  bool lane_need_tcp_notify_ = false;

  std::vector<std::unique_ptr<detail::LaneState>> lane_runtimes_;
  std::vector<std::thread> multi_lane_threads_;

  void ReleaseLaneResources();
  void ReleaseAllLaneRuntimes();
  int RunOnePair(const std::string &remote, void *src_slice, size_t register_len);
  int RunSingleDevice();
  int RunSharedMultiRemote();
  int RunMultiLane();
  int RunClientSharedRemoteWorkers();
  int RunClientLaneWorkers();
};

}  // namespace hixl_benchmark

#endif  // HIXL_CLIENT_RUNNER_H
