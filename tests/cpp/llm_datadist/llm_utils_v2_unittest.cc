/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <atomic>
#include <chrono>
#include <cerrno>
#include <cstdlib>
#include <gtest/gtest.h>
#include <fcntl.h>
#include <future>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include "common/llm_utils.h"
#include "common/msg_handler_plugin.h"
#include "common/llm_checker.h"
#include "common/hixl_utils.h"
#include "llm_datadist/llm_engine_types.h"

using namespace std;
using namespace ::testing;

namespace llm {
namespace {
bool WaitForChildSuccess(const pid_t child) {
  int status = 0;
  for (int32_t i = 0; i < 1000; ++i) {
    const pid_t wait_ret = waitpid(child, &status, WNOHANG);
    if (wait_ret == child) {
      return WIFEXITED(status) && WEXITSTATUS(status) == EXIT_SUCCESS;
    }
    if (wait_ret < 0) {
      return false;
    }
    usleep(5000);
  }
  (void)kill(child, SIGKILL);
  (void)waitpid(child, &status, 0);
  return false;
}

bool SetNonBlocking(const int fd) {
  const int flags = fcntl(fd, F_GETFL, 0);
  return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

bool FillSocket(const int fd) {
  char data[4096] = {};
  while (true) {
    const ssize_t write_ret = write(fd, data, sizeof(data));
    if (write_ret > 0) {
      continue;
    }
    if (write_ret < 0 && errno == EINTR) {
      continue;
    }
    return write_ret < 0 && (errno == EAGAIN || errno == EWOULDBLOCK);
  }
}

void NoopSignalHandler(const int) {}

class ScopedSignalHandler {
 public:
  explicit ScopedSignalHandler(const int signal_number) : signal_number_(signal_number) {
    struct sigaction action {};
    action.sa_handler = NoopSignalHandler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    installed_ = sigaction(signal_number_, &action, &old_action_) == 0;
  }

  ~ScopedSignalHandler() {
    if (installed_) {
      (void)sigaction(signal_number_, &old_action_, nullptr);
    }
  }

  bool IsInstalled() const {
    return installed_;
  }

 private:
  int signal_number_;
  struct sigaction old_action_ {};
  bool installed_{false};
};
}  // namespace

class LLMUtilsTest : public ::testing::Test {
 protected:
  void SetUp() override {}
  // 在测试类中进行清理工作，如果需要的话
  void TearDown() override {}
};

TEST_F(LLMUtilsTest, CalcTensorMemSize) {
  int64_t mem_size = -1;
  EXPECT_EQ(LLMUtils::CalcTensorMemSize({1}, ge::DT_INT32, mem_size), ge::SUCCESS);
  EXPECT_EQ(mem_size, 4);
  EXPECT_EQ(LLMUtils::CalcTensorMemSize({1}, ge::DT_STRING, mem_size), ge::SUCCESS);
  EXPECT_EQ(mem_size, 16);
  EXPECT_EQ(LLMUtils::CalcTensorMemSize({1}, ge::DT_INT4, mem_size), ge::SUCCESS);
  EXPECT_EQ(mem_size, 1);
  EXPECT_EQ(LLMUtils::CalcTensorMemSize({3}, ge::DT_INT4, mem_size), ge::SUCCESS);
  EXPECT_EQ(mem_size, 2);
  EXPECT_EQ(LLMUtils::CalcTensorMemSize({3}, ge::DT_UNDEFINED, mem_size), ge::LLM_PARAM_INVALID);
}

TEST_F(LLMUtilsTest, GetSizeInBytes_BitPackedCountBeyondInt32) {
  const int64_t element_count = static_cast<int64_t>(std::numeric_limits<int32_t>::max() / 4) + 1;
  const int64_t expected_size = (element_count * 4 + ge::kBitNumOfOneByte - 1) / ge::kBitNumOfOneByte;
  int64_t mem_size = -1;

  EXPECT_EQ(LLMUtils::GetSizeInBytes(element_count, ge::DT_INT4, mem_size), ge::SUCCESS);
  EXPECT_EQ(mem_size, expected_size);
}

TEST_F(LLMUtilsTest, SplitSuccess) {
  auto ret = hixl::Split("", ',');
  EXPECT_EQ(ret.size(), 1);
  EXPECT_EQ(ret[0], "");
  ret = hixl::Split("abcd", 'b');
  EXPECT_EQ(ret.size(), 2);
  EXPECT_EQ(ret[0], "a");
  EXPECT_EQ(ret[1], "cd");
  ret = hixl::Split("abcd", 'd');
  EXPECT_EQ(ret.size(), 2);
  EXPECT_EQ(ret[0], "abc");
  EXPECT_EQ(ret[1], "");
}

TEST_F(LLMUtilsTest, CheckMultiplyOverflowInt64) {
  // Test case 1: a > 0, b > 0, no overflow
  EXPECT_FALSE(LLMUtils::CheckMultiplyOverflowInt64(100, 200));
  // Test case 2: a > 0, b > 0, overflow
  EXPECT_TRUE(LLMUtils::CheckMultiplyOverflowInt64(std::numeric_limits<int64_t>::max() / 2 + 1, 2));
  // Test case 3: a > 0, b < 0, no overflow
  EXPECT_FALSE(LLMUtils::CheckMultiplyOverflowInt64(100, -200));
  // Test case 4: a > 0, b < 0, overflow
  EXPECT_TRUE(LLMUtils::CheckMultiplyOverflowInt64(std::numeric_limits<int64_t>::max(), -2));
  // Test case 5: a < 0, b > 0, no overflow
  EXPECT_FALSE(LLMUtils::CheckMultiplyOverflowInt64(-100, 200));
  // Test case 6: a < 0, b > 0, overflow
  EXPECT_TRUE(LLMUtils::CheckMultiplyOverflowInt64(std::numeric_limits<int64_t>::min(), 2));
  // Test case 7: a < 0, b < 0, no overflow
  EXPECT_FALSE(LLMUtils::CheckMultiplyOverflowInt64(-100, -200));
  // Test case 8: a < 0, b < 0, overflow (a != 0, b < max/a)
  EXPECT_TRUE(LLMUtils::CheckMultiplyOverflowInt64(std::numeric_limits<int64_t>::min(), -1));
  // Test case 9: a == 0 (special case in last branch)
  EXPECT_FALSE(LLMUtils::CheckMultiplyOverflowInt64(0, std::numeric_limits<int64_t>::min()));
}

TEST_F(LLMUtilsTest, TestParserOptions) {
  // 构建选项参数
  const std::map<ge::AscendString, ge::AscendString> llm_options = {
      {"ge.socVersion", "Ascend910B1"},
      {"ge.graphRunMode", "0"},
      {llm::LLM_OPTION_ROLE, "decoder"},
      {"ge.distributed_cluster_build", "1"},
      {"RESOURCE_CONFIG_PATH", "/tmp/numa_config_path"},
      {llm::LLM_OPTION_SYNC_KV_CACHE_WAIT_TIME, "20"},
  };

  DecoderWaitTimeInfo wait_time_info;
  ge::Status ret;
  ret = LLMUtils::ParserWaitTimeInfo(llm_options, wait_time_info);
  EXPECT_EQ(ret, ge::SUCCESS);

  std::vector<int32_t> device_ids;
  ret = LLMUtils::ParseDeviceId(llm_options, device_ids, ge::OPTION_EXEC_DEVICE_ID);
  EXPECT_EQ(ret, ge::SUCCESS);
}

TEST_F(LLMUtilsTest, TestMsgHandlerPlugin) {
  int32_t fd = -1;
  char data[1];
  size_t len = 1;
  ssize_t s = 0;
  s = MsgHandlerPlugin::Read(fd, data, len);
  EXPECT_TRUE(s < 0);

  s = MsgHandlerPlugin::Write(fd, data, len);
  EXPECT_TRUE(s < 0);
}

TEST_F(LLMUtilsTest, ConnectFailureLeavesInvalidFd) {
  const int32_t bound_fd = socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_GE(bound_fd, 0);
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = 0;
  ASSERT_EQ(bind(bound_fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)), 0);
  socklen_t addr_len = sizeof(addr);
  ASSERT_EQ(getsockname(bound_fd, reinterpret_cast<sockaddr *>(&addr), &addr_len), 0);

