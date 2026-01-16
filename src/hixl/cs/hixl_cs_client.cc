/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hixl_cs_client.h"
#include <chrono>
#include <thread>
#include <cstring>
#include <algorithm>
#include <securec.h>
#include "runtime/rt.h"
#include "hixl/hixl_types.h"
#include "common/hixl_checker.h"
#include "common/hixl_log.h"
#include "common/hixl_utils.h"
#include "common/scope_guard.h"
#include "common/ctrl_msg_plugin.h"
#include "conn_msg_handler.h"
#include "mem_msg_handler.h"
#include "load_kernel.h"


namespace hixl {

constexpr uint32_t kFlagSizeBytes = 8;
constexpr int32_t kDefaultChannelStatus = -1;
constexpr uint32_t kWaitChannelPollIntervalMs = 1U;
constexpr uint64_t kFlagDoneValue = 1ULL;
constexpr uint64_t kFlagResetValue = 0ULL;
constexpr uint16_t kRtMallocModuleId = 0;

namespace {
constexpr uint32_t kUbCompleteMagic = 0x55425548U;
constexpr uint32_t kLegacyCompleteMagic = 0x4C454741U; // 'LEGA'
constexpr const char *kUbRemoteFlagTag = "_hixl_builtin_dev_trans_flag";//TODO:是否要设置不唯一
constexpr const char *kTransFlagNameHost = "_hixl_builtin_host_trans_flag";
constexpr const char *kTransFlagNameDevice = "_hixl_builtin_dev_trans_flag";
constexpr uint64_t kUbFlagDoneValue = 1ULL;
constexpr uint64_t kUbFlagInitValue = 0ULL;
constexpr uint32_t kUbFlagBytes = sizeof(uint64_t);

inline uint64_t PtrToU64(const void *p) {
  return reinterpret_cast<uintptr_t>(p);
}

constexpr const char *kUbKernelJson = "libcann_hixl_kernel.json";
constexpr const char *kUbFuncGet = "HixlBatchGet";
constexpr const char *kUbFuncPut = "HixlBatchPut";
}  // namespace


HixlCSClient::HixlCSClient() : mem_store_() {
  flag_queue_ = nullptr;
  top_index_ = 0;  // 未初始化时为空栈
  for (size_t i = 0; i < kFlagQueueSize; ++i) {
    available_indices_[i] = static_cast<int32_t>(i);
    live_handles_[i] = nullptr;
  }
}

Status HixlCSClient::InitFlagQueue() noexcept {
  if (flag_queue_ != nullptr) {
    return SUCCESS;  // 已初始化
  }
  void *tmp = nullptr;
  rtError_t ret = rtMallocHost(&tmp, kFlagQueueSize * sizeof(uint64_t), HCCL);
  if (ret != RT_ERROR_NONE || tmp == nullptr) {
    HIXL_LOGE(RESOURCE_EXHAUSTED, "rtMallocHost failed, ret=%d", ret);
    return RESOURCE_EXHAUSTED;
  }
  flag_queue_ = static_cast<uint64_t *>(tmp);
  for (size_t i = 0; i < kFlagQueueSize; ++i) {
    flag_queue_[i] = 0;
  }
  top_index_ = kFlagQueueSize;  // 初始化成功后可用
  return SUCCESS;
}

HixlCSClient::~HixlCSClient() {
  if (flag_queue_ != nullptr) {
    rtError_t ret = rtFreeHost(flag_queue_);
    if (ret != RT_ERROR_NONE) {
      HIXL_LOGI("rtFreeHost failed, ret=%d", ret);
    }
    flag_queue_ = nullptr;
  }
  for (size_t i = 0; i < kFlagQueueSize; ++i){
    if (live_handles_[i] != nullptr) {
      delete live_handles_[i];
      live_handles_[i] = nullptr;
    }
  }
}

namespace {
static inline bool IsDeviceEndpoint(const EndPointDesc &ep) {
  return (ep.loc.locType == END_POINT_LOCATION_DEVICE);
}
}

Status HixlCSClient::Create(const char *server_ip, uint32_t server_port, const EndPointDesc *src_endpoint,
                            const EndPointDesc *dst_endpoint) {
  HIXL_CHECK_NOTNULL(server_ip);
  HIXL_CHECK_NOTNULL(src_endpoint);
  HIXL_CHECK_NOTNULL(dst_endpoint);
  HIXL_EVENT("[HixlClient] Create begin. Server=%s:%u. "
             "Src[Loc:%d, Proto:%d, Type:%d, Val:0x%x], "
             "Dst[Loc:%d, Proto:%d, Type:%d, Val:0x%x]",
             server_ip, server_port,
             src_endpoint->loc, src_endpoint->protocol, src_endpoint->addr.type, src_endpoint->addr.id,
             dst_endpoint->loc, dst_endpoint->protocol, dst_endpoint->addr.type, dst_endpoint->addr.id);
  std::lock_guard<std::mutex> lock(mutex_);
  server_ip_ = server_ip;
  server_port_ = server_port;
  src_endpoint_ = MakeShared<Endpoint>(*src_endpoint);
  HIXL_CHECK_NOTNULL(src_endpoint_);
  Status ret = src_endpoint_->Initialize();
  HIXL_CHK_STATUS_RET(ret,
                      "[HixlClient] Failed to initialize src endpoint. "
                      "Check Config: [Loc:%d, Proto:%d, AddrVal:0x%x]",
                      src_endpoint->loc, src_endpoint->protocol, src_endpoint->addr.id);
  HIXL_LOGI("[HixlClient] src_endpoint initialized. ep_handle=%p", src_endpoint_->GetHandle());
  dst_endpoint_ = *dst_endpoint;
  CtrlMsgPlugin::Initialize();
  HIXL_LOGD("[HixlClient] CtrlMsgPlugin initialized");
  EndPointHandle endpoint_handle = src_endpoint_->GetHandle();
  HIXL_EVENT("[HixlClient] Create success. server=%s:%u, src_ep_handle=%p", server_ip_.c_str(), server_port_,
             endpoint_handle);
  Status init_ret = InitFlagQueue();
  if (init_ret != SUCCESS) {
    HIXL_LOGE(init_ret, "[HixlClient] Failed to initialize flag queue.");
    return init_ret;
  }
  const EndPointDesc &src_ep = src_endpoint_->GetEndpoint();
  is_device_ = IsDeviceEndpoint(src_ep);
  if (is_device_) {
    // deviceId：优先用 endpoint 的 ID（若给的是 ID 类型）TODO：获取deviceID
    if (src_endpoint->addr.type == COMM_ADDR_TYPE_ID) {
      ub_device_id_ = static_cast<int32_t>(src_endpoint->addr.id);
    } else {
      ub_device_id_ = -1;
    }

    if (ub_device_id_ < 0) {
      int32_t curDevId = -1;
      const rtError_t rret = rtGetDevice(&curDevId);
      if (rret != RT_ERROR_NONE) {
        HIXL_LOGE(FAILED, "[HixlClient] rtGetDevice failed in Create. ret=%d", static_cast<int32_t>(rret));
        return FAILED;
      }
      ub_device_id_ = curDevId;
    }

    // pool ref++，必要时初始化 128 slots（一次性准备）
    const HcclResult pret =
        g_complete_pool.AddRefAndInitIfNeeded(ub_device_id_, kUbEngine, kUbThreadNum, kUbNotifyNumPerThread, src_endpoint_.get());
    if (pret != HCCL_SUCCESS) {
      HIXL_LOGE(FAILED,
                "[HixlClient] CompletePool AddRefAndInitIfNeeded failed. devId=%d ret=0x%X",
                ub_device_id_, static_cast<uint32_t>(pret));
      return FAILED;
    }

    HIXL_LOGI("[HixlClient] UB mode enabled. devId=%d", ub_device_id_);
  }
  return SUCCESS;
}

// 注册client的endpoint的内存信息到内存注册表中。mem是一个结构体，其中记录了内存类型、地址和大小。
Status HixlCSClient::RegMem(const char *mem_tag, const HcclMem *mem, MemHandle *mem_handle) {
  HIXL_CHECK_NOTNULL(mem);
  auto check_result = mem_store_.CheckMemoryForRegister(false, mem->addr, mem->size);
  if (check_result) {
    HIXL_LOGE(PARAM_INVALID, "Memory registration failed. The provided memory has already been registered. Please check Mem, mem_adrr: %p, mem_size: %u.", mem->addr, mem->size);
    return PARAM_INVALID;
  }
  MemHandle ep_mem_handle = nullptr;
  HIXL_CHK_STATUS_RET(src_endpoint_->RegisterMem(mem_tag, *mem, ep_mem_handle), "Failed to register mem.");
  *mem_handle = ep_mem_handle;
  mem_store_.RecordMemory(false, mem->addr, mem->size);  // 记录client侧给endpoint分配的内存信息
  return SUCCESS;
}

// 获取列表中有效的flag，考虑多线程调用，加上线程锁
int32_t HixlCSClient::AcquireFlagIndex() {
  std::lock_guard<std::mutex> lock(indices_mutex_);
  if (top_index_ == size_t{0}) {
    return -1;
  }
  size_t idx = top_index_ - size_t{1};
  top_index_ = idx;
  return available_indices_[idx];
}

Status HixlCSClient::ReleaseCompleteHandle(CompleteHandle *queryhandle) {
  HIXL_CHECK_NOTNULL(queryhandle);
  std::lock_guard<std::mutex> lock(indices_mutex_);
  if (top_index_ < kFlagQueueSize) {
    size_t idx = top_index_ + size_t{1};
    top_index_ = idx;
    available_indices_[idx] = queryhandle->flag_index; // 回收索引
    live_handles_[queryhandle->flag_index] = nullptr;
  }
  delete queryhandle;
  return SUCCESS;
}

namespace {
Buffers SelectBuffers(bool is_get, const void *src, const void *dst) noexcept {
  return is_get ? Buffers{src, dst} : Buffers{dst, src};
}
}

Status HixlCSClient::BatchTransferHost(bool is_get, const CommunicateMem& communicate_mem_param, void** queryhandle) {
  if (flag_queue_ == nullptr) {
    HIXL_LOGE(RESOURCE_EXHAUSTED, "Client not initialized: flag queue is null.");
    return RESOURCE_EXHAUSTED;
  }
  // 先校验用户提供的地址的有效性
  for (uint32_t i = 0; i < communicate_mem_param.list_num; i++) {
    Buffers buffer =
        SelectBuffers(is_get, communicate_mem_param.src_buf_list[i], communicate_mem_param.dst_buf_list[i]);
    Status check_result =
        mem_store_.ValidateMemoryAccess(buffer.remote, communicate_mem_param.len_list[i], buffer.local);
    if (check_result != SUCCESS) {
      HIXL_LOGE(PARAM_INVALID,
                "This memory is not registered and cannot be read from or written to. "
                "Please check remote_buf:%p, local_buf:%p, buf_len:%u",
                buffer.remote, buffer.local, communicate_mem_param.len_list[i]);
      return PARAM_INVALID;
    }
  }

  if (is_get) {
    // 批量提交读任务
    for (uint32_t i = 0; i < communicate_mem_param.list_num; i++) {
      HcommReadNbi(client_channel_handle_, communicate_mem_param.dst_buf_list[i], const_cast<void *>(communicate_mem_param.src_buf_list[i]),
                   communicate_mem_param.len_list[i]);  // HcommReadNbi 没有返回值
    }
  }
  else {
    // 批量提交写任务
    for (uint32_t i = 0; i < communicate_mem_param.list_num; i++) {
      HcommWriteNbi(client_channel_handle_, communicate_mem_param.dst_buf_list[i], const_cast<void *>(communicate_mem_param.src_buf_list[i]),
                    communicate_mem_param.len_list[i]);  // HcommWriteNbi 没有返回值
    }
  }
  // 创建内存隔断，等到通道上所有的读任务执行结束后才会接着执行之后创建的读写任务
  HcommChannelFence(client_channel_handle_);

  int32_t flag_index = AcquireFlagIndex();
  if (flag_index == -1) {
    HIXL_LOGE(PARAM_INVALID, "There are a large number of transfer tasks with no query results, making it impossible to create new transfer tasks.");
    return PARAM_INVALID;
  }
  uint64_t *flag_addr = &flag_queue_[flag_index];
  EndPointDesc endpoint = src_endpoint_->GetEndpoint();
  const char *kTransFlagName = nullptr;
  if (endpoint.loc.locType == END_POINT_LOCATION_HOST) {
    kTransFlagName = kTransFlagNameHost;
  } else {
    kTransFlagName = kTransFlagNameDevice;
  }
  HcommReadNbi(client_channel_handle_, flag_addr, tag_mem_descs_[kTransFlagName].addr, kFlagSizeBytes);
  CompleteHandle* query_mem_handle = new (std::nothrow) CompleteHandle();
  if (query_mem_handle != nullptr ) {
    query_mem_handle->magic = kLegacyCompleteMagic;
    query_mem_handle->flag_index = flag_index;
    query_mem_handle->flag_address = flag_addr;
    // 需要先创建queryhandle实体，之后再传给指针。
    *queryhandle = query_mem_handle;
    live_handles_[flag_index] = query_mem_handle;
  }
  else {
    HIXL_LOGE(PARAM_INVALID, "Memory allocation failed; unable to generate query handle.");
    return PARAM_INVALID;
  }
  return SUCCESS;
}

Status HixlCSClient::EnsureUbRemoteFlagInitedLocked() {
  if (ub_remote_flag_inited_) {
    return SUCCESS;
  }
  EndPointDesc endpoint = src_endpoint_->GetEndpoint();
  const char *kTransFlagName = nullptr;
  if (endpoint.loc.locType == END_POINT_LOCATION_HOST) {
    kTransFlagName = kTransFlagNameHost;
  } else {
    kTransFlagName = kTransFlagNameDevice;
  }
  const auto it = tag_mem_descs_.find(kTransFlagName);
  if (it == tag_mem_descs_.end()) {
    HIXL_LOGE(PARAM_INVALID, "[HixlClient][UB] builtin remote_flag tag not found: %s", kTransFlagName);
    return PARAM_INVALID;
  }

  const HcclMem &mem = it->second;
  if (mem.addr == nullptr) {
    HIXL_LOGE(PARAM_INVALID, "[HixlClient][UB] builtin remote_flag addr is null");
    return PARAM_INVALID;
  }

  if (mem.size < static_cast<uint64_t>(sizeof(uint64_t))) {
    HIXL_LOGE(PARAM_INVALID, "[HixlClient][UB] builtin remote_flag size too small. size=%" PRIu64, mem.size);
    return PARAM_INVALID;
  }

  ub_remote_flag_addr_ = mem.addr;
  ub_remote_flag_size_ = mem.size;
  ub_remote_flag_inited_ = true;

  HIXL_LOGI("[HixlClient][UB] builtin remote_flag ready. addr=%p u64=%" PRIu64 " size=%" PRIu64,
            mem.addr,
            ub_remote_flag_addr_,
            ub_remote_flag_size_);
  return SUCCESS;
}

Status HixlCSClient::ReleaseUbCompleteHandle(UbCompleteHandle *h) {
  HIXL_CHECK_NOTNULL(h);

  if (h->magic != kUbCompleteMagic) {
    HIXL_LOGW("[HixlClient][UB] ReleaseUbCompleteHandle bad magic=0x%X", h->magic);
  }

  g_complete_pool.Release(h->slot.slotIndex);

  delete h;
  return SUCCESS;
}

Status HixlCSClient::EnsureUbKernelLoadedLocked() {
  if (ub_kernel_loaded_) {
    return SUCCESS;
  }

  HIXL_CHK_BOOL_RET_STATUS(ub_device_id_ >= 0,
                           FAILED,
                           "[HixlClient][UB] ub_device_id_ invalid: %d",
                           ub_device_id_);

  hixl::UbKernelStubs stubs{};
  Status ret = hixl::LoadUbKernelAndResolveStubs(ub_device_id_,
                                                 kUbKernelJson,
                                                 kUbFuncGet,
                                                 kUbFuncPut,
                                                 ub_kernel_handle_,
                                                 stubs);
  if (ret != SUCCESS) {
    HIXL_LOGE(ret, "[HixlClient][UB] LoadUbKernelAndResolveStubs failed. dev=%d json=%s",
              ub_device_id_, kUbKernelJson);
    return ret;
  }

  HIXL_CHK_BOOL_RET_STATUS(stubs.batchGet != nullptr,
                           FAILED,
                           "[HixlClient][UB] batchGet stub is null");

  HIXL_CHK_BOOL_RET_STATUS(stubs.batchPut != nullptr,
                           FAILED,
                           "[HixlClient][UB] batchPut stub is null");

  ub_stub_get_ = stubs.batchGet;
  ub_stub_put_ = stubs.batchPut;

  ub_kernel_loaded_ = true;

  HIXL_LOGI("[HixlClient][UB] kernel loaded. dev=%d handle=%p get=%p put=%p",
            ub_device_id_,
            ub_kernel_handle_,
            ub_stub_get_,
            ub_stub_put_);
  return SUCCESS;
}

const void *HixlCSClient::UbGetKernelStubFunc(bool is_get) const {
  if (is_get) {
    return ub_stub_get_;
  }
  return ub_stub_put_;
}

Status HixlCSClient::BatchTransferDevice(bool is_get,
                                     const CommunicateMem &communicate_mem_param,
                                     void **queryhandle) {
  HIXL_CHECK_NOTNULL(queryhandle);
  *queryhandle = nullptr;

  HIXL_CHK_BOOL_RET_STATUS(communicate_mem_param.list_num > 0U,
                           PARAM_INVALID,
                           "[HixlClient][UB] list_num must be > 0");
  HIXL_CHECK_NOTNULL(communicate_mem_param.src_buf_list);
  HIXL_CHECK_NOTNULL(communicate_mem_param.dst_buf_list);
  HIXL_CHECK_NOTNULL(communicate_mem_param.len_list);

  void *remote_flag = 0ULL;
  {
    std::lock_guard<std::mutex> lk(ub_mu_);
    Status fret = EnsureUbRemoteFlagInitedLocked();
    if (fret != SUCCESS) {
      HIXL_LOGE(fret, "[HixlClient][UB] EnsureUbRemoteFlagInitedLocked failed");
      return fret;
    }
    remote_flag = ub_remote_flag_addr_;
  }
  HIXL_CHK_BOOL_RET_STATUS(remote_flag != nullptr,
                           PARAM_INVALID,
                           "[HixlClient][UB] remote_flag is nullptr");
  //TODO:用真实json文件
  {
    std::lock_guard<std::mutex> lk(ub_mu_);
    Status kr = EnsureUbKernelLoadedLocked();
    if (kr != SUCCESS) {
      HIXL_LOGE(kr, "[HixlClient][UB] EnsureUbKernelLoadedLocked failed");
      return kr;
    }
  }

  CompletePool::SlotHandle slot{};
  HcclResult aret = g_complete_pool.Acquire(&slot);
  if (aret != HCCL_SUCCESS) {
    HIXL_LOGE(FAILED,
              "[HixlClient][UB] CompletePool Acquire failed. ret=0x%X",
              static_cast<uint32_t>(aret));
    return FAILED;
  }

  HIXL_DISMISSABLE_GUARD(slot_guard, [&]() {
    g_complete_pool.Release(slot.slotIndex);
  });

  HIXL_CHK_BOOL_RET_STATUS(slot.hostFlag != nullptr, FAILED, "[HixlClient][UB] slot.hostFlag is null");
  HIXL_CHK_BOOL_RET_STATUS(slot.devFlagAddr != 0ULL, FAILED, "[HixlClient][UB] slot.devFlagAddr is 0");

  g_complete_pool.ResetHostFlag(slot);

  UbCompleteHandle *h = new (std::nothrow) UbCompleteHandle();
  if (h == nullptr) {
    HIXL_LOGE(FAILED, "[HixlClient][UB] new UbCompleteHandle failed");
    return FAILED;
  }

  HIXL_DISMISSABLE_GUARD(handle_guard, [&]() {
    delete h;
  });

  h->magic = kUbCompleteMagic;
  h->reserved = 0U;
  h->slot = slot;

  const void *local_list_ptr = nullptr;
  const void *remote_list_ptr = nullptr;

  if (is_get) {
    remote_list_ptr = static_cast<const void *>(communicate_mem_param.src_buf_list);
    local_list_ptr = static_cast<const void *>(communicate_mem_param.dst_buf_list);
  } else {
    local_list_ptr = static_cast<const void *>(communicate_mem_param.src_buf_list);
    remote_list_ptr = static_cast<const void *>(communicate_mem_param.dst_buf_list);
  }

  UbBatchArgs &args = h->args;
  args.is_get = is_get ? 1U : 0U;
  args.list_num = communicate_mem_param.list_num;
  args.thread = PtrToU64(static_cast<const void *>(slot.thread));
  args.channel = static_cast<uint64_t>(client_channel_handle_);
  args.local_buf_list = PtrToU64(local_list_ptr);
  args.remote_buf_list = PtrToU64(remote_list_ptr);
  args.len_list = PtrToU64(static_cast<const void *>(communicate_mem_param.len_list));
  args.remote_flag = remote_flag;
  args.local_flag = slot.devFlagAddr;
  args.flag_size = static_cast<uint32_t>(sizeof(uint64_t));
  args.reserved = 0U;

  // === 保存/恢复当前线程 ctx（专家要求） ===
  aclrtContext old_ctx = nullptr;
  aclError aerr = aclrtGetCurrentContext(&old_ctx);
  if (aerr != ACL_SUCCESS) {
    HIXL_LOGE(FAILED, "[HixlClient][UB] aclrtGetCurrentContext failed. ret=%d", static_cast<int32_t>(aerr));
    return FAILED;
  }

  HIXL_DISMISSABLE_GUARD(ctx_guard, [&]() {
    if (old_ctx != nullptr) {
      (void)aclrtSetCurrentContext(old_ctx);
    }
  });

  aerr = aclrtSetCurrentContext(slot.ctx);
  if (aerr != ACL_SUCCESS) {
    HIXL_LOGE(FAILED, "[HixlClient][UB] aclrtSetCurrentContext(slot.ctx) failed. ret=%d", static_cast<int32_t>(aerr));
    return FAILED;
  }

  const void *stubFunc = UbGetKernelStubFunc(is_get);
  HIXL_CHK_BOOL_RET_STATUS(stubFunc != nullptr, FAILED, "[HixlClient][UB] stubFunc is null");

  const uint32_t blockDim = 1U;
  rtSmDesc_t *smDesc = nullptr;

  rtError_t rret = rtKernelLaunch(stubFunc,
                                  blockDim,
                                  static_cast<void *>(&h->args),
                                  static_cast<uint32_t>(sizeof(UbBatchArgs)),
                                  smDesc,
                                  slot.stream);
  if (rret != RT_ERROR_NONE) {
    HIXL_LOGE(FAILED, "[HixlClient][UB] rtKernelLaunch failed. ret=%d", static_cast<int32_t>(rret));
    return FAILED;
  }

  rret = rtNotifyWait(slot.notify, slot.stream);
  if (rret != RT_ERROR_NONE) {
    HIXL_LOGE(FAILED, "[HixlClient][UB] rtNotifyWait failed. ret=%d", static_cast<int32_t>(rret));
    return FAILED;
  }

  void *hostDst = static_cast<void *>(slot.hostFlag);

  rret = rtMemcpyAsync(hostDst,
                       static_cast<uint64_t>(sizeof(uint64_t)),
                       remote_flag,
                       static_cast<uint64_t>(sizeof(uint64_t)),
                       RT_MEMCPY_DEVICE_TO_HOST,
                       slot.stream);
  if (rret != RT_ERROR_NONE) {
    HIXL_LOGE(FAILED, "[HixlClient][UB] rtMemcpyAsync(D2H) failed. ret=%d", static_cast<int32_t>(rret));
    return FAILED;
  }

  // 返回 handle（异步）
  *queryhandle = static_cast<void *>(h);

  // 成功路径：slot 交给 handle 管理，handle_guard 也放弃
  HIXL_DISMISS_GUARD(handle_guard);
  HIXL_DISMISS_GUARD(slot_guard);

  // 注意：ctx_guard 不要 dismiss，让函数返回时自动恢复 old_ctx
  HIXL_LOGI("[HixlClient][UB] BatchTransferUb submitted. is_get=%d list_num=%u slot=%u",
            static_cast<int32_t>(args.is_get),
            args.list_num,
            slot.slotIndex);
  return SUCCESS;
}


// 通过已经建立好的channel，从用户提取的地址列表中，批量读取server内存地址中的内容
Status HixlCSClient::BatchTransfer(bool is_get, const CommunicateMem &communicate_mem_param, void **queryhandle) {
  for (uint32_t i = 0U; i < communicate_mem_param.list_num; i++) {
    Buffers buffer =
        SelectBuffers(is_get, communicate_mem_param.src_buf_list[i], communicate_mem_param.dst_buf_list[i]);
    Status check_result = mem_store_.ValidateMemoryAccess(buffer.remote, communicate_mem_param.len_list[i], buffer.local);
    if (check_result != SUCCESS) {
      HIXL_LOGE(PARAM_INVALID,
                "This memory is not registered and cannot be read from or written to. "
                "Please check remote_buf:%p, local_buf:%p, buf_len:%u",
                buffer.remote, buffer.local, communicate_mem_param.len_list[i]);
      return PARAM_INVALID;
    }
  }

  if (is_device_) {
    return BatchTransferDevice(is_get, communicate_mem_param, queryhandle);
  }
  return BatchTransferHost(is_get, communicate_mem_param, queryhandle);
}

Status HixlCSClient::CheckStatusHost(CompleteHandle *queryhandle, int32_t *status) {
  // 检验queryhandle中的序号是否合规
  if (queryhandle->flag_index < 0 ||
      queryhandle->flag_index >= static_cast<int32_t>(kFlagQueueSize)) {
    HIXL_LOGE(PARAM_INVALID, "The value of queryhandle->flag_index is outside the valid verification range; please check the queryhandle. queryhandle->flag_index：%d", queryhandle->flag_index);
    return PARAM_INVALID;
  }
  // 通过读取queryhandle中地址的值，来判断任务的完成状态
  uint64_t* atomic_flag = queryhandle->flag_address;
  HIXL_CHECK_NOTNULL(atomic_flag);
  // 查到flag变成1之后，就把其重置为0，之后告知用户读写任务已经完成。
  if (*atomic_flag == kFlagDoneValue) {
    *atomic_flag = kFlagResetValue;
    *status = BatchTransferStatus::COMPLETED;
    HIXL_LOGI("The current transmission task has been completed.");
    return ReleaseCompleteHandle(queryhandle);  // 释放内存并回收索引
  }
  *status = BatchTransferStatus::WAITING;
  HIXL_LOGI("The current transmission task has not been completed.");
  return SUCCESS;
}

Status HixlCSClient::CheckStatusDevice(UbCompleteHandle *queryhandle, int32_t *status) {
  HIXL_CHECK_NOTNULL(queryhandle);
  HIXL_CHECK_NOTNULL(status);

  if (queryhandle->magic != kUbCompleteMagic) {
    HIXL_LOGE(PARAM_INVALID, "[HixlClient][UB] CheckStatusUb bad magic=0x%X", queryhandle->magic);
    return PARAM_INVALID;
  }

  uint64_t *flag_ptr = queryhandle->slot.hostFlag;
  if (flag_ptr == nullptr) {
    HIXL_LOGE(PARAM_INVALID, "[HixlClient][UB] CheckStatusUb hostFlag is null");
    return PARAM_INVALID;
  }

  const uint64_t flag_val = *flag_ptr;
  if (flag_val == kUbFlagDoneValue) {
    *flag_ptr = kUbFlagInitValue;
    *status = BatchTransferStatus::COMPLETED;

    HIXL_LOGI("[HixlClient][UB] Batch completed. slot=%u", queryhandle->slot.slotIndex);
    return ReleaseUbCompleteHandle(queryhandle);
  }

  *status = BatchTransferStatus::WAITING;
  return SUCCESS;
}

// 通过已经建立好的channel，检查批量读写的状态。
Status HixlCSClient::CheckStatus(void *queryhandle, int32_t *status) {
  HIXL_CHECK_NOTNULL(queryhandle);
  HIXL_CHECK_NOTNULL(status);

  uint32_t head = 0U;
  errno_t rc = memcpy_s(&head, sizeof(head), queryhandle, sizeof(head));
  if (rc != EOK) {
    HIXL_LOGE(FAILED, "[HixlClient] CheckStatus memcpy_s failed, rc=%d", static_cast<int32_t>(rc));
    return FAILED;
  }

  if (head == kUbCompleteMagic) {
    UbCompleteHandle *ub = static_cast<UbCompleteHandle *>(queryhandle);
    return CheckStatusDevice(ub, status);
  }

  if (head == kLegacyCompleteMagic) {
    CompleteHandle *legacy = static_cast<CompleteHandle *>(queryhandle);
    return CheckStatusHost(legacy, status);
  }

  HIXL_LOGE(PARAM_INVALID, "[HixlClient] CheckStatus bad magic=0x%X", head);
  return PARAM_INVALID;
}


// 注销client的endpoint的内存信息。
Status HixlCSClient::UnRegMem(MemHandle mem_handle) {
  HIXL_CHECK_NOTNULL(mem_handle);
  HixlMemDesc desc;
  Status query_status = src_endpoint_->GetMemDesc(mem_handle, desc);
  if (query_status != SUCCESS) {
    return PARAM_INVALID;
  }
  Status result = src_endpoint_->DeregisterMem(mem_handle);
  if (result == SUCCESS) {
    mem_store_.UnrecordMemory(false, desc.mem.addr);  // 删掉记录中client侧给endpoint分配的内存信息
    return SUCCESS;
  }
  return PARAM_INVALID;
}

Status HixlCSClient::Connect(uint32_t timeout_ms) {
  std::lock_guard<std::mutex> lock(mutex_);
  HIXL_CHECK_NOTNULL(src_endpoint_);
  HIXL_CHK_BOOL_RET_STATUS(dst_endpoint_.protocol != COMM_PROTOCOL_RESERVED, PARAM_INVALID,
                           "[HixlClient] Connect called but dst_endpoint is not set in Create");
  HIXL_EVENT("[HixlClient] Connect start. Target=%s:%u, timeout=%u ms", server_ip_.c_str(), server_port_, timeout_ms);
  HIXL_CHK_STATUS_RET(CtrlMsgPlugin::Connect(server_ip_, server_port_, socket_, timeout_ms),
                      "[HixlClient] Connect socket to %s:%u failed", server_ip_.c_str(), server_port_);
  HIXL_LOGI("[HixlClient] Socket connected (TCP ready). fd=%d", socket_);
  HIXL_CHK_STATUS_RET(ExchangeEndpointAndCreateChannelLocked(timeout_ms),
                      "[HixlClient] Exchange endpoint info failed. fd=%d, Target=%s:%u", socket_, server_ip_.c_str(),
                      server_port_);
  HIXL_EVENT("[HixlClient] Connect success. target=%s:%u, fd=%d, remote_ep_handle=%" PRIu64 ", ch=%p",
             server_ip_.c_str(), server_port_, socket_, dst_endpoint_handle_, client_channel_handle_);

  return SUCCESS;
}

Status WaitChannelReadyInternal(Endpoint &endpoint, ChannelHandle channel_handle, uint32_t timeout_ms) {
  auto start = std::chrono::steady_clock::now();
  while (true) {
    auto now = std::chrono::steady_clock::now();
    uint32_t elapsed_ms =
        static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count());
    if (elapsed_ms > timeout_ms) {
      HIXL_LOGE(TIMEOUT, "[HixlClient] Wait channel ready timeout. ch=%p, elapsed=%u ms (limit %u ms)", channel_handle,
                elapsed_ms, timeout_ms);
      return TIMEOUT;
    }
    int32_t channel_status_val = kDefaultChannelStatus;
    Status st_status = endpoint.GetChannelStatus(channel_handle, &channel_status_val);
    if (st_status != SUCCESS) {
      HIXL_LOGE(st_status, "[HixlClient] GetChannelStatus failed. ch=%p", channel_handle);
      return st_status;
    }
    if (channel_status_val == 0) {
      HIXL_LOGI("[HixlClient] Channel is ready. handle=%p, Cost=%u ms", channel_handle, elapsed_ms);
      return SUCCESS;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(kWaitChannelPollIntervalMs));
  }
}

