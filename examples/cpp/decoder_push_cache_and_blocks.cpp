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
#include <numeric>
#include <string>
#include <utility>
#include <vector>
#include "acl/acl.h"
#include "control_channel.h"
#include "llm_datadist/llm_datadist.h"

using namespace llm_datadist;
namespace sample {
inline const char *GetAclRecentErrMsg() {
  const char *errmsg = aclGetRecentErrMsg();
  if (errmsg == nullptr) {
    return "no error";
  }
  return errmsg;
}

inline int InitDeviceBuffer(int32_t *buffer, size_t tensor_size) {
  std::vector<int32_t> host_buffer(tensor_size / sizeof(int32_t));
  std::iota(host_buffer.begin(), host_buffer.end(), 0);
  aclError ret = aclrtMemcpy(buffer, tensor_size, host_buffer.data(), tensor_size, ACL_MEMCPY_HOST_TO_DEVICE);
  if (ret != ACL_ERROR_NONE) {
    printf("[ERROR] aclrtMemcpy failed, ret = %d, errmsg: %s\n", ret, GetAclRecentErrMsg());
    return -1;
  }
  return 0;
}

inline int AllocateCacheBuffers(uint32_t num_tensors, size_t tensor_size, bool init_host_data,
                                std::vector<uint64_t> &tensor_addrs, std::vector<void *> &buffers) {
  for (uint32_t i = 0U; i < num_tensors; ++i) {
    int32_t *buffer = nullptr;
    aclError ret = aclrtMalloc(reinterpret_cast<void **>(&buffer), tensor_size, ACL_MEM_MALLOC_HUGE_ONLY);
    if (ret != ACL_ERROR_NONE) {
      printf("[ERROR] aclrtMalloc failed, ret = %d, errmsg: %s\n", ret, GetAclRecentErrMsg());
      return -1;
    }
    if (init_host_data && InitDeviceBuffer(buffer, tensor_size) != 0) {
      aclrtFree(buffer);
      return -1;
    }
    tensor_addrs.emplace_back(reinterpret_cast<uint64_t>(buffer));
    buffers.emplace_back(reinterpret_cast<void *>(buffer));
  }
  return 0;
}

inline int RegisterKvCache(llm_datadist::LlmDataDist &datadist, const llm_datadist::CacheDesc &cache_desc,
                           size_t tensor_size, uint32_t num_tensors, bool init_host_data, std::vector<void *> &buffers,
                           int64_t &cache_id) {
  std::vector<uint64_t> tensor_addrs;
  if (AllocateCacheBuffers(num_tensors, tensor_size, init_host_data, tensor_addrs, buffers) != 0) {
    return -1;
  }

  auto ret = datadist.RegisterKvCache(cache_desc, tensor_addrs, {}, cache_id);
  if (ret != llm_datadist::LLM_SUCCESS) {
    printf("[ERROR] RegisterKvCache failed, ret = %u, errmsg: %s\n", ret, GetAclRecentErrMsg());
    return -1;
  }

  printf("[INFO] RegisterKvCache success\n");
  for (size_t i = 0U; i < tensor_addrs.size(); ++i) {
    printf("[INFO] Tensor[%zu] addr = %p\n", i, reinterpret_cast<void *>(tensor_addrs[i]));
  }
  return 0;
}

inline int UnregisterKvCache(llm_datadist::LlmDataDist &datadist, int64_t cache_id) {
  if (cache_id <= 0) {
    return 0;
  }
  auto ret = datadist.UnregisterKvCache(cache_id);
  if (ret != llm_datadist::LLM_SUCCESS) {
    printf("[ERROR] UnregisterKvCache failed, ret = %u, errmsg: %s\n", ret, GetAclRecentErrMsg());
    return -1;
  }
  printf("[INFO] UnregisterKvCache success\n");
  return 0;
}

inline void FreeCacheBuffers(const std::vector<void *> &buffers) {
  for (auto buffer : buffers) {
    aclrtFree(buffer);
  }
}

inline void FinalizeCache(llm_datadist::LlmDataDist &datadist, int64_t cache_id, const std::vector<void *> &buffers) {
  (void)UnregisterKvCache(datadist, cache_id);
  FreeCacheBuffers(buffers);
  datadist.Finalize();
}

inline void ForceFinalizeCache(llm_datadist::LlmDataDist &datadist, const std::vector<void *> &buffers) {
  datadist.Finalize();
  FreeCacheBuffers(buffers);
}

template <typename UnlinkFn>
inline void FinalizeLinkedCache(llm_datadist::LlmDataDist &datadist, int64_t cache_id, bool linked,
                                UnlinkFn unlink_func, const std::vector<void *> &buffers) {
  bool can_unregister = true;
  if (linked) {
    auto ret = unlink_func();
    if (ret != 0) {
      printf("[ERROR] Unlink failed, ret = %d\n", ret);
      can_unregister = false;
    } else {
      printf("[INFO] Unlink success\n");
    }
  }
  if (can_unregister) {
    (void)UnregisterKvCache(datadist, cache_id);
  } else if (cache_id > 0) {
    printf("[WARN] Skip UnregisterKvCache since Unlink failed and cache may still be bound\n");
  }
  FreeCacheBuffers(buffers);
  datadist.Finalize();
}

inline int LinkClusters(llm_datadist::LlmDataDist &datadist, uint16_t remote_cluster_id, const char *local_ip,
                        uint16_t local_port, const char *remote_ip, uint16_t remote_port) {
  std::vector<llm_datadist::Status> rets;
  std::vector<llm_datadist::ClusterInfo> clusters;
  llm_datadist::ClusterInfo cluster_info;
  cluster_info.remote_cluster_id = remote_cluster_id;
  llm_datadist::IpInfo local_ip_info;
  local_ip_info.ip = local_ip;
  local_ip_info.port = local_port;
  cluster_info.local_ip_infos.emplace_back(std::move(local_ip_info));
  llm_datadist::IpInfo remote_ip_info;
  remote_ip_info.ip = remote_ip;
  remote_ip_info.port = remote_port;
  cluster_info.remote_ip_infos.emplace_back(std::move(remote_ip_info));
  clusters.emplace_back(std::move(cluster_info));
  auto ret = datadist.LinkLlmClusters(clusters, rets);
  if (ret != llm_datadist::LLM_SUCCESS) {
    printf("[ERROR] LinkLlmClusters failed, ret = %u, errmsg: %s\n", ret, GetAclRecentErrMsg());
    return -1;
  }
  printf("[INFO] LinkLlmClusters success\n");
  return 0;
}

inline int UnlinkClusters(llm_datadist::LlmDataDist &datadist, uint16_t remote_cluster_id, const char *remote_ip,
                          uint16_t remote_port) {
  std::vector<llm_datadist::Status> rets;
  std::vector<llm_datadist::ClusterInfo> clusters;
  llm_datadist::ClusterInfo cluster_info;
  cluster_info.remote_cluster_id = remote_cluster_id;
  llm_datadist::IpInfo remote_ip_info;
  remote_ip_info.ip = remote_ip;
  remote_ip_info.port = remote_port;
  cluster_info.remote_ip_infos.emplace_back(std::move(remote_ip_info));
  clusters.emplace_back(std::move(cluster_info));
  auto ret = datadist.UnlinkLlmClusters(clusters, rets);
  if (ret != llm_datadist::LLM_SUCCESS) {
    printf("[ERROR] UnlinkLlmClusters failed, ret = %u, errmsg: %s\n", ret, GetAclRecentErrMsg());
    return -1;
  }
  printf("[INFO] UnlinkLlmClusters success\n");
  return 0;
}

inline int CheckInt32Block(const std::vector<int32_t> &host_buffer, size_t tensor_block_element_num,
                           uint32_t check_index) {
  for (size_t i = 0U; i < tensor_block_element_num; ++i) {
    auto expect = check_index * tensor_block_element_num + i;
    if (static_cast<uint32_t>(host_buffer[expect]) != expect) {
      printf("[ERROR] Buffer check failed, index = %zu, val = %d, expect val = %zu\n", expect, host_buffer[expect],
             expect);
      return -1;
    }
  }
  return 0;
}

inline int CheckInt32Buffers(const std::vector<void *> &buffers, size_t tensor_size, size_t tensor_block_element_num,
                             const std::vector<uint32_t> &check_index_list) {
  for (auto buffer : buffers) {
    std::vector<int32_t> host_buffer(tensor_size / sizeof(int32_t));
    aclError ret = aclrtMemcpy(host_buffer.data(), tensor_size, buffer, tensor_size, ACL_MEMCPY_DEVICE_TO_HOST);
    if (ret != ACL_ERROR_NONE) {
      printf("[ERROR] aclrtMemcpy failed, ret = %d, errmsg: %s\n", ret, GetAclRecentErrMsg());
      return -1;
    }
    for (auto check_index : check_index_list) {
      if (CheckInt32Block(host_buffer, tensor_block_element_num, check_index) != 0) {
        return -1;
      }
    }
  }
  printf("[INFO] CheckBuffers success\n");
  return 0;
}

inline int PullKvCacheBlocksAndBatch(llm_datadist::LlmDataDist &datadist, int64_t local_cache_id,
                                     uint16_t remote_cluster_id, int64_t remote_cache_id,
                                     const std::vector<uint64_t> &src_blocks, const std::vector<uint64_t> &dst_blocks,
                                     uint32_t batch_index) {
  llm_datadist::CacheIndex cache_index = {};
  cache_index.cluster_id = remote_cluster_id;
  cache_index.cache_id = remote_cache_id;
  cache_index.batch_index = 0;
  llm_datadist::Cache cache{};
  cache.cache_id = local_cache_id;
  auto ret = datadist.PullKvBlocks(cache_index, cache, src_blocks, dst_blocks);
  if (ret != llm_datadist::LLM_SUCCESS) {
    printf("[ERROR] PullKvBlocks failed, ret = %u, errmsg: %s\n", ret, GetAclRecentErrMsg());
    return -1;
  }
  printf("[INFO] PullKvBlocks success\n");

  cache_index.batch_index = batch_index;
  ret = datadist.PullKvCache(cache_index, cache, batch_index);
  if (ret != llm_datadist::LLM_SUCCESS) {
    printf("[ERROR] PullKvCache failed, ret = %u, errmsg: %s\n", ret, GetAclRecentErrMsg());
    return -1;
  }
  printf("[INFO] PullKvCache success\n");
  return 0;
}

inline int PushKvCacheBlocksAndBatch(llm_datadist::LlmDataDist &datadist, int64_t local_cache_id,
                                     uint16_t remote_cluster_id, int64_t remote_cache_id,
                                     const std::vector<uint64_t> &src_blocks, const std::vector<uint64_t> &dst_blocks,
                                     uint32_t batch_index, uint8_t tensor_num_per_layer, uint32_t num_tensors) {
  llm_datadist::Cache cache{};
  cache.cache_id = local_cache_id;
  llm_datadist::CacheIndex cache_index = {};
  cache_index.cluster_id = remote_cluster_id;
  cache_index.cache_id = remote_cache_id;
  for (uint32_t i = 0U; i < num_tensors; ++i) {
    llm_datadist::KvCacheExtParam param{};
    param.src_layer_range = std::pair<int32_t, int32_t>(i, i);
    param.dst_layer_range = std::pair<int32_t, int32_t>(i, i);
    param.tensor_num_per_layer = 1;
    auto ret = datadist.PushKvBlocks(cache, cache_index, src_blocks, dst_blocks, param);
    if (ret != llm_datadist::LLM_SUCCESS) {
      printf("[ERROR] PushKvBlocks failed, ret = %u, errmsg: %s\n", ret, GetAclRecentErrMsg());
      return -1;
    }
  }
  printf("[INFO] PushKvBlocks success\n");

  llm_datadist::CacheIndex batch_cache_index = {};
  batch_cache_index.cluster_id = remote_cluster_id;
  batch_cache_index.cache_id = remote_cache_id;
  batch_cache_index.batch_index = batch_index;
  llm_datadist::KvCacheExtParam param{};
  param.src_layer_range = std::pair<int32_t, int32_t>(0, 0);
  param.dst_layer_range = std::pair<int32_t, int32_t>(0, 0);
  param.tensor_num_per_layer = tensor_num_per_layer;
  auto ret = datadist.PushKvCache(cache, batch_cache_index, batch_index, -1, param);
  if (ret != llm_datadist::LLM_SUCCESS) {
    printf("[ERROR] PushKvCache failed, ret = %u, errmsg: %s\n", ret, GetAclRecentErrMsg());
    return -1;
  }
  printf("[INFO] PushKvCache success\n");
  return 0;
}
}  // namespace sample

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
