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
constexpr uint16_t kPromptControlPort = 26002;
constexpr uint16_t kDecoderControlPort = 26003;
constexpr int32_t kSocketBacklog = 2;
constexpr int32_t kExpectedArgCnt = 4;
constexpr uint32_t kArgIndexDeviceId = 1;
constexpr uint32_t kArgIndexLocalIp = 2;
constexpr uint32_t kArgIndexRemoteIp = 3;
constexpr uint32_t kPushBatchIndex = 4;
constexpr uint32_t kRemoteCacheId = 1U;
constexpr uint8_t kPushTensorNumPerLayer = 4;
constexpr const char *kPromptReadyMessage = "LLM_DATADIST_PROMPT_READY";
constexpr const char *kDecoderPromptReadyMessage = "LLM_DATADIST_DECODER_PROMPT_READY";
constexpr const char *kPushDoneMessage = "LLM_DATADIST_PUSH_DONE";

}  // namespace

int Initialize(LlmDataDist &llm_datadist, const std::string &device_id, const std::string &local_ip) {
  std::map<AscendString, AscendString> options;
  options[OPTION_DEVICE_ID] = device_id.c_str();
  options[OPTION_LISTEN_IP_INFO] = (local_ip + ":" + std::to_string(kPromptListenPort)).c_str();
  auto ret = llm_datadist.Initialize(options);
  if (ret != LLM_SUCCESS) {
    printf("[ERROR] Initialize failed, ret = %u, errmsg: %s\n", ret, sample::GetAclRecentErrMsg());
    return -1;
  }
  printf("[INFO] Initialize success\n");
  return LLM_SUCCESS;
}

int32_t SetRole(LlmDataDist &llm_datadist, LlmRole role) {
  std::map<AscendString, AscendString> options;
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
  return sample::RegisterKvCache(llm_datadist, cache_desc, kTensorSize, kNumTensors, true, buffers, cache_id);
}

int RunRoleSwitchAndPush(LlmDataDist &llm_datadist, int64_t cache_id, int control_fd, const char *local_ip,
                         const char *remote_ip, bool &linked) {
  if (sample::SendControlMessage(remote_ip, kDecoderControlPort, "decoder", kPromptReadyMessage, "prompt ready") != 0 ||
      sample::WaitControlMessage(control_fd, kDecoderPromptReadyMessage, "decoder prompt ready") != 0) {
    return -1;
  }
  if (SetRole(llm_datadist, LlmRole::kDecoder) != 0) {
    return -1;
  }
  if (sample::LinkClusters(llm_datadist, kDecoderClusterId, local_ip, kDecoderListenPort, remote_ip,
                           kDecoderListenPort) != 0) {
    return -1;
  }
  linked = true;
  std::vector<uint64_t> prompt_blocks{5, 6, 7};
  std::vector<uint64_t> decoder_blocks{5, 6, 7};
  if (sample::PushKvCacheBlocksAndBatch(llm_datadist, cache_id, kDecoderClusterId, kRemoteCacheId, prompt_blocks,
                                        decoder_blocks, kPushBatchIndex, kPushTensorNumPerLayer, kNumTensors) != 0) {
    return -1;
  }
  return sample::SendControlMessage(remote_ip, kDecoderControlPort, "decoder", kPushDoneMessage, "push done");
}

int32_t RunPromptSample(const char *device_id, const char *local_ip, const char *remote_ip) {
  printf("[INFO] Prompt Sample start\n");
  int control_fd = sample::StartControlServer(local_ip, kPromptControlPort, "Prompt", kSocketBacklog);
  if (control_fd < 0) {
    return -1;
  }
  // 1. 初始化
  LlmDataDist llm_datadist(kPromptClusterId, LlmRole::kPrompt);
  if (Initialize(llm_datadist, device_id, local_ip) != 0) {
    sample::CloseFd(control_fd);
    printf("[ERROR] Initialize LlmDataDist failed\n");
    return -1;
  }

  // 2. 注册内存地址
  std::vector<void *> buffers;
  int64_t cache_id = -1;
  bool linked = false;
  auto fail = [&]() {
    sample::FinalizeLinkedCache(
        llm_datadist, cache_id, linked,
        [&]() { return sample::UnlinkClusters(llm_datadist, kDecoderClusterId, remote_ip, kDecoderListenPort); },
        buffers);
    sample::CloseFd(control_fd);
    return -1;
  };
  if (RegisterCache(llm_datadist, buffers, cache_id) != 0) {
    return fail();
  }

  // 3. 通知decoder拉取cache，切换为decoder后建链并推送cache
  if (RunRoleSwitchAndPush(llm_datadist, cache_id, control_fd, local_ip, remote_ip, linked) != 0) {
    return fail();
  }

  // 4. 释放Cache与llmDataDist
  sample::CloseFd(control_fd);
  llm_datadist.Finalize();
  printf("[INFO] Finalize success\n");
  printf("[INFO] Prompt Sample end\n");
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
  auto ret = RunPromptSample(device_id, local_ip, remote_ip);
  return ret;
}