Status HixlCSClient::ExchangeEndpointAndCreateChannelLocked(uint32_t timeout_ms) {
  const EndPointDesc &src_ep = src_endpoint_->GetEndpoint();
  HIXL_LOGD("[HixlClient] Sending CreateChannelReq. socket: %d, timeout: %u ms, "
            "Src[prot:%u, type:%u, id:%u], Dst[prot:%u, type:%u, id:%u]",
            socket_, timeout_ms,
            src_ep.protocol, src_ep.addr.type, src_ep.addr.id,
            dst_endpoint_.protocol, dst_endpoint_.addr.type, dst_endpoint_.addr.id);
  Status ret = ConnMsgHandler::SendCreateChannelRequest(socket_, src_ep, dst_endpoint_);
  HIXL_CHK_STATUS_RET(ret, "[HixlClient] SendCreateChannelRequest failed. fd=%d", socket_);
  ret = ConnMsgHandler::RecvCreateChannelResponse(socket_, dst_endpoint_handle_, timeout_ms);
  HIXL_CHK_STATUS_RET(ret, "[HixlClient] RecvCreateChannelResponse failed. fd=%d, timeout=%u ms", socket_, timeout_ms);
  HIXL_LOGI("[HixlClient] Connect: remote endpoint handle = %" PRIu64, dst_endpoint_handle_);
  ChannelHandle channel_handle = 0UL;
  ret = src_endpoint_->CreateChannel(dst_endpoint_, channel_handle);
  HIXL_CHK_STATUS_RET(ret, "[HixlClient] Endpoint CreateChannel failed. remote_ep_handle=%" PRIu64, dst_endpoint_handle_);
  ret = WaitChannelReadyInternal(*src_endpoint_, channel_handle, timeout_ms);
  HIXL_CHK_STATUS_RET(ret, "[HixlClient] WaitChannelReadyInternal failed. ch=%p, timeout=%u ms", channel_handle, timeout_ms);
  client_channel_handle_ = channel_handle;
  HIXL_LOGI("[HixlClient] Channel Ready. client_channel_handle_=%p", client_channel_handle_);
  return SUCCESS;
}

