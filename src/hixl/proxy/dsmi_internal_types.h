/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_PROXY_DSMI_INTERNAL_TYPES_H_
#define CANN_HIXL_SRC_HIXL_PROXY_DSMI_INTERNAL_TYPES_H_

#include <cstdint>

struct DsmiBoardInfoStru {
  uint32_t board_id;
  uint32_t pcb_id;
  uint32_t bom_id;
  uint32_t slot_id;
};

struct DsmiSpodInfo {
  uint32_t sdid;
  uint32_t scale_type;
  uint32_t super_pod_id;
  uint32_t server_id;
  uint32_t chassis_id;
  uint32_t super_pod_type;
  uint32_t super_pod_intercon_type;
  uint32_t reserve[5];
};

#endif  // CANN_HIXL_SRC_HIXL_PROXY_DSMI_INTERNAL_TYPES_H_
