/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <stdint.h>
#include <stdarg.h>
#include "msprof_stub.h"

#ifdef __cplusplus
extern "C" {
#endif

ProfCommandHandle g_hixl_prof_callback = nullptr;

int32_t g_msprof_reg_type_info_ret = 0;
int32_t g_msprof_register_cb_ret = 0;
uint64_t g_msprof_report_api_count = 0U;

ProfCommandHandle GetHixlProfCallback(void) {
  return g_hixl_prof_callback;
}

void SetMsprofRegTypeInfoRet(int32_t ret) {
  g_msprof_reg_type_info_ret = ret;
}
void SetMsprofRegisterCallbackRet(int32_t ret) {
  g_msprof_register_cb_ret = ret;
}

uint64_t GetMsprofReportApiCount(void) {
  return g_msprof_report_api_count;
}

int32_t MsprofRegTypeInfo(uint32_t moduleId, uint32_t typeId, const char *name) {
  (void)moduleId;
  (void)typeId;
  (void)name;
  return g_msprof_reg_type_info_ret;
}

int32_t MsprofRegisterCallback(uint32_t moduleId, ProfCommandHandle cb) {
  (void)moduleId;
  g_hixl_prof_callback = cb;
  return g_msprof_register_cb_ret;
}

uint64_t MsprofSysCycleTime(void) {
  return 123456;
}

int32_t MsprofReportApi(bool flag, void *api) {
  (void)flag;
  (void)api;
  ++g_msprof_report_api_count;
  return 0;
}

int32_t MsprofFinalize(void) {
  return 0;
}

#ifdef __cplusplus
}
#endif