Status HixlCSClient::GetRemoteMem(HcclMem **remote_mem_list, char ***mem_tag_list, uint32_t *list_num,
                                  uint32_t timeout_ms) {
  HIXL_EVENT("[HixlClient] GetRemoteMem begin. fd=%d, remote_ep_handle=%" PRIu64 ", timeout=%u ms", socket_,
             dst_endpoint_handle_, timeout_ms);
  HIXL_CHECK_NOTNULL(remote_mem_list);
  HIXL_CHECK_NOTNULL(mem_tag_list);
  HIXL_CHECK_NOTNULL(list_num);
  *remote_mem_list = nullptr;
  *mem_tag_list = nullptr;
  std::lock_guard<std::mutex> lock(mutex_);
  HIXL_CHECK_NOTNULL(src_endpoint_);
  Status ret = MemMsgHandler::SendGetRemoteMemRequest(socket_, dst_endpoint_handle_, timeout_ms);
  HIXL_CHK_STATUS_RET(ret, "[HixlClient] SendGetRemoteMemRequest failed. fd=%d, remote_ep_handle=%" PRIu64, socket_,
                      dst_endpoint_handle_);
  std::vector<HixlMemDesc> mem_descs;
  ret = MemMsgHandler::RecvGetRemoteMemResponse(socket_, mem_descs, timeout_ms);
  HIXL_CHK_STATUS_RET(ret, "[HixlClient] RecvGetRemoteMemResponse failed. fd=%d, timeout=%u ms", socket_, timeout_ms);
  HIXL_LOGD("[HixlClient] Recv remote mem descs success. Count=%zu", mem_descs.size());
  ret = ImportRemoteMem(mem_descs, remote_mem_list, mem_tag_list, list_num);
  HIXL_CHK_STATUS_RET(ret, "[HixlClient] ImportRemoteMem failed. desc_count=%zu", mem_descs.size());
  HIXL_EVENT("[HixlClient] GetRemoteMem success. fd=%d, remote_ep_handle=%" PRIu64 ", imported=%u", socket_,
             dst_endpoint_handle_, *list_num);
  return SUCCESS;
}

