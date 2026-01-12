/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "mem_msg_handler.h"

#include <cstring>
#include <cinttypes>
#include <algorithm>
#include "nlohmann/json.hpp"
#include "common/ctrl_msg_plugin.h"

#include <securec.h>

namespace {
constexpr uint64_t kMaxGetRemoteMemBodySize = static_cast<uint64_t>(4ULL * 1024ULL * 1024ULL);  // 4MB 示例上限
constexpr uint32_t kMaxGetRemoteMemNum = 4096U;                                                 // 最多 4096 段远端内存
}  // namespace

namespace hixl {

Status MemMsgHandler::SendGetRemoteMemRequest(int32_t socket, uint64_t endpoint_handle, uint32_t timeout_ms) {
  HIXL_EVENT("[HixlClient] SendGetRemoteMemRequest start. socket: %d, endpoint_handle: %lu",
             socket, endpoint_handle);
  (void)timeout_ms;
  CtrlMsgHeader header{};
  header.magic = kMagicNumber;
  header.body_size = static_cast<uint64_t>(sizeof(CtrlMsgType) + sizeof(GetRemoteMemReq));

  CtrlMsgType msg_type = CtrlMsgType::kGetRemoteMemReq;

  GetRemoteMemReq body{};
  body.dst_ep_handle = endpoint_handle;

  HIXL_CHK_STATUS_RET(CtrlMsgPlugin::Send(socket, &header, static_cast<uint64_t>(sizeof(header))));
  HIXL_CHK_STATUS_RET(CtrlMsgPlugin::Send(socket, &msg_type, static_cast<uint64_t>(sizeof(msg_type))));
  HIXL_CHK_STATUS_RET(CtrlMsgPlugin::Send(socket, &body, static_cast<uint64_t>(sizeof(body))));
  HIXL_EVENT("[HixlClient] SendGetRemoteMemRequest success. fd=%d", socket);
  return SUCCESS;
}

void FreeExportDesc(std::vector<hixl::HixlMemDesc> &descs) {
  for (auto &d : descs) {
    if (d.export_desc != nullptr) {
      std::free(d.export_desc);
      d.export_desc = nullptr;
      d.export_len = 0U;
    }
  }
  descs.clear();
}

Status RecvAndCheckHeader(int32_t socket, uint64_t &body_size, uint32_t timeout_ms) {
  HIXL_LOGI("[HixlClient] RecvAndCheckHeader start. socket: %d", socket);
  CtrlMsgHeader header{};
  HIXL_CHK_STATUS_RET(CtrlMsgPlugin::Recv(socket, &header, static_cast<uint32_t>(sizeof(header)), timeout_ms));
  HIXL_EVENT("[HixlClient] RecvGetRemoteMemResp header ok. fd=%d, body_size=%" PRIu64, socket, header.body_size);
  HIXL_LOGD("[HixlClient] Header received. magic: 0x%X, body_size: %lu", header.magic, header.body_size);
  HIXL_CHK_BOOL_RET_STATUS(header.magic == kMagicNumber, PARAM_INVALID,
                           "[HixlClient] Invalid magic in GetRemoteMemResp, expect:0x%X, actual:0x%X", kMagicNumber, header.magic);

  HIXL_CHK_BOOL_RET_STATUS(
      header.body_size > sizeof(CtrlMsgType) && header.body_size <= kMaxGetRemoteMemBodySize, PARAM_INVALID,
      "[HixlClient] Invalid body_size in GetRemoteMemResp, body_size=%" PRIu64 ", must be in (%zu, %" PRIu64 "]", header.body_size,
      sizeof(CtrlMsgType), kMaxGetRemoteMemBodySize);

  body_size = header.body_size;
  return SUCCESS;
}

Status RecvBody(int32_t socket, uint64_t body_size, std::vector<uint8_t> &body, uint32_t timeout_ms) {
  HIXL_LOGI("[HixlClient] RecvBody start. body_size: %lu", body_size);
  body.resize(static_cast<size_t>(body_size));
  HIXL_EVENT("[HixlClient] RecvGetRemoteMemResp body begin. fd=%d, body_size=%" PRIu64, socket, body_size);
  HIXL_CHK_STATUS_RET(CtrlMsgPlugin::Recv(socket, body.data(), static_cast<uint32_t>(body_size), timeout_ms));
  HIXL_EVENT("[HixlClient] RecvGetRemoteMemResp body ok. fd=%d, body_size=%" PRIu64, socket, body_size);
  return SUCCESS;
}

Status ExtractTypeAndJsonPtr(const std::vector<uint8_t> &body, CtrlMsgType &msg_type, const char *&json_ptr,
                             size_t &json_len) {
  const uint64_t body_size = static_cast<uint64_t>(body.size());
  const void *src = static_cast<const void *>(body.data());

  errno_t rc = memcpy_s(&msg_type, sizeof(msg_type), src, sizeof(msg_type));
  HIXL_CHK_BOOL_RET_STATUS(rc == EOK, FAILED, "[HixlClient] memcpy_s msg_type failed, rc=%d", static_cast<int32_t>(rc));

  HIXL_LOGD("[HixlClient] Parsed MsgType: %d", static_cast<int32_t>(msg_type));
  HIXL_CHK_BOOL_RET_STATUS(msg_type == CtrlMsgType::kGetRemoteMemResp, PARAM_INVALID,
                           "[HixlClient] Unexpected msg_type=%d, expect=%d", static_cast<int32_t>(msg_type),
                           static_cast<int32_t>(CtrlMsgType::kGetRemoteMemResp));

  json_len = static_cast<size_t>(body_size - sizeof(CtrlMsgType));
  json_ptr = reinterpret_cast<const char *>(body.data() + sizeof(CtrlMsgType));

  HIXL_LOGD("[HixlClient] Extracted JSON length: %zu", json_len);
  return SUCCESS;
}

Status ParseResultAndGetArray(const nlohmann::json &j, const nlohmann::json *&arr_out) {
  try {
    if (!j.contains("result")) {
      HIXL_LOGE(PARAM_INVALID, "[HixlClient] GetRemoteMemResp json has no field 'result'");
      return PARAM_INVALID;
    }

    Status result = static_cast<Status>(j["result"].get<uint32_t>());
    if (result != SUCCESS) {
      HIXL_LOGE(result, "[HixlClient] GetRemoteMemResp result not SUCCESS, result=%u", static_cast<uint32_t>(result));
      return result;
    }

    if (!j.contains("mem_descs") || !j["mem_descs"].is_array()) {
      HIXL_LOGE(PARAM_INVALID, "[HixlClient] GetRemoteMemResp json 'mem_descs' is missing or not array");
      return PARAM_INVALID;
    }

    arr_out = &j["mem_descs"];
    return SUCCESS;
  } catch (const nlohmann::json::exception &e) {
    HIXL_LOGE(PARAM_INVALID, "[HixlClient] JSON error in ParseResultAndGetArray: %s", e.what());
    return PARAM_INVALID;
  } catch (...) {
    HIXL_LOGE(FAILED, "[HixlClient] Unknown error in ParseResultAndGetArray");
    return FAILED;
  }
}

Status ParseMemObject(const nlohmann::json &j_mem, HcclMem &mem) {
  try {
    if (!j_mem.contains("type") || !j_mem.contains("addr") || !j_mem.contains("size")) {
      HIXL_LOGE(PARAM_INVALID, "[HixlClient] GetRemoteMemResp.mem missing 'type' / 'addr' / 'size'");
      return PARAM_INVALID;
    }

    mem.type = static_cast<HcclMemType>(j_mem["type"].get<uint32_t>());
    const uint64_t addr_u64 = j_mem["addr"].get<uint64_t>();
    mem.addr = reinterpret_cast<void *>(static_cast<uintptr_t>(addr_u64));
    mem.size = j_mem["size"].get<uint64_t>();
    return SUCCESS;
  } catch (const nlohmann::json::exception &e) {
    HIXL_LOGE(PARAM_INVALID, "[HixlClient] JSON error in ParseMemObject: %s", e.what());
    return PARAM_INVALID;
  } catch (...) {
    HIXL_LOGE(FAILED, "[HixlClient] Unknown error in ParseMemObject");
    return FAILED;
  }
}

Status FillExportDescFromString(const std::string &export_str, hixl::HixlMemDesc &desc) {
  desc.export_len = static_cast<uint32_t>(export_str.size());
  if (desc.export_len == 0U) {
    desc.export_desc = nullptr;
    return SUCCESS;
  }

  void *buf = std::malloc(desc.export_len);
  if (buf == nullptr) {
    HIXL_LOGE(FAILED, "[HixlClient] malloc export_desc buffer failed, len=%u", desc.export_len);
    return FAILED;
  }

  errno_t rc = memcpy_s(buf, desc.export_len, export_str.data(), desc.export_len);
  if (rc != EOK) {
    HIXL_LOGE(FAILED, "[HixlClient] memcpy_s export_desc failed, rc=%d, len=%u", static_cast<int32_t>(rc), desc.export_len);
    std::free(buf);
    return FAILED;
  }

  desc.export_desc = buf;
  return SUCCESS;
}

static Status BuildExportString(const nlohmann::json& exp, std::string& export_str, uint32_t idx) {
  if (exp.is_string()) {
    export_str = exp.get<std::string>();
    return SUCCESS;
  }
  if (!exp.is_array()) {
    HIXL_LOGE(PARAM_INVALID, "[HixlClient] mem_descs[%u].export_desc type invalid", idx);
    return PARAM_INVALID;
  }
  export_str.reserve(exp.size());
  for (const auto &v : exp) {
    if (!v.is_number_unsigned()) {
      HIXL_LOGE(PARAM_INVALID, "[HixlClient] mem_descs[%u].export_desc has non-unsigned element", idx);
      return PARAM_INVALID;
    }
    uint64_t x = v.get<uint64_t>();
    if (x > 255) { // 限制mem_descs数组中元素个数不超过255
      HIXL_LOGE(PARAM_INVALID, "[HixlClient] mem_descs[%u].export_desc element >255", idx);
      return PARAM_INVALID;
    }
    export_str.push_back(static_cast<char>(static_cast<unsigned char>(x)));
  }
  return SUCCESS;
}

Status ParseOneMemDesc(const nlohmann::json &item, uint32_t idx, hixl::HixlMemDesc &out) {
  if (!item.contains("tag") || !item.contains("export_desc") ||
  !item.contains("mem") || !item["mem"].is_object()) {
    HIXL_LOGE(PARAM_INVALID, "[HixlClient] GetRemoteMemResp.mem_descs[%u] missing 'tag' / 'export_desc' or 'mem'", idx);
    return PARAM_INVALID;
  }

  HcclMem mem{};
  Status ret = ParseMemObject(item["mem"], mem);
  HIXL_CHK_STATUS_RET(ret);
  out.mem = mem;

  if (!item["tag"].is_string()) {
    HIXL_LOGE(PARAM_INVALID, "[HixlClient] mem_descs[%u].tag not string", idx);
    return PARAM_INVALID;
  }
  out.tag = item["tag"].get<std::string>();

  std::string export_str;
  ret = BuildExportString(item["export_desc"], export_str, idx);
  HIXL_CHK_STATUS_RET(ret);

  return FillExportDescFromString(export_str, out);
}

Status ParseMemDescsArray(const nlohmann::json &arr, std::vector<hixl::HixlMemDesc> &mem_descs) {
  const uint32_t mem_num = static_cast<uint32_t>(arr.size());
  HIXL_LOGD("[HixlClient] Parsing mem_descs array, size: %u", mem_num);
  HIXL_CHK_BOOL_RET_STATUS(mem_num <= kMaxGetRemoteMemNum, PARAM_INVALID,
                           "[HixlClient] mem_num too large in GetRemoteMemResp, mem_num=%u, max=%u", mem_num, kMaxGetRemoteMemNum);

  mem_descs.clear();
  mem_descs.reserve(mem_num);

  for (uint32_t i = 0; i < mem_num; ++i) {
    hixl::HixlMemDesc desc{};
    Status ret = ParseOneMemDesc(arr[i], i, desc);
    if (ret != SUCCESS) {
      FreeExportDesc(mem_descs);
      return ret;
    }
    mem_descs.emplace_back(std::move(desc));
  }
  return SUCCESS;
}

Status ParseGetRemoteMemJson(const char* json_ptr, size_t json_len, std::vector<hixl::HixlMemDesc> &mem_descs) {
  HIXL_LOGD("[HixlClient] Parsing JSON from memory address %p, len %zu", json_ptr, json_len);
  nlohmann::json j;
  try {
    j = nlohmann::json::parse(json_ptr, json_ptr + json_len);
  } catch (const nlohmann::json::exception &e) {
    HIXL_LOGE(PARAM_INVALID, "[HixlClient] Failed to parse GetRemoteMemResp json, exception:%s", e.what());
    return PARAM_INVALID;
  } catch (...) {
    HIXL_LOGE(FAILED, "[HixlClient] Unknown error during json parse");
    return FAILED;
  }

  const nlohmann::json *arr = nullptr;
  Status ret = ParseResultAndGetArray(j, arr);
  HIXL_CHK_STATUS_RET(ret);

  return ParseMemDescsArray(*arr, mem_descs);
}

Status MemMsgHandler::RecvGetRemoteMemResponse(int32_t socket, std::vector<HixlMemDesc> &mem_descs,
                                               uint32_t timeout_ms) {
  HIXL_EVENT("[HixlClient] RecvGetRemoteMemResponse start. socket: %d, timeout_ms: %u", socket, timeout_ms);
  uint64_t body_size = 0;
  HIXL_CHK_STATUS_RET(RecvAndCheckHeader(socket, body_size, timeout_ms));
  std::vector<uint8_t> body;
  HIXL_CHK_STATUS_RET(RecvBody(socket, body_size, body, timeout_ms));
  CtrlMsgType msg_type{};
  const char* json_ptr = nullptr;
  size_t json_len = 0;
  HIXL_CHK_STATUS_RET(ExtractTypeAndJsonPtr(body, msg_type, json_ptr, json_len));
  Status ret = ParseGetRemoteMemJson(json_ptr, json_len, mem_descs);
  if (ret == SUCCESS) {
    HIXL_EVENT("[HixlClient] RecvGetRemoteMemResponse success. Parsed %zu mem descriptors.", mem_descs.size());
  } else {
    HIXL_LOGE(ret, "[HixlClient] RecvGetRemoteMemResponse failed during parsing.");
  }
  return ret;
}
}  // namespace hixl
