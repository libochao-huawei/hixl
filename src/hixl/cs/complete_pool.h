#ifndef CANN_HIXL_SRC_HIXL_COMMON_COMPLETE_POOL_H_
#define CANN_HIXL_SRC_HIXL_COMMON_COMPLETE_POOL_H_

#include <array>
#include <cstdint>
#include <mutex>
#include <vector>

#include "acl/acl.h"
#include "runtime/runtime/rt.h"
#include "common/hccl_api.h"

namespace hixl {

class Endpoint;

class CompletePool {
 public:
  static constexpr uint32_t kMaxSlots = 128U;

  struct SlotHandle {
    uint32_t slotIndex;
    aclrtContext ctx;
    aclrtStream stream;
    ThreadHandle thread;
    rtNotify_t notify;
    uint64_t *hostFlag;     // pinned host
    uint64_t devFlagAddr;   // device addr
  };

  CompletePool();
  ~CompletePool();

  CompletePool(const CompletePool &) = delete;
  CompletePool &operator=(const CompletePool &) = delete;

  HcclResult AddRefAndInitIfNeeded(int32_t deviceId,
                                  CommEngine engine,
                                  uint32_t threadNum,
                                  uint32_t notifyNumPerThread,
                                  Endpoint *endpoint);

  void ReleaseRefAndDeinitIfNeeded();

  HcclResult Acquire(SlotHandle *handle);
  void Release(uint32_t slotIndex);

  bool IsComplete(const SlotHandle &handle) const;
  void ResetHostFlag(const SlotHandle &handle) const;
  uint32_t GetInUseCount() const;
 private:
  struct Slot {
    bool inUse;
    aclrtContext ctx;
    aclrtStream stream;
    ThreadHandle thread;
    rtNotify_t notify;

    uint64_t devFlagAddr;
    uint64_t *hostFlag;          // pinned host memory
    MemHandle notifyMemHandle;

    std::array<char, 64> notifyTag;
  };

  HcclResult InitAllSlotsLocked(int32_t deviceId,
                               CommEngine engine,
                               uint32_t threadNum,
                               uint32_t notifyNumPerThread);

  void DeinitAllSlotsLocked();

  HcclResult EnsureContextLocked(Slot &slot, int32_t deviceId);
  HcclResult EnsureStreamLocked(Slot &slot);
  HcclResult EnsureThreadLocked(Slot &slot,
                               CommEngine engine,
                               uint32_t threadNum,
                               uint32_t notifyNumPerThread);

  HcclResult EnsureNotifyAndDevFlagLocked(Slot &slot, uint32_t slot_index, int32_t deviceId);

  HcclResult EnsurePinnedHostFlagLocked(Slot &slot);

  void DestroySlotLocked(Slot &slot);

 private:
  mutable std::mutex mu_;
  uint32_t refCnt_;
  bool inited_;
  std::vector<uint32_t> freeList_;
  std::array<Slot, kMaxSlots> slots_;

  Endpoint *endpoint_;

  int32_t initDeviceId_;
  CommEngine initEngine_;
  uint32_t initThreadNum_;
  uint32_t initNotifyNumPerThread_;
};

}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_COMMON_COMPLETE_POOL_H_
