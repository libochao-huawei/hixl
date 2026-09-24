/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <utility>

#include "prof_proxy.h"
#include "prof_reporter.h"

namespace hixl {
ProfStartPtr GetProfStart(const HixlProfType prof_type) {
  return std::make_shared<ProfStart>(prof_type);
}

HixlProfilingReporter::HixlProfilingReporter(const HixlProfType api_id) : prof_start_(GetProfStart(api_id)) {}

HixlProfilingReporter::HixlProfilingReporter(ProfStartPtr prof_start) : prof_start_(std::move(prof_start)) {}

HixlProfilingReporter::~HixlProfilingReporter() noexcept {
  if (prof_start_ != nullptr) {
    prof_start_->Stop();
  }
}
}  // namespace hixl
