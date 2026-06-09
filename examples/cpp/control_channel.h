/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef HIXL_EXAMPLES_CPP_CONTROL_CHANNEL_H
#define HIXL_EXAMPLES_CPP_CONTROL_CHANNEL_H

#include <arpa/inet.h>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <netinet/in.h>
#include <string>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

namespace sample {
constexpr int32_t kDefaultControlTimeoutSec = 60;
constexpr int32_t kDefaultControlConnectRetryIntervalUs = 100000;

inline void CloseFd(int &fd) {
  if (fd >= 0) {
    (void)close(fd);
    fd = -1;
  }
}

inline int BuildSocketAddress(const char *ip, uint16_t port, const char *desc, sockaddr_in &addr) {
  addr = {};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  if (inet_pton(AF_INET, ip, &addr.sin_addr) != 1) {
    printf("[ERROR] Invalid %s ip: %s\n", desc, ip);
    return -1;
  }
  return 0;
}

inline int StartControlServer(const std::string &local_ip, uint16_t listen_port, const char *role_name,
                              int32_t socket_backlog) {
  int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (listen_fd < 0) {
    printf("[ERROR] Create %s control socket failed, errno = %d\n", role_name, errno);
    return -1;
  }

  const int reuse = 1;
  if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) != 0) {
    printf("[ERROR] Set %s control socket option failed, errno = %d\n", role_name, errno);
    CloseFd(listen_fd);
    return -1;
  }

  sockaddr_in listen_addr{};
  const std::string desc = std::string(role_name) + " control listen";
  if (BuildSocketAddress(local_ip.c_str(), listen_port, desc.c_str(), listen_addr) != 0) {
    CloseFd(listen_fd);
    return -1;
  }
  if (bind(listen_fd, reinterpret_cast<sockaddr *>(&listen_addr), sizeof(listen_addr)) != 0) {
    printf("[ERROR] Bind %s control socket failed, ip = %s, port = %u, errno = %d\n", role_name, local_ip.c_str(),
           static_cast<unsigned int>(listen_port), errno);
    CloseFd(listen_fd);
    return -1;
  }
  if (listen(listen_fd, socket_backlog) != 0) {
    printf("[ERROR] Listen %s control socket failed, errno = %d\n", role_name, errno);
    CloseFd(listen_fd);
    return -1;
  }

  printf("[INFO] %s control server listen on %s:%u\n", role_name, local_ip.c_str(),
         static_cast<unsigned int>(listen_port));
  return listen_fd;
}

inline int WaitReadable(int listen_fd, const char *message_desc, int32_t timeout_sec) {
  while (true) {
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(listen_fd, &read_fds);
    timeval timeout{};
    timeout.tv_sec = timeout_sec;
    const int select_ret = select(listen_fd + 1, &read_fds, nullptr, nullptr, &timeout);
    if (select_ret < 0 && errno == EINTR) {
      continue;
    }
    if (select_ret == 0) {
      printf("[ERROR] Wait %s timeout, timeout = %d seconds\n", message_desc, timeout_sec);
      return -1;
    }
    if (select_ret < 0) {
      printf("[ERROR] Wait %s failed, errno = %d\n", message_desc, errno);
      return -1;
    }
    return 0;
  }
}

inline int ReceiveExpectedMessage(int conn_fd, const char *expected_message, const char *message_desc) {
  const size_t expected_len = std::strlen(expected_message);
  std::vector<char> buffer(expected_len + 1U, 0);
  size_t received_len = 0U;
  while (received_len < expected_len) {
    const ssize_t recv_len = recv(conn_fd, buffer.data() + received_len, expected_len - received_len, 0);
    if (recv_len < 0 && errno == EINTR) {
      continue;
    }
    if (recv_len <= 0) {
      printf("[ERROR] Receive %s failed, errno = %d\n", message_desc, errno);
      return -1;
    }
    received_len += static_cast<size_t>(recv_len);
  }

  if (std::string(buffer.data(), received_len) != expected_message) {
    printf("[ERROR] Receive invalid %s, message = %s, expected = %s\n", message_desc, buffer.data(), expected_message);
    return -1;
  }
  printf("[INFO] Receive %s success\n", message_desc);
  return 0;
}

inline int WaitControlMessage(int listen_fd, const char *expected_message, const char *message_desc,
                              int32_t timeout_sec = kDefaultControlTimeoutSec) {
  if (WaitReadable(listen_fd, message_desc, timeout_sec) != 0) {
    return -1;
  }

  sockaddr_in peer_addr{};
  socklen_t peer_addr_len = sizeof(peer_addr);
  int conn_fd = accept(listen_fd, reinterpret_cast<sockaddr *>(&peer_addr), &peer_addr_len);
  if (conn_fd < 0) {
    printf("[ERROR] Accept %s connection failed, errno = %d\n", message_desc, errno);
    return -1;
  }

  const int ret = ReceiveExpectedMessage(conn_fd, expected_message, message_desc);
  CloseFd(conn_fd);
  return ret;
}

inline int ConnectControlServer(const sockaddr_in &remote_addr, const char *remote_ip, uint16_t remote_port,
                                const char *message_desc, int32_t timeout_sec, int32_t retry_interval_us) {
  int conn_fd = -1;
  constexpr int32_t kUsecPerSec = 1000000;
  const int32_t retry_times = timeout_sec * kUsecPerSec / retry_interval_us;
  for (int32_t i = 0; i < retry_times; ++i) {
    conn_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (conn_fd < 0) {
      printf("[ERROR] Create socket for %s failed, errno = %d\n", message_desc, errno);
      return -1;
    }
    if (connect(conn_fd, reinterpret_cast<const sockaddr *>(&remote_addr), sizeof(remote_addr)) == 0) {
      return conn_fd;
    }
    CloseFd(conn_fd);
    usleep(retry_interval_us);
  }

  printf("[ERROR] Connect control server timeout for %s, remote = %s:%u, timeout = %d seconds\n", message_desc,
         remote_ip, static_cast<unsigned int>(remote_port), timeout_sec);
  return -1;
}

inline int SendAll(int conn_fd, const char *message, const char *message_desc) {
  const size_t message_len = std::strlen(message);
  size_t sent_len = 0U;
  while (sent_len < message_len) {
    const ssize_t ret = send(conn_fd, message + sent_len, message_len - sent_len, 0);
    if (ret < 0 && errno == EINTR) {
      continue;
    }
    if (ret <= 0) {
      printf("[ERROR] Send %s failed, errno = %d\n", message_desc, errno);
      return -1;
    }
    sent_len += static_cast<size_t>(ret);
  }
  printf("[INFO] Send %s success\n", message_desc);
  return 0;
}

inline int SendControlMessage(const char *remote_ip, uint16_t remote_port, const char *peer_name, const char *message,
                              const char *message_desc, int32_t timeout_sec = kDefaultControlTimeoutSec,
                              int32_t retry_interval_us = kDefaultControlConnectRetryIntervalUs) {
  sockaddr_in remote_addr{};
  const std::string desc = std::string(peer_name) + " control";
  if (BuildSocketAddress(remote_ip, remote_port, desc.c_str(), remote_addr) != 0) {
    return -1;
  }

  int conn_fd = ConnectControlServer(remote_addr, remote_ip, remote_port, message_desc, timeout_sec, retry_interval_us);
  if (conn_fd < 0) {
    return -1;
  }

  const int ret = SendAll(conn_fd, message, message_desc);
  CloseFd(conn_fd);
  return ret;
}
}  // namespace sample

#endif  // HIXL_EXAMPLES_CPP_CONTROL_CHANNEL_H
