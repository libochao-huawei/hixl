/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "channel.h"

#include "common/hixl_utils.h"

namespace hixl {

Status Channel::Create(EndPointHandle ep_handle, HcommChannelDesc &ch_desc, CommEngine engine) {
  HIXL_CHK_BOOL_RET_STATUS(ep_handle != nullptr, PARAM_INVALID, "Channel::Create called with null endpoint handle");
  constexpr uint32_t list_num = 1U;
  ChannelHandle ch_list[1] = {};
  HIXL_LOGI("[JZY]channel:protocol=%d", ch_desc.remoteEndpoint.protocol);
  HIXL_LOGI("[JZY]ep_handle=%p",ep_handle);
  Status ret = 0;
  try {
    HIXL_LOGI("[JZY]HcommChannelCreate start");
    HIXL_LOGI("[JZY][channel.cc] ch_desc.remoteEndpoint.loc.device.devPhyId=%u", ch_desc.remoteEndpoint.loc.device.devPhyId);
    ret = HcommChannelCreate(ep_handle, engine, &ch_desc, list_num, ch_list);
    HIXL_LOGI("[JZY]HcommChannelCreate end");

  } catch (const std::exception &e) {
    HIXL_LOGE(FAILED, "[JZY] e=%s", e.what());
  }
  HIXL_LOGI("[JZY] HcommChannelCreate ret=%d", ret);
  if (ret != HCCL_SUCCESS) {
    HIXL_LOGE(FAILED, "[JZY] HcommChannelCreate ERROR");
    return FAILED;
  }
  channel_handle_ = ch_list[0];
  HIXL_LOGI("Channel::Create success, handle=%lu", channel_handle_);
  return SUCCESS;
}

ChannelHandle Channel::GetChannelHandle() const {
  return channel_handle_;
}

Status Channel::GetStatus(ChannelHandle channel_handle, int32_t *status_out) {
  HIXL_CHECK_NOTNULL(status_out);
  HIXL_CHK_BOOL_RET_STATUS(channel_handle != 0, PARAM_INVALID, "Channel handle is invalid(0)");
  const ChannelHandle ch_list[1] = {channel_handle};
  int32_t status_list[1] = {0};
  auto ret = HcommChannelGetStatus(ch_list, 1U, status_list);
  if (ret == HCCL_E_AGAIN) {
    return 20;
  }
  *status_out = status_list[0];
  // 底层约定：0 表示 ready，其它值表示“尚未 ready 或失败”
  if (*status_out != 0) {
    HIXL_LOGD("Channel query success but not ready. channel_handle=%p, status_out=%d",
              channel_handle, *status_out);
  }
  return SUCCESS;
}

Status Channel::Destroy() {
  const ChannelHandle ch_list[1] = {channel_handle_};
  HIXL_CHK_HCCL_RET(HcommChannelDestroy(ch_list, 1U));

  HIXL_LOGI("Channel::Destroy success, handle=%lu", channel_handle_);
  return SUCCESS;
}

}  // namespace hixl