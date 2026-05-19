/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hixl_wrapper.h"
#include "hixl/hixl.h"
#include "hixl/hixl_types.h"
#include <cstring>
#include <map>
#include <securec.h>
#include <vector>
#include <string>

extern "C" {

HixlHandle HixlCreate() {
  return new hixl::Hixl();
}

void HixlDestroy(HixlHandle handle) {
  if (handle) {
    delete static_cast<hixl::Hixl *>(handle);
  }
}

uint32_t HixlInitialize(HixlHandle handle, const char *local_engine, const char **option_keys, const char **option_vals,
                        int option_count) {
  auto *hixl = static_cast<hixl::Hixl *>(handle);
  if (!hixl) return hixl::PARAM_INVALID;

  std::map<hixl::AscendString, hixl::AscendString> options;
  for (int i = 0; i < option_count; ++i) {
    options[hixl::AscendString(option_keys[i])] = hixl::AscendString(option_vals[i]);
  }

  return hixl->Initialize(hixl::AscendString(local_engine), options);
}

void HixlFinalize(HixlHandle handle) {
  auto *hixl = static_cast<hixl::Hixl *>(handle);
  if (hixl) {
    hixl->Finalize();
  }
}

uint32_t HixlRegisterMem(HixlHandle handle, uintptr_t addr, size_t len, int mem_type, void **mem_handle) {
  auto *hixl = static_cast<hixl::Hixl *>(handle);
  if (!hixl || !mem_handle) return hixl::PARAM_INVALID;

  hixl::MemDesc desc;
  desc.addr = addr;
  desc.len = len;

  hixl::MemHandle handle_out;
  auto status = hixl->RegisterMem(desc, static_cast<hixl::MemType>(mem_type), handle_out);
  *mem_handle = handle_out;
  return status;
}

uint32_t HixlDeregisterMem(HixlHandle handle, void *mem_handle) {
  auto *hixl = static_cast<hixl::Hixl *>(handle);
  if (!hixl) return hixl::PARAM_INVALID;

  return hixl->DeregisterMem(mem_handle);
}

uint32_t HixlConnect(HixlHandle handle, const char *remote_engine, int32_t timeout_ms) {
  auto *hixl = static_cast<hixl::Hixl *>(handle);
  if (!hixl) return hixl::PARAM_INVALID;

  return hixl->Connect(hixl::AscendString(remote_engine), timeout_ms);
}

uint32_t HixlDisconnect(HixlHandle handle, const char *remote_engine, int32_t timeout_ms) {
  auto *hixl = static_cast<hixl::Hixl *>(handle);
  if (!hixl) return hixl::PARAM_INVALID;

  return hixl->Disconnect(hixl::AscendString(remote_engine), timeout_ms);
}

uint32_t HixlTransferSync(HixlHandle handle, const char *remote_engine, int operation, const uintptr_t *local_addrs,
                          const uintptr_t *remote_addrs, const size_t *lengths, int desc_count, int32_t timeout_ms) {
  auto *hixl = static_cast<hixl::Hixl *>(handle);
  if (!hixl || !remote_engine) return hixl::PARAM_INVALID;
  if (desc_count > 0 && (!local_addrs || !remote_addrs || !lengths)) return hixl::PARAM_INVALID;

  std::vector<hixl::TransferOpDesc> descs;
  for (int i = 0; i < desc_count; ++i) {
    descs.push_back({local_addrs[i], remote_addrs[i], lengths[i]});
  }

  return hixl->TransferSync(hixl::AscendString(remote_engine), static_cast<hixl::TransferOp>(operation), descs,
                            timeout_ms);
}

uint32_t HixlTransferAsync(HixlHandle handle, const char *remote_engine, int operation, const uintptr_t *local_addrs,
                           const uintptr_t *remote_addrs, const size_t *lengths, int desc_count, void *optional_args,
                           void **req_handle) {
  auto *hixl = static_cast<hixl::Hixl *>(handle);
  if (!hixl || !remote_engine || !req_handle) return hixl::PARAM_INVALID;
  if (desc_count <= 0) return hixl::PARAM_INVALID;
  if (!local_addrs || !remote_addrs || !lengths) return hixl::PARAM_INVALID;

  std::vector<hixl::TransferOpDesc> descs;
  for (int i = 0; i < desc_count; ++i) {
    descs.push_back({local_addrs[i], remote_addrs[i], lengths[i]});
  }

  hixl::TransferArgs args;
  if (optional_args) {
    errno_t rc = memcpy_s(args.reserved, sizeof(args.reserved), optional_args, sizeof(args.reserved));
    if (rc != EOK) {
      return hixl::PARAM_INVALID;
    }
  }

  hixl::TransferReq req;
  auto status = hixl->TransferAsync(hixl::AscendString(remote_engine), static_cast<hixl::TransferOp>(operation), descs,
                                    args, req);
  *req_handle = req;
  return status;
}

uint32_t HixlGetTransferStatus(HixlHandle handle, void *req_handle, int *status) {
  auto *hixl = static_cast<hixl::Hixl *>(handle);
  if (!hixl || !req_handle || !status) return hixl::PARAM_INVALID;

  hixl::TransferStatus ts;
  auto ret = hixl->GetTransferStatus(req_handle, ts);
  *status = static_cast<int>(ts);
  return ret;
}

uint32_t HixlSendNotify(HixlHandle handle, const char *remote_engine, const char *notify_name, const char *notify_msg,
                        int32_t timeout_ms) {
  auto *hixl = static_cast<hixl::Hixl *>(handle);
  if (!hixl) return hixl::PARAM_INVALID;

  hixl::NotifyDesc notify;
  notify.name = hixl::AscendString(notify_name);
  notify.notify_msg = hixl::AscendString(notify_msg);

  return hixl->SendNotify(hixl::AscendString(remote_engine), notify, timeout_ms);
}

uint32_t HixlGetNotifies(HixlHandle handle, char ***notify_names, char ***notify_msgs, int *notify_count) {
  auto *hixl = static_cast<hixl::Hixl *>(handle);
  if (!hixl || !notify_names || !notify_msgs || !notify_count) return hixl::PARAM_INVALID;

  std::vector<hixl::NotifyDesc> notifies;
  auto status = hixl->GetNotifies(notifies);

  *notify_count = static_cast<int>(notifies.size());
  if (notifies.empty()) {
    *notify_names = nullptr;
    *notify_msgs = nullptr;
    return status;
  }

  *notify_names = new (std::nothrow) char *[notifies.size()];
  *notify_msgs = new (std::nothrow) char *[notifies.size()];
  if (*notify_names == nullptr || *notify_msgs == nullptr) {
    delete[] *notify_names;
    delete[] *notify_msgs;
    *notify_names = nullptr;
    *notify_msgs = nullptr;
    return hixl::RESOURCE_EXHAUSTED;
  }

  for (size_t i = 0; i < notifies.size(); ++i) {
    const char *name_str = notifies[i].name.GetString();
    const char *msg_str = notifies[i].notify_msg.GetString();
    size_t name_len = strlen(name_str);
    size_t msg_len = strlen(msg_str);
    (*notify_names)[i] = new (std::nothrow) char[name_len + 1];
    (*notify_msgs)[i] = new (std::nothrow) char[msg_len + 1];
    if ((*notify_names)[i] == nullptr || (*notify_msgs)[i] == nullptr) {
      for (size_t j = 0; j <= i; ++j) {
        delete[] (*notify_names)[j];
        delete[] (*notify_msgs)[j];
      }
      delete[] *notify_names;
      delete[] *notify_msgs;
      *notify_names = nullptr;
      *notify_msgs = nullptr;
      return hixl::RESOURCE_EXHAUSTED;
    }
    errno_t name_rc = memcpy_s((*notify_names)[i], name_len + 1, name_str, name_len + 1);
    errno_t msg_rc = memcpy_s((*notify_msgs)[i], msg_len + 1, msg_str, msg_len + 1);
    if (name_rc != EOK || msg_rc != EOK) {
      for (size_t j = 0; j <= i; ++j) {
        delete[] (*notify_names)[j];
        delete[] (*notify_msgs)[j];
      }
      delete[] *notify_names;
      delete[] *notify_msgs;
      *notify_names = nullptr;
      *notify_msgs = nullptr;
      return hixl::FAILED;
    }
  }

  return status;
}

void HixlFreeNotifies(char **notify_names, char **notify_msgs, int count) {
  if (notify_names) {
    for (int i = 0; i < count; ++i) {
      delete[] notify_names[i];
    }
    delete[] notify_names;
  }
  if (notify_msgs) {
    for (int i = 0; i < count; ++i) {
      delete[] notify_msgs[i];
    }
    delete[] notify_msgs;
  }
}

}  // extern "C"
