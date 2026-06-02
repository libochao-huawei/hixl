/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_FABRIC_MEM_FABRIC_MEM_TRANSFER_SERVICE_H_
#define CANN_HIXL_SRC_HIXL_FABRIC_MEM_FABRIC_MEM_TRANSFER_SERVICE_H_

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <set>
#include <unordered_map>
#include <vector>

#include "fabric_mem/fabric_mem_statistic.h"
#include "fabric_mem/fabric_mem_types.h"
#include "hixl/hixl_types.h"

namespace hixl {
class FabricMemTransferService {
 public:
  FabricMemTransferService() = default;
  ~FabricMemTransferService();
  FabricMemTransferService(const FabricMemTransferService &) = delete;
  FabricMemTransferService &operator=(const FabricMemTransferService &) = delete;
  FabricMemTransferService(FabricMemTransferService &&) = delete;
  FabricMemTransferService &operator=(FabricMemTransferService &&) = delete;

  Status Initialize(int32_t device_id, size_t max_stream_num, size_t task_stream_num,
                    FabricMemStatistic *statistic);
  void Finalize();

  Status RegisterMem(const MemDesc &mem, MemType type, MemHandle &mem_handle);
  Status DeregisterMem(MemHandle mem_handle);

  Status Transfer(const FabricMemTransferContext &context, TransferOp operation,
                  const std::vector<TransferOpDesc> &op_descs, int32_t timeout_in_millis);
  Status TransferAsync(const FabricMemTransferContext &context, TransferOp operation,
                       const std::vector<TransferOpDesc> &op_descs, TransferReq &req);
  Status GetTransferStatus(const FabricMemTransferContext &context, const TransferReq &req, TransferStatus &status);

  std::vector<ShareHandleInfo> GetShareHandles();
  void RemoveChannel(const std::string &channel_id);

  static Status MallocMem(MemType type, size_t size, void **ptr);
  static Status FreeMem(void *ptr);

 private:
  struct TransferSlotEntry {
    aclrtContext ctx = nullptr;
    std::vector<aclrtStream> streams;
    bool available = true;
  };

  enum class AsyncStreamQueryResult { kWaiting, kFailed, kComplete };

  class OperationGuard {
   public:
    explicit OperationGuard(FabricMemTransferService &service);
    ~OperationGuard();
    OperationGuard(const OperationGuard &) = delete;
    OperationGuard &operator=(const OperationGuard &) = delete;
    bool Acquired() const;

   private:
    FabricMemTransferService &service_;
    bool acquired_{false};
  };

  bool TryAcquireOperation();
  void ReleaseOperation();
  Status InitDevConstOne();
  void FreeDevConstOne();
  Status TryAcquireAsyncSlot(AsyncSlot &slot);
  void ReleaseAsyncSlot(AsyncSlot &slot, bool destroy_slot);
  Status AppendHostFlagCopies(const AsyncSlot &slot) const;
  static bool AllHostFlagsDone(const AsyncSlot &slot);
  static AsyncStreamQueryResult QueryAsyncSlotStreams(const AsyncSlot &slot);
  Status HandleAsyncStreamQueryFailure(const FabricMemTransferContext &context, uint64_t req_id,
                                       AsyncRecord &async_record, TransferStatus &status);
  void RegisterAsyncTransferRecord(const FabricMemTransferContext &context, TransferReq &req, AsyncSlot &&slot,
                                   const std::chrono::steady_clock::time_point &transfer_start,
                                   const std::chrono::steady_clock::time_point &real_copy_start,
                                   uint64_t transfer_bytes, uint64_t op_desc_count);
  Status CompleteAsyncTransferAndUpdateStats(const FabricMemTransferContext &context, uint64_t req_id,
                                             AsyncRecord &async_record, TransferStatus &status);
  Status TryAcquireSlotLocked(AsyncSlot &slot);
  Status TryAcquireHostFlagsLocked(std::vector<void *> &host_flags, size_t count);
  void ReleaseHostFlagsLocked(std::vector<void *> &host_flags);
  void FreeAllHostFlagsLocked();
  Status CreateSlotEntryLocked(TransferSlotEntry &entry);
  void DestroySlotEntryLocked(TransferSlotEntry &entry, bool abort_streams);
  void ReturnSlotLocked(const AsyncSlot &slot);
  Status TryAcquireSlot(AsyncSlot &slot);
  Status TryAcquireSlotWithTimeout(AsyncSlot &slot, uint64_t timeout_us);
  static Status ProcessCopyWithAsync(const AsyncSlot &slot, TransferOp operation,
                                     const std::vector<TransferOpDesc> &op_descs);
  Status DoTransfer(const AsyncSlot &slot, const FabricMemTransferContext &context, TransferOp operation,
                    std::vector<TransferOpDesc> &op_descs, std::chrono::steady_clock::time_point &start);
  void ReleaseSlot(AsyncSlot &slot, bool destroy_slot);
  void RemoveChannelReqRelation(const std::string &channel_id, uint64_t req_id);
  static Status TransOpAddr(uintptr_t old_addr, size_t len,
                            const std::unordered_map<uintptr_t, VaInfo> &new_va_to_old_va, uintptr_t &new_addr);
  Status TransLocalHostOpAddr(uintptr_t old_addr, size_t len, uintptr_t &new_addr);
  Status TransLocalHostOpAddrs(std::vector<TransferOpDesc> &op_descs);
  bool FindLocalHostRegisteredAddrLocked(uintptr_t old_addr, size_t len, uintptr_t &new_addr) const;
  void UpdateStats(const FabricMemTransferContext &context, uint64_t transfer_cost, uint64_t real_copy_cost,
                   uint64_t transfer_bytes, uint64_t op_desc_count) const;
  Status NeedTransLocalAddr(const std::vector<TransferOpDesc> &op_descs, bool &need_trans_local_addr) const;

  // Lock hierarchy (must be acquired in this order):
  //   lifecycle_mutex_ -> channel_op_mutex_ -> share_handle_mutex_ -> stream_pool_mutex_
  //   lifecycle_mutex_ -> channel_op_mutex_ -> async_req_mutex_ -> channel_2_req_mutex_
  std::mutex lifecycle_mutex_;
  std::condition_variable lifecycle_cv_;
  uint32_t active_operations_{0U};
  bool finalizing_{false};

  std::mutex channel_op_mutex_;
  std::mutex share_handle_mutex_;
  std::unordered_map<aclrtDrvMemHandle, ShareHandleInfo> share_handles_;
  int32_t device_id_{-1};
  size_t max_stream_num_{0};
  size_t task_stream_num_{0};
  size_t max_async_slot_num_{0};
  FabricMemStatistic *statistic_{nullptr};

  std::mutex stream_pool_mutex_;
  std::condition_variable slot_pool_cv_;
  std::vector<TransferSlotEntry> slot_pool_;
  std::queue<size_t> free_slot_indices_;
  std::vector<void *> free_host_flags_;
  size_t allocated_host_flag_count_{0};
  void *dev_const_one_{nullptr};

  std::mutex async_req_mutex_;
  std::unordered_map<uint64_t, AsyncRecord> req_2_async_record_;

  std::mutex channel_2_req_mutex_;
  std::unordered_map<std::string, std::set<uint64_t>> channel_2_req_;

  bool has_host_memory_{false};
};
}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_FABRIC_MEM_FABRIC_MEM_TRANSFER_SERVICE_H_
