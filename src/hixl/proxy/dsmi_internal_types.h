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

struct DsmiBoardInfoStru {
  unsigned int board_id;
  unsigned int pcb_id;
  unsigned int bom_id;
  unsigned int slot_id;
};

struct DsmiSpodInfo {
  unsigned int sdid;
  unsigned int scale_type;
  unsigned int super_pod_id;
  unsigned int server_id;
  unsigned int chassis_id;
  unsigned int super_pod_type;
  unsigned int super_pod_intercon_type;
  unsigned int reserve[5];
};

#endif  // CANN_HIXL_SRC_HIXL_PROXY_DSMI_INTERNAL_TYPES_H_