namespace {
void FreeExportDesc(std::vector<HixlMemDesc> &desc_list) {
  for (auto &d : desc_list) {
    if (d.export_desc != nullptr && d.export_len > 0U) {
      std::free(d.export_desc);
      d.export_desc = nullptr;
      d.export_len = 0U;
    }
  }
  desc_list.clear();
}

Status ValidateExportDescList(const std::vector<HixlMemDesc> &desc_list) {
  for (const auto &d : desc_list) {
    if (d.export_desc == nullptr || d.export_len == 0U) {
      HIXL_LOGE(PARAM_INVALID,
                "[HixlClient] ValidateExportDescList failed! Invalid export_desc at"
                "ptr=%p, len=%u, total_count=%zu",
                d.export_desc, d.export_len, desc_list.size());
      return PARAM_INVALID;
    }
  }
  return SUCCESS;
}

Status AppendTagStorage(std::vector<std::vector<char>> &storage, const std::string &tag) {
  std::vector<char> buf(tag.size() + 1U, '\0');
  if (!tag.empty()) {
    errno_t rc = memcpy_s(buf.data(), buf.size(), tag.data(), tag.size());
    HIXL_CHK_BOOL_RET_STATUS(
        rc == EOK, FAILED,
        "[HixlClient] AppendTagStorage failed! memcpy_s error.tag: '%s', tag_len: %zu, dest_buf_size: %zu, rc: %d",
        tag.c_str(), tag.size(), buf.size(), static_cast<int32_t>(rc));
  }
  storage.emplace_back(std::move(buf));
  HIXL_LOGD("[HixlClient] AppendTagStorage success. tag: '%s', current_storage_size: %zu",
              tag.c_str(), storage.size());
  return SUCCESS;
}

void BuildTagPtrs(std::vector<std::vector<char>> &storage, std::vector<char*> &ptrs) {
  ptrs.clear();
  ptrs.reserve(storage.size());
  for (auto &s : storage) {
    ptrs.emplace_back(s.empty() ? nullptr : s.data());
  }
}

void CloseImportedBufs(EndPointHandle ep_handle, std::vector<HcommBuf> &bufs) {
  if (ep_handle == nullptr) {
    bufs.clear();
    return;
  }
  for (const auto &b : bufs) {
    HcommBuf tmp{};
    tmp.addr = b.addr;
    tmp.len = b.len;
    const HcclResult ret = HcommMemUnimport(ep_handle, &tmp);
    if (ret != HCCL_SUCCESS) {
      HIXL_LOGW("[HixlClient] HcommMemClose failed. addr=%p len=%" PRIu64 " ret=0x%X",
                tmp.addr, tmp.len, static_cast<uint32_t>(ret));
    }
  }
  bufs.clear();
}

void UnrecordAddrs(HixlMemStore &store, std::vector<void*> &addrs) {
  for (auto *addr : addrs) {
    if (addr == nullptr) {
      continue;
    }

    const Status ret = store.UnrecordMemory(true, addr);
    if (ret != SUCCESS) {
      HIXL_LOGW("[HixlClient] UnrecordMemory failed. addr=%p ret=%u", addr, static_cast<uint32_t>(ret));
    }
  }
  addrs.clear();
}

Status ImportOneDesc(ImportCtx &ctx, uint32_t idx, HixlMemDesc &desc) {
  HcommBuf buf{};
  Status ret = ctx.ep->MemImport(desc.export_desc, desc.export_len, buf);
  if (ret != SUCCESS) {
    HIXL_LOGE(ret, "[HixlClient] MemImport failed, idx=%u, tag=%s", idx, desc.tag.c_str());
    return ret;
  }
  ctx.imported.emplace_back(buf);

  HcclMem mem{};
  mem.type = desc.mem.type;
  mem.addr = desc.mem.addr;
  mem.size = desc.mem.size;
  ctx.mems.emplace_back(mem);
  ctx.tag_mem_map[desc.tag] = mem;
  HIXL_LOGD("[HixlClient] Imported mem[%u]: tag='%s', addr=%p, size=%llu", idx, desc.tag.c_str(), mem.addr, mem.size);
  ret = ctx.store->RecordMemory(true, mem.addr, static_cast<size_t>(mem.size));
  if (ret == SUCCESS) {
    ctx.recorded_addrs.emplace_back(mem.addr);
  } else {
    HIXL_LOGE(ret,
              "[HixlClient] RecordMemory(server) failed! This memory may have been registered. idx=%u, tag=%s, addr=%p, size=%llu",
              idx, desc.tag.c_str(), mem.addr, mem.size);
    return ret;
  }
  return AppendTagStorage(ctx.tag_storage, desc.tag);
}

Status ImportAllDescs(ImportCtx &ctx, std::vector<HixlMemDesc> &desc_list) {
  for (uint32_t i = 0; i < ctx.num; ++i) {
    Status ret = ImportOneDesc(ctx, i, desc_list[i]);
    if (ret != SUCCESS) {
      return ret;
    }
  }
  return SUCCESS;
}

}  // namespace

