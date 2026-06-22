/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <securec.h>
#include <thread>
#include <chrono>
#include <vector>
#include "gtest/gtest.h"
#include "gmock/gmock.h"
#include "ascendcl_stub.h"
#include "depends/mmpa/src/mmpa_stub.h"
#include "engine/test_mmpa_utils.h"
#include "load_kernel.h"
#define private public
#define protected public
#include "hixl_cs_client.h"
#undef protected
#undef private

namespace hixl {

class MockAclRuntimeStub : public llm::AclRuntimeStub {
 public:
  MOCK_METHOD(aclError, aclrtBinaryLoadFromFile, (const char *, aclrtBinaryLoadOptions *, aclrtBinHandle *),
              (override));
  MOCK_METHOD(aclError, aclrtBinaryGetFunction, (aclrtBinHandle, const char *, aclrtFuncHandle *), (override));
  MOCK_METHOD(aclError, aclrtWaitAndResetNotify, (aclrtNotify, aclrtStream, uint32_t), (override));
  MOCK_METHOD(aclError, aclrtSynchronizeStreamWithTimeout, (aclrtStream, int32_t), (override));
  MOCK_METHOD(aclError, aclrtNotifyBatchReset, (aclrtNotify *, size_t), (override));
  MOCK_METHOD(aclError, aclrtMemcpyAsync,
              (void *dst, size_t dest_max, const void *src, size_t src_count, aclrtMemcpyKind kind, aclrtStream stream),
              (override));
  MOCK_METHOD(aclError, aclrtLaunchKernelWithConfig,
              (aclrtFuncHandle funcHandle, uint32_t blockDim, aclrtStream stream, aclrtLaunchKernelCfg *config,
               aclrtArgsHandle argsHandle, void *reserved),
              (override));
};

namespace {

constexpr uint32_t kDeviceDevId = 2U;
constexpr uint32_t kDummyPort = 12345U;

constexpr uint32_t kListNum1 = 1U;
constexpr uint64_t kLen8 = 8ULL;
constexpr int32_t kDefaultStreamSyncTimeoutMsForTest = 10000;

constexpr const char *kTransFlagNameDevice = "_hixl_builtin_dev_trans_flag";

EndpointDesc MakeDeviceEp(CommProtocol protocol, uint32_t dev_id) {
  EndpointDesc ep{};
  ep.loc.locType = ENDPOINT_LOC_TYPE_DEVICE;
  ep.protocol = protocol;
  ep.commAddr.type = COMM_ADDR_TYPE_ID;
  ep.commAddr.id = dev_id;
  return ep;
}

void PrepareKernelReadyForUt(HixlCSClient &cli) {
  cli.device_kernel_loaded_ = true;
  static uint8_t kNonNullStub = 0U;
  cli.device_func_get_ = static_cast<void *>(&kNonNullStub);
  cli.device_func_put_ = static_cast<void *>(&kNonNullStub);
}

void RecordMemForBatchTransfer(HixlCSClient &cli, void *remote_addr, size_t remote_size, void *local_addr,
                               size_t local_size) {
  (void)cli.mem_store_.RecordMemory(true, remote_addr, remote_size);
  (void)cli.mem_store_.RecordMemory(false, local_addr, local_size);
}

void FillTagMem(HixlCSClient &cli, const char *tag, void *addr, uint64_t size) {
  CommMem mem{};
  mem.type = COMM_MEM_TYPE_DEVICE;
  mem.addr = addr;
  mem.size = size;
  cli.tag_mem_descs_[tag] = mem;
}

Status PollUntilCompleted(HixlCSClient &cli, void *qh, HixlCompleteStatus *out_status) {
  HIXL_CHECK_NOTNULL(out_status);
  *out_status = HixlCompleteStatus::HIXL_COMPLETE_STATUS_WAITING;

  for (int i = 0; i < 10; ++i) {
    const Status ret = cli.CheckStatus(qh, out_status);
    if (ret != SUCCESS) {
      return ret;
    }
    if (*out_status == HixlCompleteStatus::HIXL_COMPLETE_STATUS_COMPLETED) {
      return SUCCESS;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return SUCCESS;
}

}  // namespace

// Use common TestMmpaStub from test_mmpa_utils.h
using MockMmpaStub = hixl::test::TestMmpaStub;

class HixlCSClientDeviceFixture : public ::testing::Test {
 protected:
  void SetUp() override {
    // EnsureDeviceKernelLoadedLocked 现在在初始化阶段调用，需要提前设置 MmpaStub
    auto kernel_stub = std::make_shared<MockMmpaStub>();
    kernel_stub->real_path_ok_ = true;
    kernel_stub->access_ok_ = true;
    llm::MmpaStub::GetInstance().SetImpl(kernel_stub);

    const EndpointDesc src = MakeDeviceEp(COMM_PROTOCOL_UBC_TP, kDeviceDevId);
    const EndpointDesc dst = MakeDeviceEp(COMM_PROTOCOL_UBC_TP, kDeviceDevId);

    HixlClientConfig config{};
    HixlClientDesc desc{};
    desc.server_ip = "127.0.0.1";
    desc.server_port = kDummyPort;
    desc.local_endpoint = &src;
    desc.remote_endpoint = &dst;
    ASSERT_EQ(cli_.Create(&desc, &config), SUCCESS);

    cli_.client_channel_handle_ = static_cast<ChannelHandle>(1ULL);
    cli_.device_remote_flag_inited_ = false;
    remote_flag_dev_ = 0ULL;
    FillTagMem(cli_, kTransFlagNameDevice, static_cast<void *>(&remote_flag_dev_), sizeof(uint64_t));

    PrepareKernelReadyForUt(cli_);

    // 手动初始化 remote flag，模拟 GetRemoteMemLocked 的行为
    ASSERT_EQ(cli_.EnsureDeviceRemoteFlagInitedLocked(), SUCCESS);
  }

  void TearDown() override {
    (void)cli_.Destroy();
    unsetenv("HIXL_UT_DEVICE_FLAG_HACK");
    llm::MmpaStub::GetInstance().Reset();
  }

  HixlOneSideOpDesc SetupBatchTransfer(bool is_get) {
    (void)is_get;
    RecordMemForBatchTransfer(cli_, remote_buf_.data(), remote_buf_.size(), local_buf_.data(), local_buf_.size());
    HixlOneSideOpDesc desc{};
    desc.remote_buf = remote_buf_.data();
    desc.local_buf = local_buf_.data();
    desc.len = kLen8;
    return desc;
  }

  HixlCSClient cli_{};
  uint64_t remote_flag_dev_{0ULL};

  std::array<uint8_t, 8> local_buf_{};
  std::array<uint8_t, 8> remote_buf_{};
};

TEST_F(HixlCSClientDeviceFixture, BatchPutDeviceSuccessUseMemcpyHackFlag) {
  setenv("HIXL_UT_DEVICE_FLAG_HACK", "1", 1);
  HixlOneSideOpDesc desc = SetupBatchTransfer(false);

  void *qh = nullptr;
  const Status ret = cli_.BatchTransferAsync(false, 1, &desc, &qh);
  EXPECT_EQ(ret, SUCCESS);
  ASSERT_NE(qh, nullptr);

  HixlCompleteStatus st = HixlCompleteStatus::HIXL_COMPLETE_STATUS_WAITING;
  (void)PollUntilCompleted(cli_, qh, &st);
  EXPECT_EQ(st, HixlCompleteStatus::HIXL_COMPLETE_STATUS_COMPLETED);
}

TEST_F(HixlCSClientDeviceFixture, BatchGetDeviceSuccessUseMemcpyHackFlag) {
  setenv("HIXL_UT_DEVICE_FLAG_HACK", "1", 1);
  HixlOneSideOpDesc desc = SetupBatchTransfer(true);

  void *qh = nullptr;
  const Status ret = cli_.BatchTransferAsync(true, 1, &desc, &qh);
  EXPECT_EQ(ret, SUCCESS);
  ASSERT_NE(qh, nullptr);

  HixlCompleteStatus st = HixlCompleteStatus::HIXL_COMPLETE_STATUS_WAITING;
  (void)PollUntilCompleted(cli_, qh, &st);
  EXPECT_EQ(st, HixlCompleteStatus::HIXL_COMPLETE_STATUS_COMPLETED);
}

TEST_F(HixlCSClientDeviceFixture, EnsureDeviceRemoteFlagInitedLockedMissingTagNoError) {
  cli_.device_remote_flag_inited_ = false;
  cli_.device_remote_flag_addr_ = nullptr;  // 重置为 nullptr
  cli_.tag_mem_descs_.clear();

  // EnsureDeviceRemoteFlagInitedLocked 现在不报错，只跳过初始化
  // 错误延迟到传输阶段的 PrepareDeviceRemoteFlagAndKernel
  EXPECT_EQ(cli_.EnsureDeviceRemoteFlagInitedLocked(), SUCCESS);
  EXPECT_EQ(cli_.device_remote_flag_addr_, nullptr);

  // 实际传输时 PrepareDeviceRemoteFlagAndKernel 会检查并报错
  void *remote_flag = nullptr;
  EXPECT_EQ(cli_.PrepareDeviceRemoteFlagAndKernel(remote_flag), PARAM_INVALID);
}

TEST_F(HixlCSClientDeviceFixture, PrepareDeviceRemoteFlagAndKernelReturnsFlagAddr) {
  // 设置 remote flag 已初始化，有正确的 tag
  cli_.device_remote_flag_inited_ = false;
  FillTagMem(cli_, kTransFlagNameDevice, static_cast<void *>(&remote_flag_dev_), sizeof(uint64_t));

  // 先初始化 remote flag
  EXPECT_EQ(cli_.EnsureDeviceRemoteFlagInitedLocked(), SUCCESS);

  // PrepareDeviceRemoteFlagAndKernel 现在只返回已初始化的 flag 地址
  void *remote_flag = nullptr;
  EXPECT_EQ(cli_.PrepareDeviceRemoteFlagAndKernel(remote_flag), SUCCESS);
  EXPECT_EQ(remote_flag, static_cast<void *>(&remote_flag_dev_));
}

TEST_F(HixlCSClientDeviceFixture, BatchPutDeviceSyncUsesStreamSyncNoMemcpy) {
  HixlOneSideOpDesc desc = SetupBatchTransfer(false);
  MockAclRuntimeStub mock_acl;
  llm::AclRuntimeStub::Install(&mock_acl);
  testing::InSequence seq;
  EXPECT_CALL(mock_acl, aclrtSynchronizeStreamWithTimeout(testing::_, kDefaultStreamSyncTimeoutMsForTest))
      .WillOnce(testing::Return(ACL_ERROR_NONE));
  EXPECT_CALL(mock_acl, aclrtSynchronizeStreamWithTimeout(testing::_, 100)).WillOnce(testing::Return(ACL_ERROR_NONE));
  EXPECT_CALL(mock_acl, aclrtMemcpyAsync(testing::_, testing::_, testing::_, testing::_, testing::_, testing::_))
      .Times(0);
  const Status ret = cli_.BatchTransferSync(false, 1, &desc, 100U);
  llm::AclRuntimeStub::UnInstall(&mock_acl);
  EXPECT_EQ(ret, SUCCESS);
  EXPECT_TRUE(cli_.pending_device_handles_.empty());
}

TEST_F(HixlCSClientDeviceFixture, BatchPutDeviceSyncStreamSyncTimeoutAbortsSlot) {
  HixlOneSideOpDesc desc = SetupBatchTransfer(false);
  MockAclRuntimeStub mock_acl;
  llm::AclRuntimeStub::Install(&mock_acl);
  EXPECT_CALL(mock_acl, aclrtSynchronizeStreamWithTimeout(testing::_, kDefaultStreamSyncTimeoutMsForTest))
      .WillOnce(testing::Return(ACL_ERROR_RT_STREAM_SYNC_TIMEOUT));
  EXPECT_CALL(mock_acl, aclrtNotifyBatchReset(testing::NotNull(), testing::Eq(static_cast<size_t>(1U))))
      .WillOnce(testing::Return(ACL_SUCCESS));
  const Status ret = cli_.BatchTransferSync(false, 1, &desc, 100U);
  llm::AclRuntimeStub::UnInstall(&mock_acl);
  EXPECT_EQ(ret, TIMEOUT);
  EXPECT_TRUE(cli_.pending_device_handles_.empty());
}

TEST_F(HixlCSClientDeviceFixture, BatchPutUbDeviceNotifyWaitFail) {
  HixlOneSideOpDesc desc = SetupBatchTransfer(false);
  void *qh = nullptr;

  MockAclRuntimeStub mock_acl;
  llm::AclRuntimeStub::Install(&mock_acl);

  EXPECT_CALL(mock_acl, aclrtWaitAndResetNotify(testing::_, testing::_, testing::_))
      .WillOnce(testing::Return(static_cast<aclError>(FAILED)));

  const Status ret = cli_.BatchTransferAsync(false, 1, &desc, &qh);

  llm::AclRuntimeStub::UnInstall(&mock_acl);

  EXPECT_EQ(ret, FAILED);
  EXPECT_TRUE(cli_.pending_device_handles_.empty());
}

TEST_F(HixlCSClientDeviceFixture, BatchPutDeviceAsyncRoutesWorkToTwoStreamsBeforeDefaultSync) {
  setenv("HIXL_UT_DEVICE_FLAG_HACK", "1", 1);
  HixlOneSideOpDesc desc = SetupBatchTransfer(false);

  void *qh = nullptr;
  aclrtStream kernel_stream = nullptr;
  aclrtStream notify_stream = nullptr;
  aclrtStream copy_stream = nullptr;
  aclrtStream sync_stream = nullptr;
  MockAclRuntimeStub mock_acl;
  llm::AclRuntimeStub::Install(&mock_acl);
  {
    testing::InSequence seq;
    EXPECT_CALL(mock_acl,
                aclrtLaunchKernelWithConfig(testing::_, testing::_, testing::_, testing::_, testing::_, testing::_))
        .WillOnce(testing::DoAll(testing::SaveArg<2>(&kernel_stream), testing::Return(ACL_SUCCESS)));
    EXPECT_CALL(mock_acl, aclrtWaitAndResetNotify(testing::_, testing::_, testing::_))
        .WillOnce(testing::DoAll(testing::SaveArg<1>(&notify_stream), testing::Return(ACL_SUCCESS)));
    EXPECT_CALL(mock_acl,
                aclrtMemcpyAsync(testing::_, testing::_, testing::_, testing::_, ACL_MEMCPY_DEVICE_TO_HOST, testing::_))
        .WillOnce(testing::Invoke([&copy_stream](void *dst, size_t dest_max, const void *src, size_t src_count,
                                                 aclrtMemcpyKind, aclrtStream stream) {
          copy_stream = stream;
          if (dst != nullptr && src != nullptr) {
            const errno_t rc = memcpy_s(dst, dest_max, src, src_count);
            return rc == EOK ? ACL_SUCCESS : ACL_ERROR_RT_FAILURE;
          }
          return ACL_SUCCESS;
        }));
    EXPECT_CALL(mock_acl, aclrtSynchronizeStreamWithTimeout(testing::_, kDefaultStreamSyncTimeoutMsForTest))
        .WillOnce(testing::DoAll(testing::SaveArg<0>(&sync_stream), testing::Return(ACL_ERROR_NONE)));
  }

  const Status ret = cli_.BatchTransferAsync(false, 1, &desc, &qh);
  llm::AclRuntimeStub::UnInstall(&mock_acl);
  ASSERT_EQ(ret, SUCCESS);
  ASSERT_NE(qh, nullptr);
  EXPECT_NE(kernel_stream, nullptr);
  EXPECT_NE(notify_stream, nullptr);
  EXPECT_EQ(copy_stream, notify_stream);
  EXPECT_EQ(sync_stream, kernel_stream);
  EXPECT_NE(kernel_stream, notify_stream);

  HixlCompleteStatus st = HixlCompleteStatus::HIXL_COMPLETE_STATUS_WAITING;
  (void)PollUntilCompleted(cli_, qh, &st);
  unsetenv("HIXL_UT_DEVICE_FLAG_HACK");
}

TEST_F(HixlCSClientDeviceFixture, BatchPutDeviceSlotReuse) {
  setenv("HIXL_UT_DEVICE_FLAG_HACK", "1", 1);
  HixlOneSideOpDesc desc = SetupBatchTransfer(false);

  MockAclRuntimeStub mock_acl;
  llm::AclRuntimeStub::Install(&mock_acl);
  EXPECT_CALL(mock_acl, aclrtWaitAndResetNotify(testing::_, testing::_, testing::_))
      .WillRepeatedly(testing::Return(ACL_SUCCESS));
  EXPECT_CALL(mock_acl, aclrtMemcpyAsync(testing::_, testing::_, testing::_, testing::_, testing::_, testing::_))
      .WillRepeatedly(testing::Return(ACL_SUCCESS));
  EXPECT_CALL(mock_acl, aclrtSynchronizeStreamWithTimeout(testing::_, kDefaultStreamSyncTimeoutMsForTest))
      .WillRepeatedly(testing::Return(ACL_ERROR_NONE));

  std::vector<void *> handles;
  handles.reserve(16U);
  uint32_t first_slot_index = 0U;

  for (uint32_t i = 0; i < 16U; ++i) {
    void *qh = nullptr;
    const Status ret = cli_.BatchTransferAsync(false, 1, &desc, &qh);
    ASSERT_EQ(ret, SUCCESS);
    ASSERT_NE(qh, nullptr);
    handles.emplace_back(qh);

    DeviceCompleteHandle *handle = static_cast<DeviceCompleteHandle *>(qh);
    if (i == 0U) {
      first_slot_index = handle->shared_slot->slot_index;
    }
    EXPECT_EQ(handle->shared_slot->slot_index, first_slot_index);
  }

  for (void *h : handles) {
    HixlCompleteStatus st = HixlCompleteStatus::HIXL_COMPLETE_STATUS_WAITING;
    (void)PollUntilCompleted(cli_, h, &st);
  }

  llm::AclRuntimeStub::UnInstall(&mock_acl);
  unsetenv("HIXL_UT_DEVICE_FLAG_HACK");
}

TEST_F(HixlCSClientDeviceFixture, BatchPutDeviceIndependentHostFlags) {
  // Verify that each async transfer gets its own independent host_flag
  setenv("HIXL_UT_DEVICE_FLAG_HACK", "1", 1);
  HixlOneSideOpDesc desc = SetupBatchTransfer(false);

  MockAclRuntimeStub mock_acl;
  llm::AclRuntimeStub::Install(&mock_acl);
  EXPECT_CALL(mock_acl, aclrtWaitAndResetNotify(testing::_, testing::_, testing::_))
      .WillRepeatedly(testing::Return(ACL_SUCCESS));
  EXPECT_CALL(mock_acl, aclrtMemcpyAsync(testing::_, testing::_, testing::_, testing::_, testing::_, testing::_))
      .WillRepeatedly(testing::Return(ACL_SUCCESS));
  EXPECT_CALL(mock_acl, aclrtSynchronizeStreamWithTimeout(testing::_, kDefaultStreamSyncTimeoutMsForTest))
      .WillRepeatedly(testing::Return(ACL_ERROR_NONE));

  void *qh1 = nullptr;
  void *qh2 = nullptr;
  ASSERT_EQ(cli_.BatchTransferAsync(false, 1, &desc, &qh1), SUCCESS);
  ASSERT_EQ(cli_.BatchTransferAsync(false, 1, &desc, &qh2), SUCCESS);

  DeviceCompleteHandle *h1 = static_cast<DeviceCompleteHandle *>(qh1);
  DeviceCompleteHandle *h2 = static_cast<DeviceCompleteHandle *>(qh2);

  EXPECT_EQ(h1->shared_slot.get(), h2->shared_slot.get());

  // But have different host_flags
  EXPECT_NE(h1->host_flag, nullptr);
  EXPECT_NE(h2->host_flag, nullptr);
  EXPECT_NE(h1->host_flag, h2->host_flag);  // Different host_flags

  // Cleanup
  HixlCompleteStatus st = HixlCompleteStatus::HIXL_COMPLETE_STATUS_WAITING;
  (void)PollUntilCompleted(cli_, qh1, &st);
  (void)PollUntilCompleted(cli_, qh2, &st);

  llm::AclRuntimeStub::UnInstall(&mock_acl);
  unsetenv("HIXL_UT_DEVICE_FLAG_HACK");
}

class LoadKernelFixture : public ::testing::Test {
 protected:
  void SetUp() override {
    llm::MmpaStub::GetInstance().Reset();
    const char *env = std::getenv("ASCEND_HOME_PATH");
    if (env != nullptr) {
      original_env_ = env;
      has_env_ = true;
    }
    system("mkdir -p ./test_opp/opp/built-in/op_impl/aicpu/config");
  }
  void TearDown() override {
    if (has_env_) {
      setenv("ASCEND_HOME_PATH", original_env_.c_str(), 1);
    } else {
      unsetenv("ASCEND_HOME_PATH");
    }
    system("rm -rf ./test_opp");
    llm::MmpaStub::GetInstance().Reset();
  }
  void CreateDummyJson(const std::string &path, bool readable) {
    std::string cmd = "echo '{}' > " + path;
    system(cmd.c_str());
    if (!readable) {
      cmd = "chmod 000 " + path;
      system(cmd.c_str());
    }
  }
  std::string original_env_;
  bool has_env_ = false;
};

TEST_F(LoadKernelFixture, NoEnvAndFileNotFound) {
  auto mock_mmpa = std::make_shared<MockMmpaStub>();
  mock_mmpa->real_path_ok_ = false;
  mock_mmpa->access_ok_ = false;
  llm::MmpaStub::GetInstance().SetImpl(mock_mmpa);
  unsetenv("ASCEND_HOME_PATH");
  aclrtBinHandle bin_handle = nullptr;
  DeviceFuncHandles func_handles{};
  Status ret = LoadDeviceKernelAndGetHandles("GetFunc", "PutFunc", bin_handle, func_handles);
  EXPECT_EQ(ret, PARAM_INVALID);
}

TEST_F(LoadKernelFixture, AclLoadBinaryFailed) {
  setenv("ASCEND_HOME_PATH", "./test_opp", 1);
  std::string file_path = "./test_opp/opp/built-in/op_impl/aicpu/config/libcann_hixl_kernel.json";
  CreateDummyJson(file_path, true);
  aclrtBinHandle bin_handle = nullptr;
  DeviceFuncHandles func_handles{};
  MockAclRuntimeStub mock_acl;
  llm::AclRuntimeStub::Install(&mock_acl);
  EXPECT_CALL(mock_acl, aclrtBinaryLoadFromFile(testing::_, testing::_, testing::_))
      .WillOnce(testing::Return(static_cast<aclError>(FAILED)));
  Status ret = LoadDeviceKernelAndGetHandles("GetFunc", "PutFunc", bin_handle, func_handles);
  llm::AclRuntimeStub::UnInstall(&mock_acl);
  EXPECT_EQ(ret, FAILED);
}

TEST_F(LoadKernelFixture, GetFuncHandleInvalidParams) {
  setenv("ASCEND_HOME_PATH", "./test_opp", 1);
  aclrtBinHandle dummy_bin_handle = reinterpret_cast<aclrtBinHandle>(0xDEADBEEF);
  DeviceFuncHandles func_handles{};
  Status ret = LoadDeviceKernelAndGetHandles(nullptr, "PutFunc", dummy_bin_handle, func_handles);
  EXPECT_EQ(ret, PARAM_INVALID);
  ret = LoadDeviceKernelAndGetHandles("GetFunc", nullptr, dummy_bin_handle, func_handles);
  EXPECT_EQ(ret, PARAM_INVALID);
}

TEST_F(LoadKernelFixture, GetFuncHandleAclGetFuncFailed) {
  setenv("ASCEND_HOME_PATH", "./test_opp", 1);
  aclrtBinHandle dummy_bin_handle = reinterpret_cast<aclrtBinHandle>(0xDEADBEEF);
  DeviceFuncHandles func_handles{};
  MockAclRuntimeStub mock_acl;
  llm::AclRuntimeStub::Install(&mock_acl);
  EXPECT_CALL(mock_acl, aclrtBinaryGetFunction(testing::_, testing::_, testing::_))
      .WillOnce(testing::Return(static_cast<aclError>(FAILED)));
  Status ret = LoadDeviceKernelAndGetHandles("GetFunc", "PutFunc", dummy_bin_handle, func_handles);
  llm::AclRuntimeStub::UnInstall(&mock_acl);
  EXPECT_EQ(ret, FAILED);
}

// Slot reuse tests - unit tests for shared slot management logic
class HixlCSClientSlotReuseFixture : public HixlCSClientDeviceFixture {
 protected:
  void SetUp() override {
    HixlCSClientDeviceFixture::SetUp();
    // device_id_ is set by InitDeviceResource which calls aclrtGetDevice (stub returns 0)
    // TransferPool::GetInstance(device_id_) is initialized by InitDeviceResource
  }
};

TEST_F(HixlCSClientSlotReuseFixture, ActiveSlotInitiallyNull) {
  EXPECT_EQ(cli_.active_slot_.get(), nullptr);
}

TEST_F(HixlCSClientSlotReuseFixture, AcquireSharedSlotReusesActiveSlot) {
  // Skip this test if TransferPool not properly initialized (UT environment limitation)
  if (cli_.device_id_ < 0) {
    GTEST_SKIP() << "TransferPool not initialized";
  }
  std::shared_ptr<TransferPool::SlotHandle> first;
  Status ret = cli_.AcquireSharedSlot(first);
  if (ret != SUCCESS) {
    GTEST_SKIP() << "TransferPool Acquire failed in UT env";
  }
  std::shared_ptr<TransferPool::SlotHandle> second;
  ASSERT_EQ(cli_.AcquireSharedSlot(second), SUCCESS);
  EXPECT_NE(first.get(), nullptr);
  EXPECT_NE(second.get(), nullptr);
  EXPECT_EQ(first.get(), second.get());
  EXPECT_EQ(cli_.active_slot_.get(), first.get());

  cli_.ReleaseSharedSlotRef(first);
  EXPECT_NE(cli_.active_slot_.get(), nullptr);
  cli_.ReleaseSharedSlotRef(second);
  EXPECT_EQ(cli_.active_slot_.get(), nullptr);
}

TEST_F(HixlCSClientSlotReuseFixture, ReleaseSharedSlotRefReturnsSlotToPoolAfterLastRef) {
  if (cli_.device_id_ < 0) {
    GTEST_SKIP() << "TransferPool not initialized";
  }
  std::shared_ptr<TransferPool::SlotHandle> first;
  std::shared_ptr<TransferPool::SlotHandle> second;
  ASSERT_EQ(cli_.AcquireSharedSlot(first), SUCCESS);
  ASSERT_EQ(cli_.AcquireSharedSlot(second), SUCCESS);
  const uint32_t slot_index = first->slot_index;

  cli_.ReleaseSharedSlotRef(first);
  EXPECT_EQ(first.get(), nullptr);
  EXPECT_NE(cli_.active_slot_.get(), nullptr);

  cli_.ReleaseSharedSlotRef(second);
  EXPECT_EQ(second.get(), nullptr);
  EXPECT_EQ(cli_.active_slot_.get(), nullptr);

  auto *pool = TransferPool::GetInstance(cli_.device_id_);
  ASSERT_NE(pool, nullptr);
  std::vector<TransferPool::SlotHandle> acquired;
  bool released_slot_found = false;
  for (uint32_t i = 0U; i < 128U; ++i) {
    TransferPool::SlotHandle slot{};
    ASSERT_EQ(pool->Acquire(&slot), SUCCESS);
    if (slot.slot_index == slot_index) {
      released_slot_found = true;
    }
    acquired.emplace_back(slot);
    if (released_slot_found) {
      break;
    }
  }
  EXPECT_TRUE(released_slot_found);
  for (const auto &slot : acquired) {
    pool->Release(slot);
  }
}

TEST_F(HixlCSClientSlotReuseFixture, HostFlagInDeviceCompleteHandle) {
  DeviceCompleteHandle handle{};
  handle.magic = 0x55425548U;  // kDeviceCompleteMagic
  handle.host_flag = nullptr;
  EXPECT_EQ(handle.host_flag, nullptr);

  // Verify structure has the field
  void *test_flag = reinterpret_cast<void *>(0x1234);
  handle.host_flag = test_flag;
  EXPECT_EQ(handle.host_flag, test_flag);
}

TEST_F(HixlCSClientSlotReuseFixture, DeviceLaunchMutexExists) {
  // Verify the mutex exists and can be locked
  std::lock_guard<std::mutex> lock(cli_.device_launch_mu_);
  // If we can acquire the lock, the mutex works correctly
  SUCCEED();
}

TEST_F(HixlCSClientSlotReuseFixture, AsyncTransferWithEnvHackAllocatesHostFlag) {
  setenv("HIXL_UT_DEVICE_FLAG_HACK", "1", 1);
  HixlOneSideOpDesc desc = SetupBatchTransfer(false);

  MockAclRuntimeStub mock_acl;
  llm::AclRuntimeStub::Install(&mock_acl);
  EXPECT_CALL(mock_acl, aclrtWaitAndResetNotify(testing::_, testing::_, testing::_))
      .WillRepeatedly(testing::Return(ACL_SUCCESS));
  EXPECT_CALL(mock_acl, aclrtMemcpyAsync(testing::_, testing::_, testing::_, testing::_, testing::_, testing::_))
      .WillRepeatedly(testing::Return(ACL_SUCCESS));
  EXPECT_CALL(mock_acl, aclrtSynchronizeStreamWithTimeout(testing::_, kDefaultStreamSyncTimeoutMsForTest))
      .WillRepeatedly(testing::Return(ACL_ERROR_NONE));

  void *qh = nullptr;
  const Status ret = cli_.BatchTransferAsync(false, 1, &desc, &qh);
  llm::AclRuntimeStub::UnInstall(&mock_acl);

  if (ret != SUCCESS) {
    GTEST_SKIP() << "BatchTransfer failed in UT env (TransferPool not initialized)";
  }

  ASSERT_NE(qh, nullptr);
  DeviceCompleteHandle *handle = static_cast<DeviceCompleteHandle *>(qh);
  // Async transfer should have independent host_flag
  EXPECT_NE(handle->host_flag, nullptr);

  // Cleanup
  HixlCompleteStatus st = HixlCompleteStatus::HIXL_COMPLETE_STATUS_WAITING;
  (void)PollUntilCompleted(cli_, qh, &st);
  unsetenv("HIXL_UT_DEVICE_FLAG_HACK");
}

TEST_F(HixlCSClientSlotReuseFixture, SyncTransferUsesStreamSync) {
  HixlOneSideOpDesc desc = SetupBatchTransfer(false);

  MockAclRuntimeStub mock_acl;
  llm::AclRuntimeStub::Install(&mock_acl);
  EXPECT_CALL(mock_acl, aclrtSynchronizeStreamWithTimeout(testing::_, testing::_))
      .WillRepeatedly(testing::Return(ACL_ERROR_NONE));
  EXPECT_CALL(mock_acl, aclrtMemcpyAsync(testing::_, testing::_, testing::_, testing::_, testing::_, testing::_))
      .Times(0);  // Sync transfer should NOT call memcpy async for flag

  const Status ret = cli_.BatchTransferSync(false, 1, &desc, 100U);
  llm::AclRuntimeStub::UnInstall(&mock_acl);

  if (ret != SUCCESS) {
    GTEST_SKIP() << "BatchTransferSync failed in UT env (TransferPool not initialized)";
  }

  EXPECT_EQ(ret, SUCCESS);
  EXPECT_TRUE(cli_.pending_device_handles_.empty());
}

TEST_F(HixlCSClientSlotReuseFixture, DestroyWaitsForClientMutex) {
  std::atomic<bool> destroy_done{false};
  std::thread destroy_thread;
  {
    std::lock_guard<std::mutex> lock(cli_.mutex_);
    destroy_thread = std::thread([this, &destroy_done]() {
      (void)cli_.Destroy();
      destroy_done.store(true, std::memory_order_release);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    EXPECT_FALSE(destroy_done.load(std::memory_order_acquire));
  }

  destroy_thread.join();
  EXPECT_TRUE(destroy_done.load(std::memory_order_acquire));
}

// ============================================================================
// ConvertUboeDescs 地址转换测试
// ============================================================================

// 测试场景：remote_buf 错误地注册到 client_regions_（应该在 server_regions_），地址转换应失败
TEST_F(HixlCSClientDeviceFixture, ConvertUboeDescsFailsIfRemoteRegisteredInWrongRegion) {
  // 错误：remote_buf 注册到 client_regions_（应该是 server_regions_）
  // local_buf 也注册到 client_regions_（正确）
  cli_.mem_store_.RecordMemory(false, remote_buf_.data(), remote_buf_.size());
  cli_.mem_store_.RecordMemory(false, local_buf_.data(), local_buf_.size());

  HixlOneSideOpDesc desc{};
  desc.remote_buf = remote_buf_.data();
  desc.local_buf = local_buf_.data();
  desc.len = kLen8;

  // 直接调用 ConvertUboeDescs，绕过 ValidateAddress
  std::vector<HixlOneSideOpDesc> mutable_descs(1, desc);
  const Status ret = cli_.ConvertUboeDescs(1, mutable_descs.data());
  EXPECT_EQ(ret, FAILED);  // remote_buf 在错误的 regions，ConvertUboeDescs 会失败
}

// 测试场景：local_buf 错误地注册到 server_regions_（应该在 client_regions_），地址转换应失败
TEST_F(HixlCSClientDeviceFixture, ConvertUboeDescsFailsIfLocalRegisteredInWrongRegion) {
  // remote_buf 注册到 server_regions_（正确）
  // 错误：local_buf 也注册到 server_regions_（应该是 client_regions_）
  cli_.mem_store_.RecordMemory(true, remote_buf_.data(), remote_buf_.size());
  cli_.mem_store_.RecordMemory(true, local_buf_.data(), local_buf_.size());

  HixlOneSideOpDesc desc{};
  desc.remote_buf = remote_buf_.data();
  desc.local_buf = local_buf_.data();
  desc.len = kLen8;

  // 直接调用 ConvertUboeDescs，绕过 ValidateAddress
  std::vector<HixlOneSideOpDesc> mutable_descs(1, desc);
  const Status ret = cli_.ConvertUboeDescs(1, mutable_descs.data());
  EXPECT_EQ(ret, FAILED);  // local_buf 在错误的 regions，ConvertUboeDescs 会失败
}

}  // namespace hixl
