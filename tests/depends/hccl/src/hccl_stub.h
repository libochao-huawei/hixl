/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef AIR_TESTS_DEPENDS_HCCL_SRC_HCCL_STUB_H_
#define AIR_TESTS_DEPENDS_HCCL_SRC_HCCL_STUB_H_

#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

// 测试辅助函数
void SetNextNbiFailure(int32_t ret);
void SetNextFenceFailure(int32_t ret);
void SetListenPortResult(int32_t ret);
void ResetTransferCounter();

// HCCL Stub 函数
HcommResult HcommMemReg(EndpointHandle endPointHandle, const char *memTag, const CommMem *mem,
                        HcommMemHandle *memHandle);
HcommResult HcommMemUnreg(EndpointHandle endPointHandle, HcommMemHandle memHandle);
HcommResult HcommMemExport(EndpointHandle endPointHandle, HcommMemHandle memHandle, void **memDesc,
                           uint32_t *memDescLen);
HcommResult HcommEndpointCreate(const EndpointDesc *endPoint, EndpointHandle *endPointHandle);
HcommResult HcommEndpointDestroy(EndpointHandle endPointHandle);
HcommResult HcommMemImport(EndpointHandle endpointHandle, const void *memDesc, uint32_t descLen, CommMem *outMem);
HcommResult HcommMemUnimport(EndpointHandle endpointHandle, const void *memDesc, uint32_t descLen);
HcommResult HcommChannelCreate(EndpointHandle endPointHandle, CommEngine engine, HcommChannelDesc *channelDescs,
    uint32_t channelNum, ChannelHandle *channels);
HcommResult HcommChannelDestroy(const ChannelHandle *channels, uint32_t channelNum);
HcommResult HcommChannelGetStatus(const ChannelHandle *channelList, uint32_t listNum, int32_t *statusList);
int32_t HcommWriteNbiOnThread(ThreadHandle thread, ChannelHandle channel, void *dst, void *src, uint64_t len);
int32_t HcommReadNbiOnThread(ThreadHandle thread, ChannelHandle channel, void *dst, void *src, uint64_t len);
HcommResult HcommThreadAlloc(CommEngine engine, uint32_t threadNum, const uint32_t *notifyNumPerThread,
                             ThreadHandle *threads);
HcommResult HcommThreadFree(const ThreadHandle *threads, uint32_t threadNum);
int32_t HcommBatchModeStart(const char *batchTag);
int32_t HcommBatchModeEnd(const char *batchTag);
int32_t HcommReadOnThread(ThreadHandle thread, ChannelHandle channel, void *dst, const void *src, uint64_t len);
int32_t HcommWriteOnThread(ThreadHandle thread, ChannelHandle channel, void *dst, const void *src, uint64_t len);
int32_t HcommChannelFenceOnThread(ThreadHandle thread, ChannelHandle channel);

#ifdef __cplusplus
}
#endif

#endif