void HixlCSClient::FillOutputParams(ImportCtx &ctx, HcclMem **remote_mem_list, char ***mem_tag_list,
                                    uint32_t *list_num) {
  imported_remote_bufs_ = std::move(ctx.imported);
  recorded_remote_addrs_ = std::move(ctx.recorded_addrs);
  tag_mem_descs_ = std::move(ctx.tag_mem_map);
  remote_mems_out_ = std::move(ctx.mems);
  remote_tag_storage_.clear();
  remote_tag_ptrs_.clear();
  remote_tag_storage_ = std::move(ctx.tag_storage);
  BuildTagPtrs(remote_tag_storage_, remote_tag_ptrs_);
  *mem_tag_list = remote_tag_ptrs_.empty() ? nullptr : remote_tag_ptrs_.data();
  *remote_mem_list = remote_mems_out_.empty() ? nullptr : remote_mems_out_.data();
  *list_num = static_cast<uint32_t>(remote_mems_out_.size());
}

Status HixlCSClient::ImportRemoteMem(std::vector<HixlMemDesc> &desc_list,
                                     HcclMem **remote_mem_list,
                                     char ***mem_tag_list,
                                     uint32_t *list_num) {
  HIXL_MAKE_GUARD(free_export_desc, [&desc_list]() {
    FreeExportDesc(desc_list);
  });
  *list_num = static_cast<uint32_t>(desc_list.size());
  Status ret = ClearRemoteMemInfo();
  HIXL_CHK_STATUS_RET(ret, "[HixlClient] ClearRemoteMemInfo before ImportRemoteMem failed");
  if (*list_num == 0U) {
    HIXL_LOGI("[HixlClient] Remote mem list is empty, nothing to import.");
    return SUCCESS;
  }
  ret = ValidateExportDescList(desc_list);
  HIXL_CHK_STATUS_RET(ret, "[HixlClient] ValidateExportDescList failed");
  HIXL_CHECK_NOTNULL(src_endpoint_);
  EndPointHandle ep_handle = src_endpoint_->GetHandle();
  HIXL_CHK_BOOL_RET_STATUS(ep_handle != nullptr, FAILED, "[HixlClient] ImportRemoteMem: endpoint handle is null");
  ImportCtx ctx;
  ctx.ep = src_endpoint_.get();
  ctx.ep_handle = ep_handle;
  ctx.store = &mem_store_;
  ctx.num = *list_num;
  ctx.imported.reserve(ctx.num);
  ctx.recorded_addrs.reserve(ctx.num);
  ctx.mems.reserve(ctx.num);
  ctx.tag_storage.reserve(ctx.num);
  ret = ImportAllDescs(ctx, desc_list);
  if (ret != SUCCESS) {
    HIXL_LOGW("[HixlClient] RollbackImport triggered. Cleaning up %zu imported bufs.", ctx.imported.size());
    CloseImportedBufs(ctx.ep_handle, ctx.imported);
    return ret;
  }
  FillOutputParams(ctx, remote_mem_list, mem_tag_list, list_num);
  return SUCCESS;
}

