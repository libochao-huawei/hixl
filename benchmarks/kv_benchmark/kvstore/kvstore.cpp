/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "kvstore.h"

#include <utility>

namespace hixl_kv_benchmark {

KvStore::KvStore(std::uint64_t seed, SegmentManager segment_manager)
    : segment_manager_(std::move(segment_manager)), rng_(seed) {}

bool KvStore::CheckInput(const std::vector<std::string> &keys, const std::vector<BufferView> &buffers) const {
  if (keys.empty() || keys.size() != buffers.size()) {
    return false;
  }
  for (const auto &buffer : buffers) {
    if (buffer.addr == 0U || buffer.size == 0U) {
      return false;
    }
  }
  return true;
}

bool KvStore::EnsurePlacement(const std::string &key, std::uint64_t size, KeyPlacement *placement) {
  const auto it = placements_.find(key);
  if (it != placements_.end()) {
    *placement = it->second;
    return it->second.size == size;
  }
  const auto segment_count = static_cast<std::uint32_t>(segment_manager_.Segments().size());
  if (segment_count == 0U) {
    return false;
  }
  std::uniform_int_distribution<std::uint32_t> dist(0U, segment_count - 1U);
  const auto allocation = segment_manager_.AllocateFrom(dist(rng_), size);
  if (!allocation.has_value()) {
    return false;
  }
  *placement = KeyPlacement{key, allocation->segment_id, allocation->offset, allocation->size};
  placements_[key] = *placement;
  return true;
}

bool KvStore::batch_put_from_multi_buffers(const std::vector<std::string> &keys,
                                           const std::vector<BufferView> &source_buffers) {
  if (!CheckInput(keys, source_buffers)) {
    return false;
  }
  last_requests_.clear();
  last_requests_.reserve(keys.size());
  for (std::size_t i = 0; i < keys.size(); ++i) {
    KeyPlacement placement;
    if (!EnsurePlacement(keys[i], source_buffers[i].size, &placement)) {
      return false;
    }
    last_requests_.push_back(TransferRequest{keys[i], "d2rh", source_buffers[i], placement});
  }
  return true;
}

bool KvStore::ensure_placements(const std::vector<std::string> &keys,
                                const std::vector<BufferView> &buffers) {
  if (!CheckInput(keys, buffers)) {
    return false;
  }
  for (std::size_t i = 0; i < keys.size(); ++i) {
    KeyPlacement placement;
    if (!EnsurePlacement(keys[i], buffers[i].size, &placement)) {
      return false;
    }
  }
  return true;
}

bool KvStore::batch_get_into_multi_buffers(const std::vector<std::string> &keys,
                                           const std::vector<BufferView> &destination_buffers) {
  if (!CheckInput(keys, destination_buffers)) {
    return false;
  }
  last_requests_.clear();
  last_requests_.reserve(keys.size());
  for (std::size_t i = 0; i < keys.size(); ++i) {
    const auto it = placements_.find(keys[i]);
    if (it == placements_.end() || it->second.size != destination_buffers[i].size) {
      return false;
    }
    last_requests_.push_back(TransferRequest{keys[i], "rh2d", destination_buffers[i], it->second});
  }
  return true;
}

}  // namespace hixl_kv_benchmark