  int32_t conn_fd = bound_fd;
  EXPECT_EQ(MsgHandlerPlugin::Connect("127.0.0.1", ntohs(addr.sin_port), conn_fd, 1000, ge::SUCCESS), ge::SUCCESS);
  EXPECT_EQ(conn_fd, -1);
  close(bound_fd);
}

TEST_F(LLMUtilsTest, TestMsgHandlerPluginReadEOF) {
  int fds[2];
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);

  char write_data[2] = {'a', 'b'};
  ASSERT_EQ(write(fds[0], write_data, sizeof(write_data)), static_cast<ssize_t>(sizeof(write_data)));
  close(fds[0]);

  char read_buf[10];
  ssize_t ret = MsgHandlerPlugin::Read(fds[1], read_buf, sizeof(read_buf));
  EXPECT_EQ(ret, static_cast<ssize_t>(sizeof(write_data)));

  close(fds[1]);
}

TEST_F(LLMUtilsTest, MsgHandlerReadReturnsOnEagain) {
  int fds[2];
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
  ASSERT_TRUE(SetNonBlocking(fds[1]));

  const pid_t child = fork();
  ASSERT_GE(child, 0);
  if (child == 0) {
    char data = 0;
    _exit(MsgHandlerPlugin::Read(fds[1], &data, sizeof(data)) < 0 ? EXIT_SUCCESS : EXIT_FAILURE);
  }

  EXPECT_TRUE(WaitForChildSuccess(child));
  close(fds[0]);
  close(fds[1]);
}

