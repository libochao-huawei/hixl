#ifndef CANN_HIXL_SRC_HIXL_COMMON_COMPLETE_POOL_H_
#define CANN_HIXL_SRC_HIXL_COMMON_COMPLETE_POOL_H_

#include <array>
#include <cstdint>
#include <mutex>
#include <vector>

#include "acl/acl.h"
#include "runtime/runtime/rt.h"
#include "common/hixl_cs.h"
#include "common/hixl_checker.h"

namespace hixl {

class Endpoint;

class CompletePool {
 public:
  static constexpr uint32_t kMaxSlots = 128U;

  struct SlotHandle {
    uint32_t slot_index;
    aclrtContext ctx;
    aclrtStream stream;
    ThreadHandle thread;
    rtNotify_t notify;
    void *host_flag;     // pinned host
    void *notify_addr;   // device addr (notify record address)
  };

  CompletePool();
  ~CompletePool();

  CompletePool(const CompletePool &) = delete;
  CompletePool &operator=(const CompletePool &) = delete;

  Status AddRefAndInitIfNeeded(int32_t device_id,
                               CommEngine engine,
                               uint32_t thread_num,
                               uint32_t notify_num_per_thread,
                               Endpoint *endpoint);

  void ReleaseRefAndDeinitIfNeeded();

  Status Acquire(SlotHandle *handle);
  void Release(uint32_t slot_index);

  bool IsComplete(const SlotHandle &handle) const;
  void ResetHostFlag(const SlotHandle &handle) const;
  uint32_t GetInUseCount() const;

 private:
  struct Slot {
    bool in_use;
    aclrtContext ctx;
    aclrtStream stream;
    ThreadHandle thread;
    rtNotify_t notify;

    void *notify_addr;
    void *host_flag;
    MemHandle notify_mem_handle;

    std::array<char, 64> notify_tag;
  };

 private:
  bool IsInitedParamsSame_(int32_t device_id,
                           CommEngine engine,
                           uint32_t thread_num,
                           uint32_t notify_num_per_thread) const;

  void SaveInitParams_(int32_t device_id,
                       CommEngine engine,
                       uint32_t thread_num,
                       uint32_t notify_num_per_thread,
                       Endpoint *endpoint);

  void ResetInitParamsLocked_();
  void InitFreeListLocked_();

  Status GetCurrentAclContext_(aclrtContext *old_ctx) const;
  void RestoreAclContext_(aclrtContext old_ctx) const;

  Status InitOneSlotLocked_(Slot &slot,
                            uint32_t slot_index,
                            int32_t device_id,
                            CommEngine engine,
                            uint32_t thread_num,
                            uint32_t notify_num_per_thread);

  Status SwitchDeviceAndNeedRestore_(int32_t target_device_id,
                                    int32_t *old_device_id,
                                    bool *need_restore) const;

  Status EnsureNotifyRecordLocked_(Slot &slot, uint32_t slot_index, int32_t device_id);

  void ResetNotifyResourcesLocked_(Slot &slot);
  Status CreateNotifyLocked_(Slot &slot, int32_t device_id, uint32_t *notify_id);
  Status GetNotifyAddrLocked_(uint32_t notify_id, void **notify_addr) const;
  Status BuildNotifyTagLocked_(uint32_t slot_index, std::array<char, 64> *tag) const;
  Status RegisterNotifyMemLocked_(Slot &slot, const char *tag, void *notify_addr);

  Status InitAllSlotsLocked(int32_t device_id,
                            CommEngine engine,
                            uint32_t thread_num,
                            uint32_t notify_num_per_thread);

  void DeinitAllSlotsLocked();

  Status EnsureContextLocked(Slot &slot, int32_t device_id);
  Status EnsureStreamLocked(Slot &slot);
  Status EnsureThreadLocked(Slot &slot,
                            CommEngine engine,
                            uint32_t thread_num,
                            uint32_t notify_num_per_thread);

  Status EnsurePinnedHostFlagLocked(Slot &slot);
  void DestroySlotLocked(Slot &slot);

 private:
  mutable std::mutex mu_;
  uint32_t ref_cnt_;
  bool inited_;
  std::vector<uint32_t> free_list_;
  std::array<Slot, kMaxSlots> slots_;

  Endpoint *endpoint_;

  int32_t init_device_id_;
  CommEngine init_engine_;
  uint32_t init_thread_num_;
  uint32_t init_notify_num_per_thread_;
};

}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_COMMON_COMPLETE_POOL_H_
