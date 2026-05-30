/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef HIXL_PY_H_
#define HIXL_PY_H_

#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "hixl/hixl.h"
#include "hixl/hixl_types.h"

namespace hixl {

class HixlPy {
 public:
  HixlPy();

  ~HixlPy();

  void initialize(const std::string &local_engine, const std::optional<std::map<std::string, std::string>> &options);

  void finalize();

  int64_t registerMem(const MemDesc &mem, MemType type);

  void deregisterMem(int64_t handle_id);

  void connect(const std::string &remote_engine, int32_t timeout = 1000);

  void disconnect(const std::string &remote_engine, int32_t timeout = 1000);

  void transferSync(const std::string &remote_engine, TransferOp op, const std::vector<TransferOpDesc> &op_descs,
                    int32_t timeout = 1000);

  int64_t transferAsync(const std::string &remote_engine, TransferOp op, const std::vector<TransferOpDesc> &op_descs,
                        const std::optional<TransferArgs> &optional_args);

  TransferStatus getTransferStatus(int64_t req_id, bool auto_cleanup = true);

  void sendNotify(const std::string &remote_engine, const std::string &name, const std::string &msg,
                  int32_t timeout = 1000);

  std::vector<std::pair<std::string, std::string>> getNotifies();

 private:
  void checkStatus(Status status, const std::string &context);

  std::unique_ptr<Hixl> hixl_;
  std::mutex mutex_;
  bool initialized_ = false;
};

}  // namespace hixl

#endif  // HIXL_PY_H_