TEST_F(LLMUtilsTest, MsgHandlerWriteReturnsOnEagain) {
  int fds[2];
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
  ASSERT_TRUE(SetNonBlocking(fds[0]));
  ASSERT_TRUE(FillSocket(fds[0]));

  const pid_t child = fork();
  ASSERT_GE(child, 0);
  if (child == 0) {
    const char data = 0;
    _exit(MsgHandlerPlugin::Write(fds[0], &data, sizeof(data)) < 0 ? EXIT_SUCCESS : EXIT_FAILURE);
  }

  EXPECT_TRUE(WaitForChildSuccess(child));
  close(fds[0]);
  close(fds[1]);
}

TEST_F(LLMUtilsTest, MsgHandlerReadRetriesOnEintr) {
  int fds[2];
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
  ScopedSignalHandler signal_handler(SIGUSR1);
  ASSERT_TRUE(signal_handler.IsInstalled());

  const pthread_t test_thread = pthread_self();
  std::promise<void> ready;
  std::atomic<int> signal_ret{-1};
  auto ready_future = ready.get_future();
  std::thread signaler([test_thread, peer_fd = fds[0], future = std::move(ready_future), &signal_ret]() mutable {
    future.wait();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    signal_ret.store(pthread_kill(test_thread, SIGUSR1), std::memory_order_release);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    const char data = 'a';
    (void)write(peer_fd, &data, sizeof(data));
  });

  ready.set_value();
  char data = 0;
  const ssize_t read_ret = MsgHandlerPlugin::Read(fds[1], &data, sizeof(data));
  signaler.join();

  EXPECT_EQ(signal_ret.load(std::memory_order_acquire), 0);
  EXPECT_EQ(read_ret, static_cast<ssize_t>(sizeof(data)));
  EXPECT_EQ(data, 'a');
  close(fds[0]);
  close(fds[1]);
}

TEST_F(LLMUtilsTest, MsgHandlerWriteRetriesOnEintr) {
  int fds[2];
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
  constexpr int kSmallSendBuffer = 4096;
  ASSERT_EQ(setsockopt(fds[0], SOL_SOCKET, SO_SNDBUF, &kSmallSendBuffer, sizeof(kSmallSendBuffer)), 0);
  ASSERT_TRUE(SetNonBlocking(fds[0]));
  ASSERT_TRUE(FillSocket(fds[0]));
  const int flags = fcntl(fds[0], F_GETFL, 0);
  ASSERT_GE(flags, 0);
  ASSERT_EQ(fcntl(fds[0], F_SETFL, flags & ~O_NONBLOCK), 0);

  ScopedSignalHandler signal_handler(SIGUSR1);
  ASSERT_TRUE(signal_handler.IsInstalled());
  const pthread_t test_thread = pthread_self();
  std::promise<void> ready;
  std::atomic<int> signal_ret{-1};
  auto ready_future = ready.get_future();
  std::thread signaler([test_thread, peer_fd = fds[1], future = std::move(ready_future), &signal_ret]() mutable {
    future.wait();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    signal_ret.store(pthread_kill(test_thread, SIGUSR1), std::memory_order_release);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    char data[65536] = {};
    (void)read(peer_fd, data, sizeof(data));
  });

  ready.set_value();
  const char data = 0;
  const ssize_t write_ret = MsgHandlerPlugin::Write(fds[0], &data, sizeof(data));
  signaler.join();

  EXPECT_EQ(signal_ret.load(std::memory_order_acquire), 0);
  EXPECT_EQ(write_ret, static_cast<ssize_t>(sizeof(data)));
  close(fds[0]);
  close(fds[1]);
}

static ge::Status TestLogTooLong() {
  std::string testlog(MSG_LENGTH * 2, 'c');
  LLM_ASSERT_TRUE(false, "TestLogTooLong:%s", testlog.c_str());
  return ge::SUCCESS;
}

TEST_F(LLMUtilsTest, TestLogTooLong) {
  ge::Status ret = TestLogTooLong();
  EXPECT_NE(ret, ge::SUCCESS);
}
}  // namespace llm
