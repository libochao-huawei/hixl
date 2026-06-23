/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#ifndef CANN_HIXL_SRC_HIXL_OPS_HIXL_KERNEL_TSFW_NOTIFIER_H_
#define CANN_HIXL_SRC_HIXL_OPS_HIXL_KERNEL_TSFW_NOTIFIER_H_

#include <cstdint>

namespace hixl {

constexpr uint32_t TSFW_NOTIFY_SUCCESS = 0U;
constexpr uint32_t TSFW_NOTIFY_DRV_ERROR = 1U;
constexpr uint32_t TSFW_NOTIFY_API_UNAVAILABLE = 2U;

uint32_t NotifyTsfwTaskException(uint32_t notify_id, int32_t user_stream_id, uint32_t error_code);

}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_OPS_HIXL_KERNEL_TSFW_NOTIFIER_H_
