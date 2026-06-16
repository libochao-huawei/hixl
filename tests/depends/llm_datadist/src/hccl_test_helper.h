/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef AIR_TESTS_DEPENDS_LLM_DATADIST_SRC_HCCL_TEST_HELPER_H_
#define AIR_TESTS_DEPENDS_LLM_DATADIST_SRC_HCCL_TEST_HELPER_H_

#include <map>
#include <string>

#include "hccl_stub.h"

// 共享的 HcclExchangeMemDesc1 桩函数，各测试文件通过覆盖 func_map 中的
// HcclBatchGet / HcclBatchPut 条目来注入各自不同的实现。
inline HcclResult HcclExchangeMemDesc1(HcclComm comm, uint32_t remoteRank, HcclMemDescs *local, int timeout,
                                       HcclMemDescs *remote, uint32_t *actualNum) {
  for (uint32_t i = 0U; i < local->arrayLength; ++i) {
    strcpy(remote->array[i].desc, local->array[i].desc);
  }
  *actualNum = local->arrayLength;
  remote->arrayLength = local->arrayLength;
  return HcclResult::HCCL_SUCCESS;
}

// 返回基础的 HCCL 函数映射表，所有条目使用真实桩实现。
// 测试文件可在此基础上覆盖 HcclBatchGet / HcclBatchPut 等条目注入自定义桩。
inline std::map<std::string, void *> GetBaseHcclFuncMap() {
  return {{"HcclCommInitClusterInfoMemConfig", reinterpret_cast<void *>(&HcclCommInitClusterInfoMemConfig)},
          {"HcclExchangeMemDesc", reinterpret_cast<void *>(&HcclExchangeMemDesc1)},
          {"HcclCommDestroy", reinterpret_cast<void *>(&HcclCommDestroy)},
          {"HcclBatchPut", reinterpret_cast<void *>(&HcclBatchPut)},
          {"HcclBatchGet", reinterpret_cast<void *>(&HcclBatchGet)},
          {"HcclRemapRegistedMemory", reinterpret_cast<void *>(&HcclRemapRegistedMemory)},
          {"HcclRegisterGlobalMem", reinterpret_cast<void *>(&HcclRegisterGlobalMem)},
          {"HcclDeregisterGlobalMem", reinterpret_cast<void *>(&HcclDeregisterGlobalMem)},
          {"HcclCommBindMem", reinterpret_cast<void *>(&HcclCommBindMem)},
          {"HcclCommUnbindMem", reinterpret_cast<void *>(&HcclCommUnbindMem)},
          {"HcclCommPrepare", reinterpret_cast<void *>(&HcclCommPrepare)}};
}

#endif  // AIR_TESTS_DEPENDS_LLM_DATADIST_SRC_HCCL_TEST_HELPER_H_
