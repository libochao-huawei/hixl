/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
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
#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include "hixl/hixl.h"
#include "hixl/hixl_types.h"

namespace hixl {

class HixlPy {
 public:
  HixlPy();

  ~HixlPy();

  Status initialize(const std::string &local_engine, const std::optional<std::map<std::string, std::string>> &options);

  void finalize();

  std::tuple<Status, int64_t> registerMem(const MemDesc &mem, MemType type);

  Status deregisterMem(int64_t handle_id);

  Status connect(const std::string &remote_engine, int32_t timeout = 1000);

  Status disconnect(const std::string &remote_engine, int32_t timeout = 1000);

  Status transferSync(const std::string &remote_engine, TransferOp op, const std::vector<TransferOpDesc> &op_descs,
                      int32_t timeout = 1000);

  std::tuple<Status, int64_t> transferAsync(const std::string &remote_engine, TransferOp op,
                                            const std::vector<TransferOpDesc> &op_descs,
                                            const std::optional<TransferArgs> &optional_args);

  std::tuple<Status, TransferStatus> getTransferStatus(int64_t req_id);

  Status sendNotify(const std::string &remote_engine, const std::string &name, const std::string &msg,
                    int32_t timeout = 1000);

  std::tuple<Status, std::vector<std::pair<std::string, std::string>>> getNotifies();

  Status connectAsync(const std::string &remote_engine, int32_t timeout = 1000);

  Status disconnectAsync(const std::string &remote_engine, int32_t timeout = 1000);

  std::tuple<Status, AsyncConnectStatus> getAsyncConnectStatus(const std::string &remote_engine);

  std::tuple<Status, std::vector<std::pair<std::string, AsyncConnectStatus>>> getAsyncConnectStatusAll();

 private:
  std::unique_ptr<Hixl> hixl_;
};

}  // namespace hixl

#endif  // HIXL_PY_H_