Status HixlCSClient::ClearRemoteMemInfo() {
  EndPointHandle ep_handle = (src_endpoint_ != nullptr) ? src_endpoint_->GetHandle() : nullptr;
  const size_t buf_cnt = imported_remote_bufs_.size();
  const size_t addr_cnt = recorded_remote_addrs_.size();
  if (buf_cnt > 0U || addr_cnt > 0U) {
    HIXL_LOGI("[HixlClient] Cleaning up remote mem info. Bufs=%zu, Addrs=%zu", buf_cnt, addr_cnt);
  }
  if (!imported_remote_bufs_.empty()) {
    if (ep_handle != nullptr) {
      CloseImportedBufs(ep_handle, imported_remote_bufs_);
    } else {
      HIXL_LOGW("[HixlClient] ClearRemoteMemInfo: endpoint handle null, skip MemClose for %zu bufs",
                imported_remote_bufs_.size());
      imported_remote_bufs_.clear();
    }
  }
  if (!recorded_remote_addrs_.empty()) {
    UnrecordAddrs(mem_store_, recorded_remote_addrs_);
  }
  tag_mem_descs_.clear();
  remote_mems_out_.clear();
  remote_tag_ptrs_.clear();
  remote_tag_storage_.clear();
  {
    std::lock_guard<std::mutex> lk(ub_mu_);
    ub_remote_flag_inited_ = false;
    ub_remote_flag_addr_ = 0ULL;
    ub_remote_flag_size_ = 0ULL;
  }
  return SUCCESS;
}

