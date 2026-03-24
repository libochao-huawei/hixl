/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "prof_api_reg.h"
#include <mutex>
#include <unordered_set>
#include <map>
#include "mmpa/mmpa_api.h"
#include "hixl_checker.h"
#include "common/hixl_log.h"
#include "hixl/hixl_types.h"

namespace {
  static bool prof_run = false;
  static std::mutex prof_mutex;
  static std::unordered_set<uint32_t> device_list;
  constexpr uint64_t ACL_PROF_HIXL_API = 0x0001U;
  constexpr uint32_t START_PROFILING = 1U;
  constexpr uint32_t STOP_PROFILING = 2U;

  static const std::map<hixl::HixlProfType, std::string> PROF_TYPE_TO_NAMES = {
    {hixl::HixlProfType::HixlProfTypeRead,                       "hixlProfTypeRead"},
    {hixl::HixlProfType::HixlProfTypeWrite,                      "hixlProfTypeWrite"},
  };

  static hixl::Status RegisterProfType() {
    for (auto &iter: PROF_TYPE_TO_NAMES) {
      uint32_t type_id = static_cast<uint32_t>(iter.first);
      const auto ret = MsprofRegTypeInfo(MSPROF_REPORT_ACL_LEVEL, type_id, iter.second.c_str());
      HIXL_CHK_BOOL_RET_STATUS(ret == MSPROF_ERROR_NONE, hixl::FAILED, "Registered api type [%u] failed = %d", type_id, ret);
    }
    return hixl::SUCCESS;
  }

  static hixl::Status AddDeviceList(const uint32_t *const device_id_list, const uint32_t device_nums) {
    HIXL_CHECK_NOTNULL(device_id_list, "device_id_list is null, please check");
    for (size_t dev_id = 0; dev_id < device_nums; dev_id++) {
      if (device_list.count(*(device_id_list + dev_id)) == 0U) {
        (void)device_list.insert(*(device_id_list + dev_id));
        HIXL_LOGI("device id %u is successfully added in hixl profiling", *(device_id_list + dev_id));
      }
    }
    return hixl::SUCCESS;
  }

  static hixl::Status RemoveDeviceList(const uint32_t *const device_id_list, const uint32_t device_nums) {
    HIXL_CHECK_NOTNULL(device_id_list, "device_id_list is null, please check");
    for (size_t dev_id = 0; dev_id < device_nums; dev_id++) {
      const auto iter = device_list.find(*(device_id_list + dev_id));
      if (iter != device_list.end()) {
        device_list.erase(iter);
        HIXL_LOGI("device id %u is successfully deleted from acl profiling", *(device_id_list + dev_id));
      }
    }
    return hixl::SUCCESS;
  }

  static hixl::Status ProfInnerStart(const MsprofCommandHandle *const profiler_config) {
    HIXL_LOGI("start to execute profInnerStart");
    if (!prof_run) {
      RegisterProfType();
      prof_run = true;
    }
    (void)AddDeviceList(profiler_config->devIdList, profiler_config->devNums);
    HIXL_LOGI("successfully execute ProfInnerStart");
    return hixl::SUCCESS;
  }

  static hixl::Status ProfInnerStop(const MsprofCommandHandle *const profiler_config) {
    HIXL_LOGI("start to exectue profInnerStop");
    (void)RemoveDeviceList(profiler_config->devIdList, profiler_config->devNums);
    if (device_list.empty() && prof_run) {
      prof_run = false;
    }
    HIXL_LOGI("successfully execute ProfInnerStop");
    return hixl::SUCCESS;
  }

  static hixl::Status ProcessProfData(void *const data, const uint32_t len) {
    HIXL_LOGI("start to exectue ProcessProfData");
    const std::lock_guard<std::mutex> lk(prof_mutex);
    HIXL_CHECK_NOTNULL(data, "data is null, please check");
    constexpr size_t command_len = sizeof(data);
    if (len < command_len) {
      HIXL_LOGE(hixl::PARAM_INVALID, "[Check][Len]len[%u] is invalid, it should not be smaller than %zu", len, command_len);
      return hixl::PARAM_INVALID;
    }
    MsprofCommandHandle *const profiler_config = static_cast<MsprofCommandHandle *>(data);
    hixl::Status ret = hixl::SUCCESS;
    const uint64_t prof_switch = profiler_config->profSwitch;
    const uint32_t type = profiler_config->type;
    if (((prof_switch & ACL_PROF_HIXL_API) != 0U) && (type == START_PROFILING)) {
      ret = ProfInnerStart(profiler_config);
    }
    if (((prof_switch & ACL_PROF_HIXL_API) != 0U) && (type == STOP_PROFILING)) {
      ret = ProfInnerStop(profiler_config);
    }
    return ret;
  }

  static hixl::Status HixlProfCtrlHandle(uint32_t data_type, void *data, uint32_t data_len) {
    HIXL_CHECK_NOTNULL(data, "data is null, please check");
    if (data_type == PROF_CTRL_SWITCH) {
      const hixl::Status ret = ProcessProfData(data, data_len);
      HIXL_CHK_STATUS_RET(ret, "[Process][ProfSwitch]failed to call ProcessProfData, result is %u", ret);
      return hixl::SUCCESS;
    }
    HIXL_LOGI("get unsupported data_type %u while processing profiling data", data_type);
    return hixl::SUCCESS;
  }

  class HixlRegProfCallback {
  public:
    HixlRegProfCallback() {
      const auto prof_ret = MsprofRegisterCallback(ASCENDCL, &HixlProfCtrlHandle);
      if (prof_ret != 0) {
        HIXL_LOGE("can not register Callback, prof result = %d", prof_ret);
      }
    }
    ~HixlProfCallback() {}
  };
  static HixlRegProfCallback prof_cb_reg;
}

namespace hixl {
  HixlProfilingReporter::HixlProfilingReporter(const HixlProfType api_id) : hixl_api_(api_id) {
    if (prof_run) {
      start_time_ = MsprofSysCycleTime();
    }
  }

  HixlProfilingReporter::HixlProfilingReporter(const HixlProfType api_id, uint64_t start_time) 
      : hixl_api_(api_id), start_time_(start_time) {}

  HixlProfilingReporter::~HixlProfilingReporter() noexcept {
    if (prof_run && (start_time_ != 0Ul)) {
      const uint64_t end_time = MsprofSysCycleTime();
      MsprofApi api{};
      api.beginTime = start_time_;
      api.endTime = end_time;
      api.threadId = static_cast<uint32_t>(mmGetTid());
      api.level = MSPROF_REPORT_ACL_LEVEL;
      api.type = static_cast<uint32_t>(hixl_api_);
      (void)MsprofReportApi(true, &api);
    }
  }
}