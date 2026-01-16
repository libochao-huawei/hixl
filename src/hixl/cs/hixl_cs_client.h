/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_CS_HIXL_CS_CLIENT_H_
#define CANN_HIXL_SRC_HIXL_CS_HIXL_CS_CLIENT_H_

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <array>
#include <atomic>
#include <vector>
#include <map>
#include "common/hixl_cs.h"
#include "hixl/hixl_types.h"
#include "endpoint.h"
#include "channel.h"
#include "hixl_mem_store.h"
#include "complete_pool.h"


namespace hixl {

namespace {
constexpr uint32_t kUbThreadNum = 1U;
constexpr uint32_t kUbNotifyNumPerThread = 1U;
constexpr CommEngine kUbEngine = CommEngine::COMM_ENGINE_AICPU;

CompletePool g_complete_pool;  // 进程级共享池：Create ref++，Destroy ref--
}  // namespace

struct CompleteHandle {
  uint32_t magic;
  int32_t flag_index;
  uint64_t *flag_address;
};

enum class UbOpType : uint32_t {
  kGet = 0U,
  kPut = 1U,
};

struct UbBatchKernelArgs {
  ThreadHandle thread;
  ChannelHandle channel;
  void *dev_flag;         // device 地址（notify 映射得到）
  uint32_t list_num;
  UbOpType op;
};

struct UbBatchArgs {
  uint32_t is_get;      // 1=get, 0=put
  uint32_t list_num;
  uint64_t thread;      // thread handle（下游需要）
  uint64_t channel;     // channel handle（下游需要）
  uint64_t local_buf_list;   // client 侧地址数组
  uint64_t remote_buf_list;  // server 侧地址数组
  uint64_t len_list;         // 长度数组
  void *remote_flag;  // server builtin flag addr（你要的 tag_mem_descs_[...].addr）
  uint64_t local_flag;   // device local flag addr（notify addr）
  uint32_t flag_size;    // sizeof(flag)
  uint32_t reserved;
};

struct UbCompleteHandle {
  uint32_t magic;
  uint32_t reserved;
  CompletePool::SlotHandle slot;
  UbBatchArgs args;
};

struct CommunicateMem {
  uint32_t list_num;
  void **dst_buf_list;
  const void **src_buf_list;
  uint64_t *len_list;
};

struct Buffers {
  const void* remote;
  const void* local;
};

struct ImportCtx {
  Endpoint *ep{nullptr};
  EndPointHandle ep_handle{nullptr};
  HixlMemStore *store{nullptr};
  uint32_t num{0U};
  bool need_tag{false};
  std::vector<HcommBuf> imported;
  std::vector<void*> recorded_addrs;
  std::map<std::string, HcclMem> tag_mem_map;
  std::vector<HcclMem> mems;
  std::vector<std::vector<char>> tag_storage;
};

class HixlCSClient {
 public:
  HixlCSClient();
  ~HixlCSClient();

  Status InitFlagQueue() noexcept;

  Status Create(const char *server_ip, uint32_t server_port, const EndPointDesc *src_endpoint,
                const EndPointDesc *dst_endpoint);

  Status Connect(uint32_t timeout_ms);

  Status GetRemoteMem(HcclMem **remote_mem_list, char ***mem_tag_list, uint32_t *list_num, uint32_t timeout_ms);

  // 注册client的endpoint的内存信息到内存注册表中。
  Status RegMem(const char *mem_tag, const HcclMem *mem, MemHandle *mem_handle);

  // 通过已经建立好的channel，从用户提取的地址列表中，批量读取、写入server内存地址中的内容
  Status BatchTransfer(bool is_get, const CommunicateMem &communicate_mem_param, void **queryhandle);

  // 通过已经建立好的channel，检查批量读写的状态。
  Status CheckStatus(void *queryhandle, int32_t *status);

  // 注册client的endpoint的内存信息到内存注册表中。
  Status UnRegMem(MemHandle mem_handle);

  Status Destroy();

 private:
  Status ExchangeEndpointAndCreateChannelLocked(uint32_t timeout_ms);
  int32_t AcquireFlagIndex();
  Status ReleaseCompleteHandle(CompleteHandle *queryhandle);
  Status ReleaseUbCompleteHandle(UbCompleteHandle *ub_handle);
  Status CheckStatusHost(CompleteHandle *queryhandle, int32_t *status);
  Status CheckStatusDevice(UbCompleteHandle *queryhandle, int32_t *status);
  Status BatchTransferHost(bool is_get, const CommunicateMem& p, void** queryhandle);
  Status BatchTransferDevice(bool is_get, const CommunicateMem& p, void** queryhandle);
  Status EnsureUbRemoteFlagInitedLocked();
  Status EnsureUbKernelLoadedLocked();
  const void *UbGetKernelStubFunc(bool is_get) const;
  Status ImportRemoteMem(std::vector<HixlMemDesc> &desc_list, HcclMem **remote_mem_list, char ***mem_tag_list,
                         uint32_t *list_num);
  void FillOutputParams(ImportCtx &ctx, HcclMem **remote_mem_list, char ***mem_tag_list, uint32_t *list_num);
  Status ClearRemoteMemInfo();
 private:
  std::mutex mutex_;
  // 用于记录内存地址的分配情况
  HixlMemStore mem_store_;
  std::string server_ip_;
  uint32_t server_port_{0U};
  EndpointPtr src_endpoint_;
  EndPointDesc dst_endpoint_{};
  Channel client_channel_;
  ChannelHandle client_channel_handle_ = 0UL;
  uint64_t dst_endpoint_handle_{0U};
  static constexpr size_t kFlagQueueSize = 4096;                  // 用于初始化队列和内存地址列表
  uint64_t* flag_queue_ = nullptr;
  std::array<uint32_t, kFlagQueueSize> available_indices_{};
  size_t top_index_ = 0;                                           // 栈顶指针
  std::mutex indices_mutex_;
  std::array<CompleteHandle*, kFlagQueueSize> live_handles_{}; // 用来记录读写生成的queryhandle
  int32_t socket_ = -1;
  std::map<std::string, HcclMem> tag_mem_descs_;
  std::vector<HcclMem> remote_mems_out_;
  std::vector<std::vector<char>> remote_tag_storage_;
  std::vector<char*> remote_tag_ptrs_;
  std::vector<void*> recorded_remote_addrs_;
  std::vector<HcommBuf> imported_remote_bufs_;
  bool is_device_ {false};
  int32_t ub_device_id_ {-1};
  std::mutex ub_mu_;
  bool ub_remote_flag_inited_ {false};
  void *ub_remote_flag_addr_ {nullptr};   // server 侧地址（远端地址）
  uint64_t ub_remote_flag_size_ {0ULL};   // 至少 8
  // UB kernel load cache
  bool ub_kernel_loaded_ {false};
  aclrtBinHandle ub_kernel_handle_ {nullptr};
  const void *ub_stub_get_ {nullptr};
  const void *ub_stub_put_ {nullptr};

};
}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_CS_HIXL_CS_CLIENT_H_