Status HixlCSClient::Destroy() {
  HIXL_EVENT("[HixlClient] Destroy start. fd=%d, imported_bufs=%zu, recorded_addrs=%zu",
             socket_, imported_remote_bufs_.size(), recorded_remote_addrs_.size());

  std::lock_guard<std::mutex> lock(mutex_);
  Status first_error = SUCCESS;

  {
    std::lock_guard<std::mutex> lk(indices_mutex_);
    uint32_t live_cnt = 0U;
    for (size_t i = 0U; i < kFlagQueueSize; ++i) {
      if (live_handles_[i] != nullptr) {
        live_cnt += 1U;
      }
    }
    if (live_cnt > 0U) {
      HIXL_LOGW("[HixlClient] Destroy: %u legacy complete_handle still live. Force releasing them.", live_cnt);
      for (size_t i = 0U; i < kFlagQueueSize; ++i) {
        if (live_handles_[i] != nullptr) {
          delete live_handles_[i];
          live_handles_[i] = nullptr;
        }
      }
      top_index_ = 0U;
      for (size_t i = 0U; i < kFlagQueueSize; ++i) {
        available_indices_[i] = static_cast<int32_t>(i);
      }
      top_index_ = kFlagQueueSize;
    }
  }

  if (is_device_) {
    const uint32_t in_use = g_complete_pool.GetInUseCount();
    if (in_use != 0U) {
      HIXL_LOGE(FAILED,
                "[HixlClient] Destroy: %u UB slots still in use. "
                "Please QueryCompleteStatus until COMPLETED before Destroy.",
                in_use);
      return FAILED;
    }
    g_complete_pool.ReleaseRefAndDeinitIfNeeded();
    is_device_ = false;
    ub_device_id_ = -1;
  }
  Status ret = ClearRemoteMemInfo();
  if (ret != SUCCESS) {
    HIXL_LOGW("[HixlClient] ClearRemoteMemInfo failed. fd=%d, ret=%u",
              socket_, static_cast<uint32_t>(ret));
    if (first_error == SUCCESS) {
      first_error = ret;
    }
  }
  if (socket_ != -1) {
    HIXL_LOGI("[HixlClient] Closing socket. fd=%d", socket_);
    close(socket_);
    socket_ = -1;
  }
  if (src_endpoint_ != nullptr) {
    ret = src_endpoint_->Finalize();
    if (ret != SUCCESS) {
      HIXL_LOGW("[HixlClient] Finalize endpoint failed in Destroy. ep_handle=%p, ret=%u",
                (src_endpoint_ != nullptr) ? src_endpoint_->GetHandle() : nullptr,
                static_cast<uint32_t>(ret));
      if (first_error == SUCCESS) {
        first_error = ret;
      }
    }
    src_endpoint_.reset();
  }
  HIXL_EVENT("[HixlClient] Destroy done. first_error=%u", static_cast<uint32_t>(first_error));
  return first_error;
}
}  // namespace hixl
