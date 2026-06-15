/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "dsmi_stub.h"

#include <cstring>

struct dsmi_board_info_stru {
  unsigned int board_id;
  unsigned int pcb_id;
  unsigned int bom_id;
  unsigned int slot_id;
};

static const unsigned int kDsmiMainCmdChipInf = 12U;
static const unsigned int kDsmiChipInfSubCmdSpodInfo = 1U;

static unsigned int g_slot_id = 0U;
static int g_board_info_ret = 0;

static unsigned int g_intercon_type = 4U;
static unsigned int g_super_pod_id = 0U;
static int g_device_info_ret = 0;

#ifdef __cplusplus
extern "C" {
#endif

int dsmi_get_board_info(int device_id, struct dsmi_board_info_stru *pboard_info) {
  (void)device_id;
  if (pboard_info == nullptr) {
    return -1;
  }
  if (g_board_info_ret != 0) {
    return g_board_info_ret;
  }
  pboard_info->board_id = 0;
  pboard_info->pcb_id = 0;
  pboard_info->bom_id = 0;
  pboard_info->slot_id = g_slot_id;
  return 0;
}

int dsmi_get_device_info(unsigned int device_id, unsigned int main_cmd, unsigned int sub_cmd, void *buf,
                         unsigned int *buf_size) {
  (void)device_id;
  if (buf == nullptr || buf_size == nullptr) {
    return -1;
  }
  if (g_device_info_ret != 0) {
    return g_device_info_ret;
  }
  if (main_cmd != kDsmiMainCmdChipInf || sub_cmd != kDsmiChipInfSubCmdSpodInfo) {
    return -1;
  }
  static const unsigned int kSpodInfoFields = 12U;
  if (*buf_size < kSpodInfoFields * sizeof(unsigned int)) {
    return -1;
  }
  auto *fields = static_cast<unsigned int *>(buf);
  for (unsigned int i = 0; i < kSpodInfoFields; ++i) {
    fields[i] = 0;
  }
  fields[2] = g_super_pod_id;
  fields[6] = g_intercon_type;
  *buf_size = kSpodInfoFields * sizeof(unsigned int);
  return 0;
}

void DsmiStubSetInterconType(unsigned int type) {
  g_intercon_type = type;
}

void DsmiStubSetSuperPodId(unsigned int id) {
  g_super_pod_id = id;
}

void DsmiStubSetSlotId(unsigned int slot_id, int ret) {
  g_slot_id = slot_id;
  g_board_info_ret = ret;
}

void DsmiStubSetDeviceInfoRet(int ret) {
  g_device_info_ret = ret;
}

#ifdef __cplusplus
}
#endif
