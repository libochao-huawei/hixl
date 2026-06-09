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
constexpr uint16_t kDecoderListenPort = 26001;
constexpr uint16_t kDecoderControlPort = 26003;
constexpr uint16_t kDecoderClusterId = 1;
constexpr uint32_t kNumTensors = 4U;
constexpr size_t kTensorSize = 8 * 16 * sizeof(int32_t);
const std::vector<int64_t> kTensorShape = {8, 16};
constexpr int32_t kSocketBacklog = 2;
constexpr int32_t kExpectedArgCnt = 4;
constexpr uint32_t kArgIndexDeviceId = 1;
constexpr uint32_t kArgIndexLocalIp = 2;
constexpr uint32_t kArgIndexLocalCommRes = 3;
constexpr const char *kDecoderReadyMessage = "LLM_DATADIST_DECODER_READY_CHECK";
constexpr const char *kUnlinkAckMessage = "LLM_DATADIST_UNLINKED";

}  // namespace

int Initialize(LlmDataDist &llm_datadist, const std::string &device_id, const std::string &local_ip,
               const std::string &local_comm_res) {
  std::map<AscendString, AscendString> options;
  options[OPTION_DEVICE_ID] = device_id.c_str();
  options[OPTION_LISTEN_IP_INFO] = (std::string(local_ip) + ":" + std::to_string(kDecoderListenPort)).c_str();
  if (!local_comm_res.empty()) {
    options[OPTION_TRANSFER_BACKEND] = "hixl";
    options[OPTION_LOCAL_COMM_RES] = local_comm_res.c_str();
  }
  auto ret = llm_datadist.Initialize(options);
  if (ret != LLM_SUCCESS) {
    printf("[ERROR] Initialize failed, ret = %u, errmsg: %s\n", ret, sample::GetAclRecentErrMsg());
    return -1;
  }
  printf("[INFO] Initialize success\n");
  return LLM_SUCCESS;
}

int32_t CheckBuffers(const std::vector<void *> &buffers, const std::vector<uint32_t> &check_index_list) {
  return sample::CheckInt32Buffers(buffers, kTensorSize, kTensorShape[1], check_index_list);
}

int32_t RegisterCache(LlmDataDist &llm_datadist, std::vector<void *> &buffers, int64_t &cache_id) {
  CacheDesc cache_desc{};
  cache_desc.num_tensors = kNumTensors;
  cache_desc.data_type = DT_INT32;
  cache_desc.shape = kTensorShape;
  return sample::RegisterKvCache(llm_datadist, cache_desc, kTensorSize, kNumTensors, false, buffers, cache_id);
}

int32_t RunDecoderSample(const char *device_id, const char *local_ip, const std::string &local_comm_res) {
  printf("[INFO] Decoder Sample start\n");
  // 1. 初始化
  LlmDataDist llm_datadist(kDecoderClusterId, LlmRole::kDecoder);
  if (Initialize(llm_datadist, device_id, local_ip, local_comm_res) != 0) {
    return -1;
  }

  std::vector<void *> buffers;
  int64_t cache_id = -1;
  if (RegisterCache(llm_datadist, buffers, cache_id) != 0) {
    sample::FinalizeCache(llm_datadist, cache_id, buffers);
    return -1;
  }

  int control_fd = sample::StartControlServer(local_ip, kDecoderControlPort, "Decoder", kSocketBacklog);
  if (control_fd < 0) {
    sample::ForceFinalizeCache(llm_datadist, buffers);
    return -1;
  }

  // 4. 等待prompt确认decoder cache已就绪后再建链
  if (sample::WaitControlMessage(control_fd, kDecoderReadyMessage, "decoder ready check") != 0) {
    sample::CloseFd(control_fd);
    sample::ForceFinalizeCache(llm_datadist, buffers);
    return -1;
  }

  // 5. 等待prompt完成UnlinkLlmClusters，确认本端comm已解绑
  if (sample::WaitControlMessage(control_fd, kUnlinkAckMessage, "unlink ack") != 0) {
    sample::CloseFd(control_fd);
    sample::ForceFinalizeCache(llm_datadist, buffers);
    return -1;
  }
  sample::CloseFd(control_fd);

  // 6. prompt已完成push并解除链路，检查结果
  if (CheckBuffers(buffers, {4, 5, 6, 7}) != 0) {
    sample::FinalizeCache(llm_datadist, cache_id, buffers);
    return -1;
  }

  // 7. 释放cache与llmDataDist
  sample::FinalizeCache(llm_datadist, cache_id, buffers);
  printf("[INFO] Decoder Sample end\n");
  return 0;
}

int main(int32_t argc, char **argv) {
  if (argc < kExpectedArgCnt - 1 || argc > kExpectedArgCnt) {
    printf("[ERROR] expect at least 2 args(device_id, localHostIp, [localCommRes]), but got %d\n", argc - 1);
    return -1;
  }
  const auto device_id = argv[kArgIndexDeviceId];
  const auto local_ip = argv[kArgIndexLocalIp];
  printf("[INFO] device_id = %s, local_ip = %s\n", device_id, local_ip);
  std::string local_comm_res;
  if (argc == kExpectedArgCnt) {
    local_comm_res = argv[kArgIndexLocalCommRes];
    printf("[INFO] local_comm_res = %s\n", local_comm_res.c_str());
  }
  auto ret = RunDecoderSample(device_id, local_ip, local_comm_res);
  return ret;
}
