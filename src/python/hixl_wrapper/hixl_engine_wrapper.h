/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_PYTHON_HIXL_WRAPPER_HIXL_ENGINE_WRAPPER_H_
#define CANN_HIXL_PYTHON_HIXL_WRAPPER_HIXL_ENGINE_WRAPPER_H_

#include <cstdint>
#include <shared_mutex>
#include <tuple>
#include <utility>
#include "hixl/hixl_types.h"

namespace hixl_wrapper {

// Tuple 类型别名：Python ↔ C++ 的桥梁类型
using MemDescTuple = std::tuple<uintptr_t, size_t>;              // (addr, length)
using TransferOpDescTuple = std::tuple<uintptr_t, uintptr_t, size_t>;  // (local_addr, remote_addr, len)
using NotifyDescTuple = std::tuple<std::string, std::string>;      // (name, msg)

class HixlEngineWrapper {
 public:
  // 拆包方法：Python tuple → C++ struct
  static hixl::MemDesc UnpackMemDesc(const MemDescTuple &mem_desc_tuple);
  static std::vector<hixl::TransferOpDesc> UnpackTransferOpDescs(
      const std::vector<TransferOpDescTuple> &op_desc_tuples);
  static hixl::NotifyDesc UnpackNotifyDesc(const NotifyDescTuple &notify_tuple);

  // 字符串 ↔ 枚举转换（非法输入返回 PARAM_INVALID）
  static std::pair<hixl::Status, hixl::MemType> ParseMemType(const std::string &mem_type_str);
  static std::pair<hixl::Status, hixl::TransferOp> ParseTransferOp(const std::string &op_str);
  static std::string TransferStatusToStr(hixl::TransferStatus status);
  static std::string AsyncConnectStatusToStr(hixl::AsyncConnectStatus status);
  static std::pair<hixl::Status, hixl::FeatureType> ParseFeatureType(const std::string &feature_type_str);

  // 业务方法（全部 static，与 llm_wrapper 风格一致）
  static hixl::Status Initialize(const std::string &local_engine,
                                  const std::map<std::string, std::string> &options);
  static void Finalize();
  static std::pair<hixl::Status, uintptr_t> RegisterMem(const MemDescTuple &mem_desc,
                                                          const std::string &mem_type);
  static hixl::Status DeregisterMem(uintptr_t mem_handle);
  static hixl::Status Connect(const std::string &remote_engine, int32_t timeout_ms = 1000);
  static hixl::Status Disconnect(const std::string &remote_engine, int32_t timeout_ms = 1000);
  static hixl::Status ConnectAsync(const std::string &remote_engine, int32_t timeout_ms = 1000);
  static hixl::Status DisconnectAsync(const std::string &remote_engine, int32_t timeout_ms = 1000);
  static std::pair<hixl::Status, std::string> GetAsyncConnectStatus(const std::string &remote_engine);
  static std::pair<hixl::Status, std::vector<std::pair<std::string, std::string>>> GetAllAsyncConnectStatus();
  static hixl::Status TransferSync(const std::string &remote_engine,
                                    const std::string &operation,
                                    const std::vector<TransferOpDescTuple> &op_descs,
                                    int32_t timeout_ms = 1000);
  static std::pair<hixl::Status, uintptr_t> TransferAsync(const std::string &remote_engine,
                                                            const std::string &operation,
                                                            const std::vector<TransferOpDescTuple> &op_descs);
  static std::pair<hixl::Status, std::string> GetTransferStatus(uintptr_t req_id);
  static std::pair<hixl::Status, std::vector<std::tuple<uintptr_t, std::string>>>
      GetTransferStatusBatch(uint32_t max_query_count, bool skip_waiting);
  static hixl::Status SendNotify(const std::string &remote_engine,
                                  const NotifyDescTuple &notify,
                                  int32_t timeout_ms = 1000);
  static std::pair<hixl::Status, std::vector<NotifyDescTuple>> GetNotifies();
  static std::pair<hixl::Status, int32_t> GetCapability(const std::string &feature_type);

 private:
  static std::unique_ptr<hixl::Hixl> hixl_engine_;
  static std::shared_mutex mutex_;
};

}  // namespace hixl_wrapper

#endif  // CANN_HIXL_PYTHON_HIXL_WRAPPER_HIXL_ENGINE_WRAPPER_H_
