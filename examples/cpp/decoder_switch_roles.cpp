/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <cstdio>
#include <string>
#include <vector>
#include "acl/acl.h"
#include "cache_utils.h"
#include "control_channel.h"
#include "llm_datadist/llm_datadist.h"

using namespace llm_datadist;
namespace {
constexpr uint16_t kPromptListenPort = 26000;
constexpr uint16_t kDecoderListenPort = 26001;
constexpr uint16_t kPromptClusterId = 0;
constexpr uint16_t kDecoderClusterId = 1;
constexpr uint32_t kNumTensors = 4U;
constexpr size_t kTensorSize = 8 * 16 * sizeof(int32_t);
const std::vector<int64_t> kTensorShape = {8, 16};
constexpr size_t kTensorBlockElementNum = 16;
constexpr uint16_t kPromptControlPort = 26002;
constexpr uint16_t kDecoderControlPort = 26003;
constexpr int32_t kSocketBacklog = 2;
constexpr int32_t kExpectedArgCnt = 4;
constexpr uint32_t kArgIndexDeviceId = 1;
constexpr uint32_t kArgIndexLocalIp = 2;
constexpr uint32_t kArgIndexRemoteIp = 3;
constexpr uint32_t kRemoteCacheId = 1U;
constexpr uint32_t kPullBatchIndex = 0U;
constexpr const char *kPromptReadyMessage = "LLM_DATADIST_PROMPT_READY";
constexpr const char *kDecoderPromptReadyMessage = "LLM_DATADIST_DECODER_PROMPT_READY";
constexpr const char *kPushDoneMessage = "LLM_DATADIST_PUSH_DONE";

}  // namespace

int Initialize(LlmDataDist &llm_datadist, const std::string &device_id) {
  std::map<AscendString, AscendString> options;
  options[OPTION_DEVICE_ID] = device_id.c_str();
  auto ret = llm_datadist.Initialize(options);
  if (ret != LLM_SUCCESS) {
    printf("[ERROR] Initialize failed, ret = %u, errmsg: %s\n", ret, sample::GetAclRecentErrMsg());
    return -1;
  }
  printf("[INFO] Initialize success\n");
  return LLM_SUCCESS;
}

int32_t SetRole(LlmDataDist &llm_datadist, LlmRole role, const char *local_ip) {
  std::map<AscendString, AscendString> options;
  options[OPTION_LISTEN_IP_INFO] = (std::string(local_ip) + ":" + std::to_string(kDecoderListenPort)).c_str();
  auto ret = llm_datadist.SetRole(role, options);
  if (ret != LLM_SUCCESS) {
    printf("[ERROR] SetRole failed, ret = %u, errmsg: %s\n", ret, sample::GetAclRecentErrMsg());
    return -1;
  }
  printf("[INFO] SetRole success\n");
  return 0;
}

int32_t RegisterCache(LlmDataDist &llm_datadist, std::vector<void *> &buffers, int64_t &cache_id) {
  CacheDesc cache_desc{};
  cache_desc.num_tensors = kNumTensors;
  cache_desc.data_type = DT_INT32;
  cache_desc.shape = kTensorShape;
  return sample::RegisterKvCache(llm_datadist, cache_desc, kTensorSize, kNumTensors, false, buffers, cache_id);
}

int RunInitialPull(LlmDataDist &llm_datadist, int64_t cache_id, int control_fd, const char *local_ip,
                   const char *remote_ip, const std::vector<void *> &buffers, bool &linked) {
  if (sample::WaitControlMessage(control_fd, kPromptReadyMessage, "prompt ready") != 0) {
    return -1;
  }
  if (sample::LinkClusters(llm_datadist, kPromptClusterId, local_ip, kPromptListenPort, remote_ip, kPromptListenPort) !=
      0) {
    return -1;
  }
  linked = true;
  std::vector<uint64_t> prompt_blocks{1, 2, 3};
  std::vector<uint64_t> decoder_blocks{1, 2, 3};
  if (sample::PullKvCacheBlocksAndBatch(llm_datadist, cache_id, kPromptClusterId, kRemoteCacheId, prompt_blocks,
                                        decoder_blocks, kPullBatchIndex) != 0) {
    return -1;
  }
  return sample::CheckInt32Buffers(buffers, kTensorSize, kTensorBlockElementNum, {0, 1, 2, 3});
}

