#include "complete_pool.h"

#include <cinttypes>
#include <cstdint>
#include <securec.h>

#include "runtime/rts/rts_device.h"
#include "common/hixl_log.h"
#include "endpoint.h"
#include "common/scope_guard.h"

namespace hixl {
namespace {

constexpr uint64_t kFlagInitValue = 0ULL;
constexpr uint64_t kFlagDoneValue = 1ULL;

constexpr uint32_t kNotifyCreateFlag = 0U;

constexpr rtDevResProcType_t kDefaultProcType = RT_PROCESS_CP1;
constexpr rtDevResType_t kDefaultResType = RT_RES_TYPE_STARS_NOTIFY_RECORD;

constexpr uint32_t kNotifyFlagBytes = static_cast<uint32_t>(sizeof(uint64_t));
constexpr const char *kUbLocalDevFlagTagPrefix = "_hixl_ub_local_dev_flag";

inline HcclResult SwitchDeviceAndGuardRestore(int32_t targetDeviceId,
                                             int32_t &oldDeviceId,
                                             bool &needRestore) {
  oldDeviceId = -1;
  needRestore = false;

  rtError_t rret = rtGetDevice(&oldDeviceId);
  if (rret != RT_ERROR_NONE) {
    return HCCL_E_INTERNAL;
  }

  if (oldDeviceId == targetDeviceId) {
    return HCCL_SUCCESS;
  }

  rret = rtSetDevice(targetDeviceId);
  if (rret != RT_ERROR_NONE) {
    return HCCL_E_INTERNAL;
  }

  needRestore = true;
  return HCCL_SUCCESS;
}

inline bool SameInitParams(int32_t deviceId,
                           CommEngine engine,
                           uint32_t threadNum,
                           uint32_t notifyNumPerThread,
                           int32_t initDeviceId,
                           CommEngine initEngine,
                           uint32_t initThreadNum,
                           uint32_t initNotifyNumPerThread) {
  if (deviceId != initDeviceId) {
    return false;
  }
  if (engine != initEngine) {
    return false;
  }
  if (threadNum != initThreadNum) {
    return false;
  }
  if (notifyNumPerThread != initNotifyNumPerThread) {
    return false;
  }
  return true;
}

}  // namespace

CompletePool::CompletePool()
    : refCnt_(0U),
      inited_(false),
      freeList_(),
      slots_(),
      endpoint_(nullptr),
      initDeviceId_(-1),
      initEngine_(CommEngine::COMM_ENGINE_RESERVED),
      initThreadNum_(0U),
      initNotifyNumPerThread_(0U) {
  for (uint32_t i = 0U; i < kMaxSlots; ++i) {
    Slot &s = slots_[i];
    s.inUse = false;
    s.ctx = nullptr;
    s.stream = nullptr;
    s.thread = nullptr;
    s.notify = nullptr;
    s.devFlagAddr = 0ULL;
    s.hostFlag = nullptr;
    s.notifyMemHandle = nullptr;
    s.notifyTag.fill('\0');
  }
}

CompletePool::~CompletePool() {
  std::lock_guard<std::mutex> lk(mu_);
  this->DeinitAllSlotsLocked();
}

HcclResult CompletePool::AddRefAndInitIfNeeded(int32_t deviceId,
                                               CommEngine engine,
                                               uint32_t threadNum,
                                               uint32_t notifyNumPerThread,
                                               Endpoint *endpoint) {
  std::lock_guard<std::mutex> lk(mu_);

  if (endpoint == nullptr) {
    return HCCL_E_PARA;
  }

  if (inited_) {
    bool ok = SameInitParams(deviceId,
                             engine,
                             threadNum,
                             notifyNumPerThread,
                             initDeviceId_,
                             initEngine_,
                             initThreadNum_,
                             initNotifyNumPerThread_);
    if (!ok) {
      HIXL_LOGE(FAILED,
                "[CompletePool] AddRef called with different init params. "
                "inited(dev=%d,engine=%d,thread=%u,notify=%u) but got(dev=%d,engine=%d,thread=%u,notify=%u)",
                initDeviceId_,
                static_cast<int32_t>(initEngine_),
                initThreadNum_,
                initNotifyNumPerThread_,
                deviceId,
                static_cast<int32_t>(engine),
                threadNum,
                notifyNumPerThread);
      return HCCL_E_PARA;
    }

    endpoint_ = endpoint;
    refCnt_ += 1U;
    return HCCL_SUCCESS;
  }

  endpoint_ = endpoint;
  initDeviceId_ = deviceId;
  initEngine_ = engine;
  initThreadNum_ = threadNum;
  initNotifyNumPerThread_ = notifyNumPerThread;

  refCnt_ += 1U;

  HcclResult ret = this->InitAllSlotsLocked(deviceId, engine, threadNum, notifyNumPerThread);
  if (ret != HCCL_SUCCESS) {
    refCnt_ -= 1U;
    endpoint_ = nullptr;
    initDeviceId_ = -1;
    initEngine_ = CommEngine::COMM_ENGINE_RESERVED;
    initThreadNum_ = 0U;
    initNotifyNumPerThread_ = 0U;
    return ret;
  }

  inited_ = true;
  return HCCL_SUCCESS;
}

uint32_t CompletePool::GetInUseCount() const {
  std::lock_guard<std::mutex> lk(mu_);
  uint32_t cnt = 0U;
  for (uint32_t i = 0U; i < kMaxSlots; ++i) {
    if (slots_[i].inUse) {
      cnt += 1U;
    }
  }
  return cnt;
}

void CompletePool::ReleaseRefAndDeinitIfNeeded() {
  std::lock_guard<std::mutex> lk(mu_);

  if (refCnt_ == 0U) {
    return;
  }

  refCnt_ -= 1U;

  if (refCnt_ != 0U) {
    return;
  }

  this->DeinitAllSlotsLocked();
}

HcclResult CompletePool::Acquire(SlotHandle *handle) {
  if (handle == nullptr) {
    return HCCL_E_PARA;
  }

  std::lock_guard<std::mutex> lk(mu_);

  if (!inited_) {
    return HCCL_E_INTERNAL;
  }

  if (freeList_.empty()) {
    return HCCL_E_INTERNAL;
  }

  uint32_t idx = freeList_.back();
  freeList_.pop_back();

  Slot &s = slots_[idx];
  s.inUse = true;

  handle->slotIndex = idx;
  handle->ctx = s.ctx;
  handle->stream = s.stream;
  handle->thread = s.thread;
  handle->notify = s.notify;
  handle->hostFlag = s.hostFlag;
  handle->devFlagAddr = s.devFlagAddr;

  return HCCL_SUCCESS;
}

void CompletePool::Release(uint32_t slotIndex) {
  std::lock_guard<std::mutex> lk(mu_);

  if (slotIndex >= kMaxSlots) {
    return;
  }

  Slot &s = slots_[slotIndex];
  if (!s.inUse) {
    return;
  }

  if (s.hostFlag != nullptr) {
    *(s.hostFlag) = kFlagInitValue;
  }

  s.inUse = false;
  freeList_.push_back(slotIndex);
}

bool CompletePool::IsComplete(const SlotHandle &handle) const {
  if (handle.hostFlag == nullptr) {
    return false;
  }

  uint64_t v = *(handle.hostFlag);
  if (v == kFlagDoneValue) {
    return true;
  }

  return false;
}

void CompletePool::ResetHostFlag(const SlotHandle &handle) const {
  if (handle.hostFlag == nullptr) {
    return;
  }
  *(handle.hostFlag) = kFlagInitValue;
}

HcclResult CompletePool::InitAllSlotsLocked(int32_t deviceId,
                                            CommEngine engine,
                                            uint32_t threadNum,
                                            uint32_t notifyNumPerThread) {
  freeList_.clear();
  freeList_.reserve(kMaxSlots);

  for (uint32_t i = 0U; i < kMaxSlots; ++i) {
    freeList_.push_back(i);
  }

  // ===== 保存/恢复调用者线程 ctx（关键）=====
  aclrtContext old_ctx = nullptr;
  aclError aerr = aclrtGetCurrentContext(&old_ctx);
  if (aerr != ACL_SUCCESS) {
    HIXL_LOGE(FAILED, "[CompletePool] aclrtGetCurrentContext failed. ret=%d", static_cast<int32_t>(aerr));
    return HCCL_E_INTERNAL;
  }
  HIXL_DISMISSABLE_GUARD(ctx_restore_guard, [&]() {
    if (old_ctx != nullptr) {
      (void)aclrtSetCurrentContext(old_ctx);
    }
  });

  for (uint32_t i = 0U; i < kMaxSlots; ++i) {
    Slot &s = slots_[i];

    int32_t oldDevId = -1;
    bool needRestore = false;
    HcclResult sw = SwitchDeviceAndGuardRestore(deviceId, oldDevId, needRestore);
    if (sw != HCCL_SUCCESS) {
      return sw;
    }

    HcclResult ret = this->EnsureContextLocked(s, deviceId);
    if (ret != HCCL_SUCCESS) {
      if (needRestore) {
        (void)rtSetDevice(oldDevId);
      }
      this->DestroySlotLocked(s);
      return ret;
    }

    // IMPORTANT: stream must be created inside slot.ctx
    ret = this->EnsureStreamLocked(s);
    if (ret != HCCL_SUCCESS) {
      if (needRestore) {
        (void)rtSetDevice(oldDevId);
      }
      this->DestroySlotLocked(s);
      return ret;
    }

    ret = this->EnsureThreadLocked(s, engine, threadNum, notifyNumPerThread);
    if (ret != HCCL_SUCCESS) {
      if (needRestore) {
        (void)rtSetDevice(oldDevId);
      }
      this->DestroySlotLocked(s);
      return ret;
    }

    ret = this->EnsureNotifyAndDevFlagLocked(s, i, deviceId);
    if (ret != HCCL_SUCCESS) {
      if (needRestore) {
        (void)rtSetDevice(oldDevId);
      }
      this->DestroySlotLocked(s);
      return ret;
    }

    ret = this->EnsurePinnedHostFlagLocked(s);
    if (ret != HCCL_SUCCESS) {
      if (needRestore) {
        (void)rtSetDevice(oldDevId);
      }
      this->DestroySlotLocked(s);
      return ret;
    }

    if (s.hostFlag != nullptr) {
      *(s.hostFlag) = kFlagInitValue;
    }

    if (needRestore) {
      (void)rtSetDevice(oldDevId);
    }
  }

  // 正常返回时也会触发 guard 恢复 old_ctx
  HIXL_DISMISS_GUARD(ctx_restore_guard);
  return HCCL_SUCCESS;
}

void CompletePool::DeinitAllSlotsLocked() {
  int32_t oldDevId = -1;
  bool needRestore = false;

  if (initDeviceId_ >= 0) {
    (void)SwitchDeviceAndGuardRestore(initDeviceId_, oldDevId, needRestore);
  }

  for (uint32_t i = 0U; i < kMaxSlots; ++i) {
    this->DestroySlotLocked(slots_[i]);
    slots_[i].inUse = false;
  }

  if (needRestore) {
    (void)rtSetDevice(oldDevId);
  }

  freeList_.clear();
  inited_ = false;
  endpoint_ = nullptr;
  initDeviceId_ = -1;
  initEngine_ = CommEngine::COMM_ENGINE_RESERVED;
  initThreadNum_ = 0U;
  initNotifyNumPerThread_ = 0U;
}

HcclResult CompletePool::EnsureContextLocked(Slot &slot, int32_t deviceId) {
  if (slot.ctx != nullptr) {
    return HCCL_SUCCESS;
  }

  aclrtContext ctx = nullptr;
  aclError aerr = aclrtCreateContext(&ctx, deviceId);
  if (aerr != ACL_SUCCESS) {
    HIXL_LOGE(FAILED, "[CompletePool] aclrtCreateContext failed. devId=%d ret=%d",
              deviceId,
              static_cast<int32_t>(aerr));
    return HCCL_E_INTERNAL;
  }

  // set current so that subsequent resource creation is bound to this ctx
  aerr = aclrtSetCurrentContext(ctx);
  if (aerr != ACL_SUCCESS) {
    (void)aclrtDestroyContext(ctx);
    HIXL_LOGE(FAILED, "[CompletePool] aclrtSetCurrentContext failed. devId=%d ret=%d",
              deviceId,
              static_cast<int32_t>(aerr));
    return HCCL_E_INTERNAL;
  }

  slot.ctx = ctx;
  return HCCL_SUCCESS;
}

HcclResult CompletePool::EnsureStreamLocked(Slot &slot) {
  if (slot.stream != nullptr) {
    return HCCL_SUCCESS;
  }

  // stream must be created under slot.ctx
  aclError aerr = aclrtSetCurrentContext(slot.ctx);
  if (aerr != ACL_SUCCESS) {
    HIXL_LOGE(FAILED, "[CompletePool] aclrtSetCurrentContext before CreateStream failed. ret=%d",
              static_cast<int32_t>(aerr));
    return HCCL_E_INTERNAL;
  }

  aclrtStream stm = nullptr;
  aerr = aclrtCreateStream(&stm);
  if (aerr != ACL_SUCCESS) {
    HIXL_LOGE(FAILED, "[CompletePool] aclrtCreateStream failed. ret=%d", static_cast<int32_t>(aerr));
    return HCCL_E_INTERNAL;
  }

  slot.stream = stm;
  return HCCL_SUCCESS;
}

HcclResult CompletePool::EnsureThreadLocked(Slot &slot,
                                            CommEngine engine,
                                            uint32_t threadNum,
                                            uint32_t notifyNumPerThread) {
  if (slot.thread != nullptr) {
    return HCCL_SUCCESS;
  }

  HcclResult ret = HcommThreadAlloc(engine, threadNum, notifyNumPerThread, &slot.thread);
  if (ret != HCCL_SUCCESS) {
    HIXL_LOGE(FAILED, "[CompletePool] HcommThreadAlloc failed. ret=0x%X",
              static_cast<uint32_t>(ret));
    return ret;
  }

  return HCCL_SUCCESS;
}

HcclResult CompletePool::EnsurePinnedHostFlagLocked(Slot &slot) {
  if (slot.hostFlag != nullptr) {
    return HCCL_SUCCESS;
  }

  void *p = nullptr;
  rtError_t rret = rtMallocHost(&p, sizeof(uint64_t), HCCL);
  if (rret != RT_ERROR_NONE || p == nullptr) {
    HIXL_LOGE(FAILED, "[CompletePool] rtMallocHost(hostFlag) failed. ret=%d",
              static_cast<int32_t>(rret));
    return HCCL_E_INTERNAL;
  }

  slot.hostFlag = static_cast<uint64_t *>(p);
  *(slot.hostFlag) = kFlagInitValue;
  return HCCL_SUCCESS;
}

HcclResult CompletePool::EnsureNotifyAndDevFlagLocked(Slot &slot, uint32_t slot_index, int32_t deviceId) {
  if (slot.notify != nullptr && slot.devFlagAddr != 0ULL && slot.notifyMemHandle != nullptr) {
    return HCCL_SUCCESS;
  }

  if (endpoint_ == nullptr) {
    HIXL_LOGE(FAILED, "[CompletePool] endpoint_ is null, cannot RegisterMem for notify flag");
    return HCCL_E_INTERNAL;
  }

  if (slot.notify != nullptr) {
    (void)rtNotifyDestroy(slot.notify);
    slot.notify = nullptr;
  }

  if (slot.notifyMemHandle != nullptr) {
    (void)endpoint_->DeregisterMem(slot.notifyMemHandle);
    slot.notifyMemHandle = nullptr;
  }

  slot.devFlagAddr = 0ULL;
  slot.notifyTag.fill('\0');

  rtError_t rret = rtNotifyCreateWithFlag(deviceId, &slot.notify, kNotifyCreateFlag);
  if (rret != RT_ERROR_NONE) {
    HIXL_LOGE(FAILED, "[CompletePool] rtNotifyCreateWithFlag failed. devId=%d ret=%d",
              deviceId,
              static_cast<int32_t>(rret));
    return HCCL_E_INTERNAL;
  }

  uint32_t notifyId = 0U;
  rret = rtGetNotifyID(slot.notify, &notifyId);
  if (rret != RT_ERROR_NONE) {
    HIXL_LOGE(FAILED, "[CompletePool] rtGetNotifyID failed. ret=%d",
              static_cast<int32_t>(rret));
    return HCCL_E_INTERNAL;
  }

  rtDevResInfo resInfo{};
  resInfo.dieId = 0U;
  resInfo.procType = kDefaultProcType;
  resInfo.resType = kDefaultResType;
  resInfo.resId = notifyId;
  resInfo.flag = 0U;

  uint32_t len = 0U;
  rtDevResAddrInfo addrInfo{};
  addrInfo.resAddress = nullptr;
  addrInfo.len = &len;

  rret = rtGetDevResAddress(&resInfo, &addrInfo);
  if (rret != RT_ERROR_NONE || addrInfo.resAddress == nullptr) {
    HIXL_LOGE(FAILED, "[CompletePool] rtGetDevResAddress failed. ret=%d addr=%p",
              static_cast<int32_t>(rret),
              addrInfo.resAddress);
    return HCCL_E_INTERNAL;
  }

  slot.devFlagAddr = reinterpret_cast<uintptr_t>(addrInfo.resAddress);

  errno_t nret = snprintf_s(slot.notifyTag.data(),
                            slot.notifyTag.size(),
                            slot.notifyTag.size() - 1U,
                            "%s_%03u",
                            kUbLocalDevFlagTagPrefix,
                            slot_index);
  if (nret < 0) {
    HIXL_LOGE(FAILED, "[CompletePool] snprintf_s notify tag failed. slot=%u", slot_index);
    return HCCL_E_INTERNAL;
  }

  HcclMem mem{};
  mem.type = HCCL_MEM_TYPE_DEVICE;
  mem.addr = addrInfo.resAddress;
  mem.size = static_cast<u64>(kNotifyFlagBytes);

  MemHandle mh = nullptr;
  Status sret = endpoint_->RegisterMem(slot.notifyTag.data(), mem, mh);
  if (sret != SUCCESS) {
    HIXL_LOGE(sret, "[CompletePool] endpoint RegisterMem(notify flag) failed. slot=%u tag=%s addr=%p",
              slot_index,
              slot.notifyTag.data(),
              mem.addr);
    return HCCL_E_INTERNAL;
  }

  slot.notifyMemHandle = mh;

  HIXL_LOGI("[CompletePool] notify flag registered. slot=%u notifyId=%u tag=%s devAddr=%p u64=%" PRIu64,
            slot_index,
            notifyId,
            slot.notifyTag.data(),
            addrInfo.resAddress,
            slot.devFlagAddr);
  return HCCL_SUCCESS;
}

void CompletePool::DestroySlotLocked(Slot &slot) {
  if (slot.notifyMemHandle != nullptr) {
    if (endpoint_ != nullptr) {
      (void)endpoint_->DeregisterMem(slot.notifyMemHandle);
    }
    slot.notifyMemHandle = nullptr;
  }

  if (slot.notify != nullptr) {
    (void)rtNotifyDestroy(slot.notify);
    slot.notify = nullptr;
  }

  if (slot.thread != nullptr) {
    HcclResult tret = HcommThreadFree(slot.thread);
    if (tret != HCCL_SUCCESS) {
      HIXL_LOGW("[CompletePool] HcommThreadFree failed. ret=0x%X", static_cast<uint32_t>(tret));
    }
    slot.thread = nullptr;
  }

  if (slot.stream != nullptr) {
    (void)aclrtDestroyStream(slot.stream);
    slot.stream = nullptr;
  }

  if (slot.ctx != nullptr) {
    (void)aclrtDestroyContext(slot.ctx);
    slot.ctx = nullptr;
  }

  if (slot.hostFlag != nullptr) {
    (void)rtFreeHost(slot.hostFlag);
    slot.hostFlag = nullptr;
  }

  slot.devFlagAddr = 0ULL;
  slot.notifyTag.fill('\0');
}

}  // namespace hixl
