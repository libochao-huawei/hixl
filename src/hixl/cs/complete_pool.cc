/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#include "complete_pool.h"

#include <cstdint>
#include <securec.h>
#include "runtime/rts/rts_device.h"
#include "common/hixl_log.h"
#include "endpoint.h"
#include "common/hixl_utils.h"
#include "common/scope_guard.h"

namespace hixl {
CompletePool &GetCompletePool() {
  static CompletePool pool;
  return pool;
}
namespace {

constexpr uint64_t kFlagInitValue = 0ULL;
constexpr uint64_t kFlagDoneValue = 1ULL;

constexpr uint32_t kNotifyCreateFlag = 0U;

constexpr rtDevResProcType_t kDefaultProcType = RT_PROCESS_CP1;
constexpr rtDevResType_t kDefaultResType = RT_RES_TYPE_STARS_NOTIFY_RECORD;

constexpr uint32_t kNotifyFlagBytes = static_cast<uint32_t>(sizeof(uint64_t));
constexpr const char *kUbLocalNotifyTagPrefix = "_hixl_ub_local_dev_flag";

}  // namespace

CompletePool::CompletePool()
    : ref_cnt_(0U),
      inited_(false),
      free_list_(),
      slots_(),
      endpoint_(nullptr),
      init_device_id_(-1),
      init_engine_(CommEngine::COMM_ENGINE_RESERVED),
      init_thread_num_(0U),
      init_notify_num_per_thread_(0U) {
  for (uint32_t i = 0U; i < kMaxSlots; ++i) {
    Slot &slot = slots_[i];
    slot.in_use = false;
    slot.ctx = nullptr;
    slot.stream = nullptr;
    slot.thread = 0U;
    slot.notify = nullptr;
    slot.notify_addr = nullptr;
    slot.host_flag = nullptr;
    slot.notify_mem_handle = nullptr;
    slot.notify_tag.fill('\0');
  }
}

CompletePool::~CompletePool() {
  std::lock_guard<std::mutex> lock(mu_);
  DeinitAllSlotsLocked();
}

bool CompletePool::IsInitedParamsSame(int32_t device_id, CommEngine engine, uint32_t thread_num,
                                      uint32_t notify_num_per_thread) const {
  return (device_id == init_device_id_) && (engine == init_engine_) && (thread_num == init_thread_num_) &&
         (notify_num_per_thread == init_notify_num_per_thread_);
}

void CompletePool::SaveInitParams(int32_t device_id, CommEngine engine, uint32_t thread_num,
                                  uint32_t notify_num_per_thread, Endpoint *endpoint) {
  endpoint_ = endpoint;
  init_device_id_ = device_id;
  init_engine_ = engine;
  init_thread_num_ = thread_num;
  init_notify_num_per_thread_ = notify_num_per_thread;
}

void CompletePool::ResetInitParamsLocked() {
  endpoint_ = nullptr;
  init_device_id_ = -1;
  init_engine_ = CommEngine::COMM_ENGINE_RESERVED;
  init_thread_num_ = 0U;
  init_notify_num_per_thread_ = 0U;
}

void CompletePool::InitFreeListLocked() {
  free_list_.clear();
  free_list_.reserve(kMaxSlots);
  for (uint32_t i = 0U; i < kMaxSlots; ++i) {
    free_list_.push_back(i);
  }
}

Status CompletePool::GetCurrentAclContext(aclrtContext *old_ctx) const {
  HIXL_CHECK_NOTNULL(old_ctx);

  HIXL_CHK_ACL_RET(aclrtGetCurrentContext(old_ctx));
  return SUCCESS;
}

void CompletePool::RestoreAclContext(aclrtContext old_ctx) const {
  if (old_ctx == nullptr) {
    return;
  }
  HIXL_CHK_ACL(aclrtSetCurrentContext(old_ctx), "restore acl context failed");
}

Status CompletePool::SwitchDeviceAndNeedRestore(int32_t target_device_id, int32_t *old_device_id,
                                                bool *need_restore) const {
  HIXL_CHECK_NOTNULL(old_device_id);
  HIXL_CHECK_NOTNULL(need_restore);

  *old_device_id = -1;
  *need_restore = false;

  HIXL_CHK_ACL_RET(aclrtGetDevice(old_device_id));
  if (*old_device_id == target_device_id) {
    return SUCCESS;
  }

  HIXL_CHK_ACL_RET(aclrtSetDevice(target_device_id));
  *need_restore = true;
  return SUCCESS;
}

Status CompletePool::AddRefAndInitIfNeeded(int32_t device_id, CommEngine engine, uint32_t thread_num,
                                           uint32_t notify_num_per_thread, Endpoint *endpoint) {
  std::lock_guard<std::mutex> lock(mu_);
  HIXL_CHECK_NOTNULL(endpoint);

  if (inited_) {
    if (!IsInitedParamsSame(device_id, engine, thread_num, notify_num_per_thread)) {
      HIXL_LOGE(PARAM_INVALID,
                "[CompletePool] AddRef with different params. "
                "inited(dev=%d,engine=%d,thread=%u,notify=%u) got(dev=%d,engine=%d,thread=%u,notify=%u)",
                init_device_id_, static_cast<int32_t>(init_engine_), init_thread_num_, init_notify_num_per_thread_,
                device_id, static_cast<int32_t>(engine), thread_num, notify_num_per_thread);
      return PARAM_INVALID;
    }
    endpoint_ = endpoint;
    ref_cnt_ += 1U;
    return SUCCESS;
  }

  SaveInitParams(device_id, engine, thread_num, notify_num_per_thread, endpoint);
  ref_cnt_ += 1U;

  Status ret = InitAllSlotsLocked(device_id, engine, thread_num, notify_num_per_thread);
  if (ret != SUCCESS) {
    ref_cnt_ -= 1U;
    ResetInitParamsLocked();
    return ret;
  }

  inited_ = true;
  return SUCCESS;
}

void CompletePool::ReleaseRefAndDeinitIfNeeded() {
  std::lock_guard<std::mutex> lock(mu_);
  if (ref_cnt_ == 0U) {
    return;
  }
  ref_cnt_ -= 1U;
  if (ref_cnt_ != 0U) {
    return;
  }
  DeinitAllSlotsLocked();
}

uint32_t CompletePool::GetInUseCount() const {
  std::lock_guard<std::mutex> lock(mu_);
  uint32_t count = 0U;
  for (uint32_t i = 0U; i < kMaxSlots; ++i) {
    if (slots_[i].in_use) {
      count += 1U;
    }
  }
  return count;
}

Status CompletePool::Acquire(SlotHandle *handle) {
  HIXL_CHECK_NOTNULL(handle);
  std::lock_guard<std::mutex> lock(mu_);

  if (!inited_) {
    return FAILED;
  }
  if (free_list_.empty()) {
    return RESOURCE_EXHAUSTED;
  }

  const uint32_t idx = free_list_.back();
  free_list_.pop_back();

  Slot &slot = slots_[idx];
  slot.in_use = true;

  handle->slot_index = idx;
  handle->ctx = slot.ctx;
  handle->stream = slot.stream;
  handle->thread = slot.thread;
  handle->notify = slot.notify;
  handle->host_flag = slot.host_flag;
  handle->notify_addr = slot.notify_addr;
  return SUCCESS;
}

void CompletePool::Release(uint32_t slot_index) {
  std::lock_guard<std::mutex> lock(mu_);
  if (slot_index >= kMaxSlots) {
    return;
  }

  Slot &slot = slots_[slot_index];
  if (!slot.in_use) {
    return;
  }

  if (slot.host_flag != nullptr) {
    *(static_cast<uint64_t *>(slot.host_flag)) = kFlagInitValue;
  }

  slot.in_use = false;
  free_list_.push_back(slot_index);
}

bool CompletePool::IsComplete(const SlotHandle &handle) const {
  if (handle.host_flag == nullptr) {
    return false;
  }
  return (*(static_cast<const uint64_t *>(handle.host_flag)) == kFlagDoneValue);
}

void CompletePool::ResetHostFlag(const SlotHandle &handle) const {
  if (handle.host_flag == nullptr) {
    return;
  }
  *(static_cast<uint64_t *>(handle.host_flag)) = kFlagInitValue;
}

Status CompletePool::InitAllSlotsLocked(int32_t device_id, CommEngine engine, uint32_t thread_num,
                                        uint32_t notify_num_per_thread) {
  InitFreeListLocked();

  aclrtContext old_ctx = nullptr;
  HIXL_CHK_STATUS_RET(GetCurrentAclContext(&old_ctx), "[CompletePool] GetCurrentAclContext failed");

  HIXL_DISMISSABLE_GUARD(ctx_restore, [&]() { RestoreAclContext(old_ctx); });

  for (uint32_t i = 0U; i < kMaxSlots; ++i) {
    Status ret = InitOneSlotLocked(slots_[i], i, device_id, engine, thread_num, notify_num_per_thread);
    if (ret != SUCCESS) {
      DestroySlotLocked(slots_[i]);
      return ret;
    }
  }
  return SUCCESS;
}

Status CompletePool::InitOneSlotLocked(Slot &slot, uint32_t slot_index, int32_t device_id, CommEngine engine,
                                       uint32_t thread_num, uint32_t notify_num_per_thread) {
  int32_t old_device_id = -1;
  bool need_restore = false;

  HIXL_CHK_STATUS_RET(SwitchDeviceAndNeedRestore(device_id, &old_device_id, &need_restore),
                      "[CompletePool] SwitchDevice failed");

  HIXL_DISMISSABLE_GUARD(dev_restore, [&]() {
    if (need_restore) {
      HIXL_CHK_ACL(aclrtSetDevice(old_device_id));
    }
  });

  HIXL_CHK_STATUS_RET(EnsureContextLocked(slot, device_id), "[CompletePool] EnsureContextLocked failed");
  HIXL_CHK_STATUS_RET(EnsureStreamLocked(slot), "[CompletePool] EnsureStreamLocked failed");
  HIXL_CHK_STATUS_RET(EnsureThreadLocked(slot, engine, thread_num, notify_num_per_thread),
                      "[CompletePool] EnsureThreadLocked failed");
  HIXL_CHK_STATUS_RET(EnsureNotifyRecordLocked(slot, slot_index, device_id),
                      "[CompletePool] EnsureNotifyRecordLocked failed");
  HIXL_CHK_STATUS_RET(EnsurePinnedHostFlagLocked(slot), "[CompletePool] EnsurePinnedHostFlagLocked failed");

  *(static_cast<uint64_t *>(slot.host_flag)) = kFlagInitValue;
  return SUCCESS;
}

Status CompletePool::EnsureNotifyRecordLocked(Slot &slot, uint32_t slot_index, int32_t device_id) {
  if ((slot.notify != nullptr) && (slot.notify_addr != nullptr) && (slot.notify_mem_handle != nullptr)) {
    return SUCCESS;
  }
  if (endpoint_ == nullptr) {
    HIXL_LOGE(FAILED, "[CompletePool] endpoint_ is null, cannot register notify record");
    return FAILED;
  }

  ResetNotifyResourcesLocked(slot);

  uint32_t notify_id = 0U;
  HIXL_CHK_STATUS_RET(CreateNotifyLocked(slot, device_id, &notify_id), "[CompletePool] CreateNotifyLocked failed");

  void *notify_addr = nullptr;
  HIXL_CHK_STATUS_RET(GetNotifyAddrLocked(notify_id, &notify_addr), "[CompletePool] GetNotifyAddrLocked failed");
  slot.notify_addr = notify_addr;

  std::array<char, 64> tag{};
  HIXL_CHK_STATUS_RET(BuildNotifyTagLocked(slot_index, &tag), "[CompletePool] BuildNotifyTagLocked failed");
  slot.notify_tag = tag;

  HIXL_CHK_STATUS_RET(RegisterNotifyMemLocked(slot, slot.notify_tag.data(), slot.notify_addr),
                      "[CompletePool] RegisterNotifyMemLocked failed");

  return SUCCESS;
}

void CompletePool::ResetNotifyResourcesLocked(Slot &slot) {
  if (slot.notify != nullptr) {
    HIXL_CHK_ACL(rtNotifyDestroy(slot.notify));
    slot.notify = nullptr;
  }

  if (slot.notify_mem_handle != nullptr) {
    (void)endpoint_->DeregisterMem(slot.notify_mem_handle);
    slot.notify_mem_handle = nullptr;
  }

  slot.notify_addr = nullptr;
  slot.notify_tag.fill('\0');
}

Status CompletePool::CreateNotifyLocked(Slot &slot, int32_t device_id, uint32_t *notify_id) {
  HIXL_CHECK_NOTNULL(notify_id);
  *notify_id = 0U;

  HIXL_CHK_ACL_RET(rtNotifyCreateWithFlag(device_id, &slot.notify, kNotifyCreateFlag));
  HIXL_CHK_ACL_RET(rtGetNotifyID(slot.notify, notify_id));
  HIXL_LOGI("[JZY] notify_id=%u", notify_id);
  return SUCCESS;
}

Status CompletePool::GetNotifyAddrLocked(uint32_t notify_id, void **notify_addr) const {
  HIXL_CHECK_NOTNULL(notify_addr);
  *notify_addr = nullptr;

  rtDevResInfo res_info{};
  res_info.dieId = 0U;
  res_info.procType = kDefaultProcType;
  res_info.resType = kDefaultResType;
  res_info.resId = notify_id;
  res_info.flag = 0U;

  uint64_t addr = 0;
  uint32_t len = 0U;
  rtDevResAddrInfo addr_info{};
  addr_info.resAddress = &addr;
  addr_info.len = &len;

  HIXL_LOGI("[CompletePool] rtDevResInfo: dieId=%u, procType=%d, resType=%d, resId=%u, flag=%u",

            res_info.dieId, static_cast<int>(res_info.procType), static_cast<int>(res_info.resType), res_info.resId,
            res_info.flag);
  HIXL_LOGI("rtGetDevResAddress start");
  HIXL_CHK_ACL_RET(rtGetDevResAddress(&res_info, &addr_info));
  HIXL_LOGI("rtGetDevResAddress end");
  HIXL_LOGI("[CompletePool] rtDevResInfo: dieId=%u, procType=%d, resType=%d, resId=%u, flag=%u", res_info.dieId,
            static_cast<int>(res_info.procType), static_cast<int>(res_info.resType), res_info.resId, res_info.flag);

  if (addr_info.resAddress != nullptr && addr_info.len != nullptr) {
    HIXL_LOGI("[HixlClient] rtDevResAddrInfo: resAddress=%p[0]=0x%016lx, len=%p, *len=%u", addr_info.resAddress,
              *addr_info.resAddress,  // 打印指针指向的第一个64位值
              addr_info.len, *addr_info.len);
  } else {
    HIXL_LOGI("[HixlClient] rtDevResAddrInfo: resAddress=%p, len=%p", addr_info.resAddress, addr_info.len);
  }

  HIXL_CHK_BOOL_RET_STATUS(addr_info.resAddress != nullptr, FAILED,
                           "[CompletePool] rtGetDevResAddress returned null. notify_id=%u", notify_id);

  //*notify_addr = addr_info.resAddress;
  uintptr_t addr_value = static_cast<uintptr_t>(*addr_info.resAddress);
  *notify_addr = reinterpret_cast<void *>(addr_value);
  return SUCCESS;
}

Status CompletePool::BuildNotifyTagLocked(uint32_t slot_index, std::array<char, 64> *tag) const {
  HIXL_CHECK_NOTNULL(tag);
  tag->fill('\0');

  const errno_t nret =
      snprintf_s(tag->data(), tag->size(), tag->size() - 1U, "%s_%03u", kUbLocalNotifyTagPrefix, slot_index);
  HIXL_CHK_BOOL_RET_STATUS(nret >= 0, FAILED, "[CompletePool] snprintf_s notify tag failed. slot=%u", slot_index);
  return SUCCESS;
}

Status CompletePool::RegisterNotifyMemLocked(Slot &slot, const char *tag, void *notify_addr) {
  HIXL_CHECK_NOTNULL(tag);
  HIXL_CHECK_NOTNULL(notify_addr);

  HcommMem mem{};
  mem.type = HCCL_MEM_TYPE_DEVICE;
  mem.addr = notify_addr;
  mem.size = static_cast<u64>(kNotifyFlagBytes);
  HIXL_LOGI("[JZY] mem.addr=%p", mem.addr);
  MemHandle mem_handle = nullptr;
  HIXL_CHK_STATUS_RET(endpoint_->RegisterMem(tag, mem, mem_handle),
                      "[CompletePool] RegisterMem(notify) failed. tag=%s addr=%p", tag, notify_addr);

  slot.notify_mem_handle = mem_handle;
  return SUCCESS;
}

void CompletePool::DeinitAllSlotsLocked() {
  int32_t old_device_id = -1;
  bool need_restore = false;

  if (init_device_id_ >= 0) {
    (void)SwitchDeviceAndNeedRestore(init_device_id_, &old_device_id, &need_restore);
  }

  for (uint32_t i = 0U; i < kMaxSlots; ++i) {
    DestroySlotLocked(slots_[i]);
    slots_[i].in_use = false;
  }

  if (need_restore) {
    HIXL_CHK_ACL(aclrtSetDevice(old_device_id));
  }

  free_list_.clear();
  inited_ = false;
  ResetInitParamsLocked();
}

Status CompletePool::EnsureContextLocked(Slot &slot, int32_t device_id) {
  if (slot.ctx != nullptr) {
    return SUCCESS;
  }

  aclrtContext ctx = nullptr;
  HIXL_CHK_ACL_RET(aclrtCreateContext(&ctx, device_id));

  aclError set_ret = aclrtSetCurrentContext(ctx);
  if (set_ret != ACL_SUCCESS) {
    HIXL_CHK_ACL(aclrtDestroyContext(ctx), "destroy ctx after set current failed");
    REPORT_INNER_ERR_MSG("E19999", "Call %s fail, ret: 0x%X", "aclrtSetCurrentContext", static_cast<uint32_t>(set_ret));
    HIXL_LOGE(FAILED, "Call acl api failed, ret: 0x%X", static_cast<uint32_t>(set_ret));
    return FAILED;
  }

  slot.ctx = ctx;
  return SUCCESS;
}

Status CompletePool::EnsureStreamLocked(Slot &slot) {
  if (slot.stream != nullptr) {
    return SUCCESS;
  }

  HIXL_CHK_ACL_RET(aclrtSetCurrentContext(slot.ctx));

  aclrtStream stream = nullptr;
  HIXL_CHK_ACL_RET(aclrtCreateStream(&stream));

  slot.stream = stream;
  return SUCCESS;
}

Status CompletePool::EnsureThreadLocked(Slot &slot, CommEngine engine, uint32_t thread_num,
                                        uint32_t notify_num_per_thread) {
  if (slot.thread != 0U) {
    return SUCCESS;
  }

  // HcommThreadAlloc returns HcclResult, convert to Status via macro you already have.
  HIXL_CHK_HCCL_RET(HcommThreadAlloc(engine, thread_num, notify_num_per_thread, &slot.thread));
  return SUCCESS;
}

Status CompletePool::EnsurePinnedHostFlagLocked(Slot &slot) {
  if (slot.host_flag != nullptr) {
    return SUCCESS;
  }

  void *p = nullptr;
  HIXL_CHK_ACL_RET(rtMallocHost(&p, sizeof(uint64_t), HCCL));
  HIXL_CHK_BOOL_RET_STATUS(p != nullptr, FAILED, "[CompletePool] rtMallocHost returned null");

  slot.host_flag = p;
  *(static_cast<uint64_t *>(slot.host_flag)) = kFlagInitValue;
  return SUCCESS;
}

void CompletePool::DestroySlotLocked(Slot &slot) {
  if (slot.notify_mem_handle != nullptr) {
    if (endpoint_ != nullptr) {
      HIXL_CHK_STATUS(endpoint_->DeregisterMem(slot.notify_mem_handle), "[CompletePool] DeregisterMem failed. tag=%s",
                      slot.notify_tag.data());
    }
    slot.notify_mem_handle = nullptr;
  }

  if (slot.notify != nullptr) {
    HIXL_CHK_ACL(rtNotifyDestroy(slot.notify));
    slot.notify = nullptr;
  }

  if (slot.thread != 0U) {
    // HIXL_CHK_HCCL(HcommThreadFree(slot.thread));
    slot.thread = 0U;
  }

  if (slot.stream != nullptr) {
    HIXL_CHK_ACL(aclrtDestroyStream(slot.stream), "destroy stream failed");
    slot.stream = nullptr;
  }

  if (slot.ctx != nullptr) {
    HIXL_CHK_ACL(aclrtDestroyContext(slot.ctx), "destroy context failed");
    slot.ctx = nullptr;
  }

  if (slot.host_flag != nullptr) {
    HIXL_CHK_ACL(rtFreeHost(slot.host_flag));
    slot.host_flag = nullptr;
  }

  slot.notify_addr = nullptr;
  slot.notify_tag.fill('\0');
}

}  // namespace hixl
