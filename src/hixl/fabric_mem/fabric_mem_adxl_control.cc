/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "fabric_mem/fabric_mem_adxl_control.h"

#include <chrono>
#include <cerrno>
#include <cstring>
#include <poll.h>
#include <sys/socket.h>

#include "common/hixl_checker.h"
#include "common/hixl_log.h"
#include "nlohmann/json.hpp"

namespace hixl {
namespace {
constexpr int64_t kMicrosPerMillis = 1000;
constexpr int32_t kHeartBeatMsgType = static_cast<int32_t>(FabricMemAdxlMsgType::kHeartBeat);

int64_t RemainingMicros(const std::chrono::steady_clock::time_point &start, uint64_t timeout_us) {
  const auto elapsed =
      std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
  return static_cast<int64_t>(timeout_us) - elapsed;
}

Status PollForWriteReady(int32_t fd, int64_t remaining_us) {
  if (remaining_us <= 0) {
    return TIMEOUT;
  }
  pollfd pfd{};
  pfd.fd = fd;
  pfd.events = POLLOUT;
  const int poll_timeout_ms = static_cast<int>(remaining_us / kMicrosPerMillis);
  const int poll_ret = poll(&pfd, 1, poll_timeout_ms);
  if (poll_ret == 0) {
    return TIMEOUT;
  }
  if (poll_ret < 0) {
    if (errno == EINTR) {
      return SUCCESS;
    }
    HIXL_LOGE(FAILED, "Socket poll failed, errno:%d, msg:%s.", errno, strerror(errno));
    return FAILED;
  }
  return SUCCESS;
}

Status WriteWithTimeout(int32_t fd, const void *buf, size_t len, uint64_t timeout_us,
                        std::chrono::steady_clock::time_point &start) {
  const auto *data = static_cast<const char *>(buf);
  size_t written = 0U;
  while (written < len) {
    const ssize_t ret = send(fd, data + written, len - written, MSG_NOSIGNAL);
    if (ret < 0) {
      if (errno == EINTR) {
        continue;
      }
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        HIXL_CHK_STATUS_RET(PollForWriteReady(fd, RemainingMicros(start, timeout_us)), "Socket poll failed.");
        continue;
      }
      HIXL_LOGE(FAILED, "Socket send failed, errno:%d, msg:%s.", errno, strerror(errno));
      return FAILED;
    }
    written += static_cast<size_t>(ret);
  }
  return SUCCESS;
}

Status SendMsgByProtocol(int32_t fd, int32_t msg_type, const std::string &msg_str, uint64_t timeout_us) {
  auto start = std::chrono::steady_clock::now();
  const uint64_t body_size = static_cast<uint64_t>(sizeof(msg_type)) + msg_str.size();
  FabricMemAdxlProtocolHeader header{kFabricMemAdxlMagic, body_size};
  HIXL_CHK_STATUS_RET(WriteWithTimeout(fd, &header, sizeof(header), timeout_us, start), "Failed to write header.");
  HIXL_CHK_STATUS_RET(WriteWithTimeout(fd, &msg_type, sizeof(msg_type), timeout_us, start),
                      "Failed to write msg type.");
  if (!msg_str.empty()) {
    HIXL_CHK_STATUS_RET(WriteWithTimeout(fd, msg_str.data(), msg_str.size(), timeout_us, start),
                        "Failed to write msg body.");
  }
  return SUCCESS;
}
}  // namespace

Status FabricMemAdxlControl::SendHeartBeat(int32_t fd, uint64_t timeout_us) {
  FabricMemAdxlHeartBeatMsg msg{};
  msg.msg = 'H';
  std::string payload;
  try {
    nlohmann::json j = msg;
    payload = j.dump();
  } catch (const nlohmann::json::exception &e) {
    HIXL_LOGE(PARAM_INVALID, "Failed to serialize heartbeat msg:%s.", e.what());
    return PARAM_INVALID;
  }
  return SendMsgByProtocol(fd, kHeartBeatMsgType, payload, timeout_us);
}

Status FabricMemAdxlControl::SendMsg(int32_t fd, FabricMemAdxlMsgType msg_type, const std::string &payload,
                                     uint64_t timeout_us) {
  return SendMsgByProtocol(fd, static_cast<int32_t>(msg_type), payload, timeout_us);
}
}  // namespace hixl
