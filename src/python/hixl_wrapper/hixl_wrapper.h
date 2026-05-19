/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef HIXL_WRAPPER_H_
#define HIXL_WRAPPER_H_

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void *HixlHandle;

// Lifecycle
HixlHandle HixlCreate();
void HixlDestroy(HixlHandle handle);

uint32_t HixlInitialize(HixlHandle handle, const char *local_engine, const char **option_keys, const char **option_vals,
                        int option_count);

void HixlFinalize(HixlHandle handle);

// Memory Management
uint32_t HixlRegisterMem(HixlHandle handle, uintptr_t addr, size_t len, int mem_type, void **mem_handle);

uint32_t HixlDeregisterMem(HixlHandle handle, void *mem_handle);

// Connection
uint32_t HixlConnect(HixlHandle handle, const char *remote_engine, int32_t timeout_ms);

uint32_t HixlDisconnect(HixlHandle handle, const char *remote_engine, int32_t timeout_ms);

// Transfer
uint32_t HixlTransferSync(HixlHandle handle, const char *remote_engine, int operation, const uintptr_t *local_addrs,
                          const uintptr_t *remote_addrs, const size_t *lengths, int desc_count, int32_t timeout_ms);

uint32_t HixlTransferAsync(HixlHandle handle, const char *remote_engine, int operation, const uintptr_t *local_addrs,
                           const uintptr_t *remote_addrs, const size_t *lengths, int desc_count, void *optional_args,
                           void **req_handle);

uint32_t HixlGetTransferStatus(HixlHandle handle, void *req_handle, int *status);

// Notify
uint32_t HixlSendNotify(HixlHandle handle, const char *remote_engine, const char *notify_name, const char *notify_msg,
                        int32_t timeout_ms);

uint32_t HixlGetNotifies(HixlHandle handle, char ***notify_names, char ***notify_msgs, int *notify_count);

void HixlFreeNotifies(char **notify_names, char **notify_msgs, int count);

#ifdef __cplusplus
}
#endif

#endif  // HIXL_WRAPPER_H_