int RunRoleSwitchAndReceivePush(LlmDataDist &llm_datadist, int control_fd, const char *local_ip, const char *remote_ip,
                                const std::vector<void *> &buffers, bool &linked) {
  if (sample::UnlinkClusters(llm_datadist, kPromptClusterId, remote_ip, kPromptListenPort) != 0) {
    return -1;
  }
  linked = false;
  if (SetRole(llm_datadist, LlmRole::kPrompt, local_ip) != 0) {
    return -1;
  }
  if (sample::SendControlMessage(remote_ip, kPromptControlPort, "prompt", kDecoderPromptReadyMessage,
                                 "decoder prompt ready") != 0) {
    return -1;
  }
  if (sample::WaitControlMessage(control_fd, kPushDoneMessage, "push done") != 0) {
    return -1;
  }
  return sample::CheckInt32Buffers(buffers, kTensorSize, kTensorBlockElementNum, {4, 5, 6, 7});
}

int32_t RunDecoderSample(const char *device_id, const char *local_ip, const char *remote_ip) {
  printf("[INFO] Decoder Sample start\n");
  int control_fd = sample::StartControlServer(local_ip, kDecoderControlPort, "Decoder", kSocketBacklog);
  if (control_fd < 0) {
    return -1;
  }
  // 1. 初始化
  LlmDataDist llm_datadist(kDecoderClusterId, LlmRole::kDecoder);
  if (Initialize(llm_datadist, device_id) != 0) {
    sample::CloseFd(control_fd);
    return -1;
  }

  // 2. 注册内存地址
  std::vector<void *> buffers;
  int64_t cache_id = -1;
  bool linked = false;
  auto fail = [&]() {
    sample::FinalizeLinkedCache(
        llm_datadist, cache_id, linked,
        [&]() { return sample::UnlinkClusters(llm_datadist, kPromptClusterId, remote_ip, kPromptListenPort); },
        buffers);
    sample::CloseFd(control_fd);
    return -1;
  };
  if (RegisterCache(llm_datadist, buffers, cache_id) != 0) {
    return fail();
  }

  // 3. 先作为decoder拉取prompt cache
  if (RunInitialPull(llm_datadist, cache_id, control_fd, local_ip, remote_ip, buffers, linked) != 0) {
    return fail();
  }
  // 4. 断链后切为prompt，等待对端push后的数据
  if (RunRoleSwitchAndReceivePush(llm_datadist, control_fd, local_ip, remote_ip, buffers, linked) != 0) {
    return fail();
  }

  // 5. 释放cache与llmDataDist
  sample::CloseFd(control_fd);
  llm_datadist.Finalize();
  printf("[INFO] Finalize success\n");
  printf("[INFO] Decoder Sample end\n");
  return 0;
}

int main(int32_t argc, char **argv) {
  if (argc != kExpectedArgCnt) {
    printf("[ERROR] expect 3 args(device_id, localHostIp, remoteHostIp), but got %d\n", argc - 1);
    return -1;
  }
  const auto device_id = argv[kArgIndexDeviceId];
  const auto local_ip = argv[kArgIndexLocalIp];
  const auto remote_ip = argv[kArgIndexRemoteIp];
  printf("[INFO] device_id = %s, local_ip = %s, remote_ip = %s\n", device_id, local_ip, remote_ip);
  auto ret = RunDecoderSample(device_id, local_ip, remote_ip);
  return ret;
}
