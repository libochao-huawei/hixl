/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <memory>
#include <vector>
#include <cstdlib>
#include <thread>
#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "engine/hixl_client.h"
#include "common/hixl_inner_types.h"
#include "common/hixl_utils.h"
#include "cs/hixl_cs_client.h"
#include "common/hixl_cs.h"
#include "depends/runtime/src/runtime_stub.h"
#include "common/hixl_utils.h"
#include "common/ctrl_msg_plugin.h"
#include "common/segment.h"

using namespace ::testing;

namespace hixl {
static constexpr uint32_t kServerPort = 16001;
static constexpr uint32_t kDefaultTimeoutMs = 5000;
static constexpr uint32_t kShortTimeoutMs = 10;
static constexpr uint32_t kMilliSeconds10 = 10;
static constexpr uint32_t kSleepMs = 10;
static constexpr uint32_t kSleepLongTimeMs = 30000;
static constexpr uint32_t kMemNum = 100U;
static constexpr uint32_t kNUm1 = 1;
static constexpr uint32_t kNUm2 = 2;
static constexpr uint32_t default_list_num = 2;
static constexpr uint32_t list_num_4ub = 4;
static std::vector<uint32_t> kLocalMems(kMemNum, kNUm1);
static std::vector<uint32_t> kRemoteMems(kMemNum, kNUm2);
enum class MockHixlServerMode : uint32_t {
  k4UbNormal = 0,
  k2UbNormal,
  // GetEndPointInfoResp 相关异常
  kGetEndPointInfoResp_BadMagic,
  kGetEndPointInfoResp_BadMsgType,
  kGetEndPointInfoResp_BadBodySizeTooSmall,  // body_size <= sizeof(CtrlMsgType)
  kGetEndPointInfoResp_BadJson,
  kGetEndPointInfoResp_JsonIsNotArray,
  kGetEndPointInfoResp_MissingField,
};
// server 内存信息
static HcclMem default_remote_mem_list[] = {{HCCL_MEM_TYPE_HOST, &kRemoteMems[0], sizeof(uint32_t)},
                                            {HCCL_MEM_TYPE_DEVICE, &kRemoteMems[2], sizeof(uint32_t)}};
static HcclMem remote_mem_list_4ub[] = {{HCCL_MEM_TYPE_HOST, &kRemoteMems[0], sizeof(uint32_t)},
                                        {HCCL_MEM_TYPE_DEVICE, &kRemoteMems[2], sizeof(uint32_t)},
                                        {HCCL_MEM_TYPE_HOST, &kRemoteMems[4], sizeof(uint32_t)},
                                        {HCCL_MEM_TYPE_DEVICE, &kRemoteMems[6], sizeof(uint32_t)}};
// client 内存信息
static HcclMem default_local_mem_list[] = {{HCCL_MEM_TYPE_HOST, &kLocalMems[0], sizeof(uint32_t)},
                                           {HCCL_MEM_TYPE_DEVICE, &kLocalMems[2], sizeof(uint32_t)}};
static HcclMem local_mem_list_4ub[] = {{HCCL_MEM_TYPE_DEVICE, &kLocalMems[0], sizeof(uint32_t)},
                                       {HCCL_MEM_TYPE_DEVICE, &kLocalMems[2], sizeof(uint32_t)},
                                       {HCCL_MEM_TYPE_HOST, &kLocalMems[4], sizeof(uint32_t)},
                                       {HCCL_MEM_TYPE_HOST, &kLocalMems[6], sizeof(uint32_t)}};

class MockHixlServer {
 public:
  MockHixlServer() : mode_(MockHixlServerMode::k4UbNormal) {}
  ~MockHixlServer() {
    DestroyServerAndUnreg();
  }
  void SetMode(MockHixlServerMode m) {
    mode_ = m;
  }

  Status CreateServer(const std::vector<EndPointConfig> &remote_endpoint_list) {
    HixlServerConfig config{};
    std::vector<EndPointInfo> endpointInfoList;
    for (const auto &ep : remote_endpoint_list) {
      EndPointInfo endpointInfo;
      ConvertToEndPointInfo(ep, endpointInfo);
      endpointInfoList.push_back(endpointInfo);
    }
    HixlStatus ret = HixlCSServerCreate("127.0.0.1", kServerPort, &endpointInfoList[0], endpointInfoList.size(),
                                        &config, &server_handle_);
    if (ret != HIXL_SUCCESS) {
      std::cerr << "Failed to create CsServer" << std::endl;
      return FAILED;
    }
    std::cout << "success to create CsServer" << std::endl;
    return SUCCESS;
  }

  void RegMem(HcclMem *mem_list, size_t size) {
    for (size_t i = 0; i < size; ++i) {
      MemHandle mem_handle = nullptr;
      HixlStatus ret = HixlCSServerRegMem(server_handle_, std::to_string(i).c_str(), &mem_list[i], &mem_handle);
      if (ret != HIXL_SUCCESS) {
        std::cerr << "CsServer failed to RegMem" << std::endl;
        return;
      }
      mem_handles_.emplace_back(mem_handle);
    }
  }

  void ListenServer() {
    MsgProcessor send_endpoint_cb = [this](int32_t fd, const char *msg, uint64_t msg_len) -> Status {
      (void)msg;
      (void)msg_len;
      conn_fd_ = fd;
      SendResponse();
      return SUCCESS;
    };
    HixlStatus ret = HixlCSServerRegProc(server_handle_, CtrlMsgType::kGetEndPointInfoReq, send_endpoint_cb);
    if (ret != HIXL_SUCCESS) {
      std::cerr << "Failed to reg proc CsServer" << std::endl;
      return;
    }

    ret = HixlCSServerListen(server_handle_, kServerPort);
    if (ret != HIXL_SUCCESS) {
      std::cerr << "Failed to listen CsServer" << std::endl;
      return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(kSleepMs));
  }

  void DestroyServerAndUnreg() {
    if (server_handle_ == nullptr) {
      return;
    }

    for (auto mem_handle : mem_handles_) {
      HixlStatus ret = HixlCSServerUnregMem(server_handle_, mem_handle);
      if (ret != HIXL_SUCCESS) {
        std::cerr << "CsServer failed to UnregMem" << std::endl;
        return;
      }
    }
    // 清空内存句柄列表
    mem_handles_.clear();

    // 销毁服务器
    HixlStatus ret = HixlCSServerDestroy(server_handle_);
    server_handle_ = nullptr;
    if (ret != HIXL_SUCCESS) {
      std::cerr << "CsServer failed to Destroy" << std::endl;
      return;
    }
  }

 private:
  HixlServerHandle server_handle_ = nullptr;
  std::vector<MemHandle> mem_handles_{};
  int32_t conn_fd_ = -1;
  MockHixlServerMode mode_;

  // 通用响应发送方法
  void SendResponseImpl(uint32_t magic, CtrlMsgType msg_type, size_t body_size, const std::string &json_content) {
    // 发送头部
    CtrlMsgHeader header{};
    header.magic = magic;
    header.body_size = body_size;
    CtrlMsgPlugin::Send(conn_fd_, &header, sizeof(header));

    // 发送消息体
    CtrlMsgPlugin::Send(conn_fd_, &msg_type, sizeof(msg_type));
    if (!json_content.empty()) {
      CtrlMsgPlugin::Send(conn_fd_, json_content.data(), json_content.size());
    }
  }

  void SendResponse() {
    const std::string kErrorJson = R"({ invalid json )";
    const std::string k2UbJson = R"([
        {
          "protocol": "roce",
          "comm_id": "127.0.0.1",
          "dst_eid": "",
          "plane": "",
          "placement": "device",
          "net_instance_id": "superpod1-1"
        },
        {
          "protocol": "ub_ctp",
          "comm_id": "0000:0000:0000:0000:0000:0000:c0a8:0463",
          "dst_eid" : "0000:0000:0000:0000:0000:0000:c0a8:0563",
          "plane": "",
          "placement" : "host",
          "net_instance_id" : "superpod1-1"
        },
        {
          "protocol": "ub_ctp",
          "comm_id": "0000:0000:0000:0000:0000:0000:c0a8:0663",
          "dst_eid" : "0000:0000:0000:0000:0000:0000:c0a8:0763",
          "plane": "",
          "placement" : "device",
          "net_instance_id" : "superpod1-1"
        }
      ])";
    const std::string k4UbJson = R"([
        {
          "protocol": "roce",
          "comm_id": "127.0.0.1",
          "dst_eid": "",
          "plane": "",
          "placement": "device",
          "net_instance_id": "superpod1-1"
        },
        {
          "protocol": "ub_ctp",
          "comm_id": "0000:0000:0000:0000:0000:0000:c0a8:0463",
          "dst_eid" : "0000:0000:0000:0000:0000:0000:c0a8:0563",
          "plane": "",
          "placement" : "host",
          "net_instance_id" : "superpod1-1"
        },
        {
          "protocol": "ub_ctp",
          "comm_id": "0000:0000:0000:0000:0000:0000:c0a8:0663",
          "dst_eid" : "0000:0000:0000:0000:0000:0000:c0a8:0763",
          "plane": "",
          "placement" : "device",
          "net_instance_id" : "superpod1-1"
        },
        {
          "protocol": "ub_ctp",
          "comm_id": "0000:0000:0000:0000:0000:0000:c0a8:0063",
          "dst_eid": "",
          "plane" : "plane-a",
          "placement" : "device",
          "net_instance_id" : "superpod1-1"
        },
        {
          "protocol": "ub_tp",
          "comm_id": "0000:0000:0000:0000:0000:0000:c0a8:0163",
          "dst_eid": "",
          "plane" : "plane-b",
          "placement" : "host",
          "net_instance_id" : "superpod1-1"
        }
      ])";
    const std::string kNotArrayJson = R"(
        {
          "protocol": "roce",
          "comm_id": "127.0.0.1",
          "dst_eid": "",
          "plane": "",
          "placement": "device",
          "net_instance_id": "superpod1-1"
        }
      )";
    const std::string kMissingFieldJson = R"([
        {
          "comm_id": "127.0.0.1",
          "dst_eid": "",
          "plane": "",
          "placement": "device",
          "net_instance_id": "superpod1-1"
        }
      ])";

    switch (mode_) {
      case MockHixlServerMode::k4UbNormal:
        SendResponseImpl(kMagicNumber, CtrlMsgType::kGetEndPointInfoResp, sizeof(CtrlMsgType) + k4UbJson.size(),
                         k4UbJson);
        break;
      case MockHixlServerMode::k2UbNormal:
        SendResponseImpl(kMagicNumber, CtrlMsgType::kGetEndPointInfoResp, sizeof(CtrlMsgType) + k2UbJson.size(),
                         k2UbJson);
        break;
      case MockHixlServerMode::kGetEndPointInfoResp_BadMagic:
        SendResponseImpl(0xDEADBEEF, CtrlMsgType::kGetEndPointInfoResp, sizeof(CtrlMsgType) + k4UbJson.size(),
                         k4UbJson);
        break;
      case MockHixlServerMode::kGetEndPointInfoResp_BadMsgType:
        SendResponseImpl(kMagicNumber, CtrlMsgType::kCreateChannelReq, sizeof(CtrlMsgType) + k4UbJson.size(), k4UbJson);
        break;
      case MockHixlServerMode::kGetEndPointInfoResp_BadBodySizeTooSmall:
        SendResponseImpl(kMagicNumber, CtrlMsgType::kGetEndPointInfoResp, sizeof(CtrlMsgType) - 1, "");
        break;
      case MockHixlServerMode::kGetEndPointInfoResp_BadJson:
        SendResponseImpl(kMagicNumber, CtrlMsgType::kGetEndPointInfoResp, sizeof(CtrlMsgType) + kErrorJson.size(),
                         kErrorJson);
        break;
      case MockHixlServerMode::kGetEndPointInfoResp_JsonIsNotArray:
        SendResponseImpl(kMagicNumber, CtrlMsgType::kGetEndPointInfoResp, sizeof(CtrlMsgType) + kNotArrayJson.size(),
                         kNotArrayJson);
        break;
      case MockHixlServerMode::kGetEndPointInfoResp_MissingField:
        SendResponseImpl(kMagicNumber, CtrlMsgType::kGetEndPointInfoResp,
                         sizeof(CtrlMsgType) + kMissingFieldJson.size(), kMissingFieldJson);
        break;
      default:
        SendResponseImpl(kMagicNumber, CtrlMsgType::kGetEndPointInfoResp, sizeof(CtrlMsgType) + k4UbJson.size(),
                         k4UbJson);
        break;
    }
  }
};

class HixlClientUTest : public ::testing::Test {
 protected:
  void SetUp() override {
    server_ = MakeUnique<MockHixlServer>();
    client_ = MakeUnique<HixlClient>("127.0.0.1", kServerPort);
  }

  void TearDown() override {
    client_->Finalize();
    server_->DestroyServerAndUnreg();
  }

  void StartServer(MockHixlServerMode mode) {
    server_->SetMode(mode);
    auto st = server_->CreateServer(Make4UbRemoteEpList());
    ASSERT_EQ(st, SUCCESS) << "Failed to start mock server";
    server_->RegMem(default_remote_mem_list, default_list_num);
    server_->ListenServer();
  }

  void StartServerReg4Ub(MockHixlServerMode mode) {
    server_->SetMode(mode);
    auto st = server_->CreateServer(Make4UbRemoteEpList());
    ASSERT_EQ(st, SUCCESS) << "Failed to start mock server";
    server_->RegMem(remote_mem_list_4ub, list_num_4ub);
    server_->ListenServer();
  }

  std::unique_ptr<HixlClient> client_;
  std::unique_ptr<MockHixlServer> server_;

  EndPointConfig MakeRoceHostLocalEp() {
    EndPointConfig ep{};
    ep.protocol = "roce";
    ep.comm_id = "127.0.0.1";
    ep.placement = "host";
    ep.net_instance_id = "superpod1-1";
    return ep;
  }

  EndPointConfig MakeRoceHostRemoteEp() {
    EndPointConfig ep{};
    ep.protocol = "roce";
    ep.comm_id = "127.0.0.1";
    ep.placement = "device";
    ep.net_instance_id = "superpod1-1";
    return ep;
  }

  EndPointConfig MakeUbHostLocalEp1() {
    EndPointConfig ep{};
    ep.protocol = "ub_ctp";
    ep.comm_id = "0000:0000:0000:0000:0000:0000:c0a8:0563";
    ep.dst_eid = "0000:0000:0000:0000:0000:0000:c0a8:0463";
    ep.placement = "host";
    ep.net_instance_id = "superpod1-1";
    return ep;
  }

  EndPointConfig MakeUbHostRemoteEp1() {
    EndPointConfig ep{};
    ep.protocol = "ub_ctp";
    ep.comm_id = "0000:0000:0000:0000:0000:0000:c0a8:0463";
    ep.dst_eid = "0000:0000:0000:0000:0000:0000:c0a8:0563";
    ep.placement = "host";
    ep.net_instance_id = "superpod1-1";
    return ep;
  }

  EndPointConfig MakeUbHostLocalEp2() {
    EndPointConfig ep{};
    ep.protocol = "ub_ctp";
    ep.comm_id = "0000:0000:0000:0000:0000:0000:c0a8:0763";
    ep.dst_eid = "0000:0000:0000:0000:0000:0000:c0a8:0663";
    ep.placement = "host";
    ep.net_instance_id = "superpod1-1";
    return ep;
  }

  EndPointConfig MakeUbDeviceRemoteEp2() {
    EndPointConfig ep{};
    ep.protocol = "ub_ctp";
    ep.comm_id = "0000:0000:0000:0000:0000:0000:c0a8:0663";
    ep.dst_eid = "0000:0000:0000:0000:0000:0000:c0a8:0763";
    ep.placement = "device";
    ep.net_instance_id = "superpod1-1";
    return ep;
  }

  EndPointConfig MakeUbDeviceLocalEp3() {
    EndPointConfig ep{};
    ep.protocol = "ub_ctp";
    ep.comm_id = "0000:0000:0000:0000:0000:0000:c0a8:0363";
    ep.plane = "plane-a";
    ep.placement = "device";
    ep.net_instance_id = "superpod1-1";
    return ep;
  }

  EndPointConfig MakeUbDeviceRemoteEp3() {
    EndPointConfig ep{};
    ep.protocol = "ub_ctp";
    ep.comm_id = "0000:0000:0000:0000:0000:0000:c0a8:0063";
    ep.plane = "plane-a";
    ep.placement = "device";
    ep.net_instance_id = "superpod1-1";
    return ep;
  }

  EndPointConfig MakeUbDeviceLocalEp4() {
    EndPointConfig ep{};
    ep.protocol = "ub_tp";
    ep.comm_id = "0000:0000:0000:0000:0000:0000:c0a8:0263";
    ep.plane = "plane-b";
    ep.placement = "device";
    ep.net_instance_id = "superpod1-1";
    return ep;
  }

  EndPointConfig MakeUbHostRemoteEp4() {
    EndPointConfig ep{};
    ep.protocol = "ub_tp";
    ep.comm_id = "0000:0000:0000:0000:0000:0000:c0a8:0163";
    ep.plane = "plane-b";
    ep.placement = "host";
    ep.net_instance_id = "superpod1-1";
    return ep;
  }

  EndPointConfig MakeUbDiffNetLocalEp1() {
    EndPointConfig ep{};
    ep.protocol = "ub_ctp";
    ep.comm_id = "0000:0000:0000:0000:0000:0000:c0a8:0563";
    ep.dst_eid = "0000:0000:0000:0000:0000:0000:c0a8:0463";
    ep.placement = "host";
    ep.net_instance_id = "superpod2-2";
    return ep;
  }

  EndPointConfig MakeUbDiffNetLocalEp2() {
    EndPointConfig ep{};
    ep.protocol = "ub_ctp";
    ep.comm_id = "0000:0000:0000:0000:0000:0000:c0a8:0763";
    ep.dst_eid = "0000:0000:0000:0000:0000:0000:c0a8:0663";
    ep.placement = "host";
    ep.net_instance_id = "superpod2-2";
    return ep;
  }

  EndPointConfig MakeRoceDiffNetLocalEp() {
    EndPointConfig ep{};
    ep.protocol = "roce";
    ep.comm_id = "127.0.0.1";
    ep.placement = "host";
    ep.net_instance_id = "superpod2-2";
    return ep;
  }

  std::vector<EndPointConfig> Make4UbRemoteEpList() {
    std::vector<EndPointConfig> ep_list;
    ep_list.push_back(MakeRoceHostRemoteEp());
    ep_list.push_back(MakeUbHostRemoteEp1());
    ep_list.push_back(MakeUbDeviceRemoteEp2());
    ep_list.push_back(MakeUbDeviceRemoteEp3());
    ep_list.push_back(MakeUbHostRemoteEp4());
    return ep_list;
  }

  std::vector<MemInfo> MakeMemInfoList() {
    std::vector<MemInfo> mem_info_list;
    // 添加DEVICE类型内存
    MemInfo device_mem;
    device_mem.mem_handle = nullptr;
    device_mem.mem.addr = reinterpret_cast<uintptr_t>(&kLocalMems[0]);
    device_mem.mem.len = sizeof(uint32_t);
    device_mem.type = MEM_DEVICE;
    mem_info_list.push_back(device_mem);

    // 添加HOST类型内存
    MemInfo host_mem;
    host_mem.mem_handle = nullptr;
    host_mem.mem.addr = reinterpret_cast<uintptr_t>(&kLocalMems[2]);
    host_mem.mem.len = sizeof(uint32_t);
    host_mem.type = MEM_HOST;
    mem_info_list.push_back(host_mem);
    return mem_info_list;
  }

  std::vector<MemInfo> Make4UbMemInfoList() {
    std::vector<MemInfo> mem_info_list;
    MemInfo mem1;
    mem1.mem_handle = nullptr;
    mem1.mem.addr = reinterpret_cast<uintptr_t>(&kLocalMems[0]);
    mem1.mem.len = sizeof(uint32_t);
    mem1.type = MEM_DEVICE;
    mem_info_list.push_back(mem1);

    MemInfo mem2;
    mem2.mem_handle = nullptr;
    mem2.mem.addr = reinterpret_cast<uintptr_t>(&kLocalMems[2]);
    mem2.mem.len = sizeof(uint32_t);
    mem2.type = MEM_DEVICE;
    mem_info_list.push_back(mem2);

    MemInfo mem3;
    mem3.mem_handle = nullptr;
    mem3.mem.addr = reinterpret_cast<uintptr_t>(&kLocalMems[4]);
    mem3.mem.len = sizeof(uint32_t);
    mem3.type = MEM_HOST;
    mem_info_list.push_back(mem3);

    MemInfo mem4;
    mem4.mem_handle = nullptr;
    mem4.mem.addr = reinterpret_cast<uintptr_t>(&kLocalMems[6]);
    mem4.mem.len = sizeof(uint32_t);
    mem4.type = MEM_HOST;
    mem_info_list.push_back(mem4);
    return mem_info_list;
  }

  void InitializeBadJson(MockHixlServerMode bad_json_mode) {
    StartServer(bad_json_mode);
    std::vector<EndPointConfig> local_endpoint_list;
    local_endpoint_list.push_back(MakeRoceDiffNetLocalEp());
    Status st = client_->Initialize(local_endpoint_list);
    EXPECT_EQ(st, PARAM_INVALID);
    st = client_->Finalize();
    EXPECT_EQ(st, SUCCESS);
    server_->DestroyServerAndUnreg();
  }
};

// Initialize 接口测试：正常场景 创建 ub 链路4条
TEST_F(HixlClientUTest, Initialize4UBTest) {
  // 启动模拟服务端
  StartServer(MockHixlServerMode::k4UbNormal);
  std::vector<EndPointConfig> local_endpoint_list;
  local_endpoint_list.push_back(MakeRoceHostLocalEp());
  local_endpoint_list.push_back(MakeUbHostLocalEp1());
  local_endpoint_list.push_back(MakeUbHostLocalEp2());
  local_endpoint_list.push_back(MakeUbDeviceLocalEp3());
  local_endpoint_list.push_back(MakeUbDeviceLocalEp4());
  Status st = client_->Initialize(local_endpoint_list);
  EXPECT_EQ(st, SUCCESS);
}

// Initialize 接口测试：正常场景 创建 ub 链路2条
TEST_F(HixlClientUTest, Initialize2UBTest) {
  // 启动模拟服务端
  StartServer(MockHixlServerMode::k4UbNormal);
  std::vector<EndPointConfig> local_endpoint_list;
  local_endpoint_list.push_back(MakeRoceHostLocalEp());
  local_endpoint_list.push_back(MakeUbHostLocalEp1());
  local_endpoint_list.push_back(MakeUbHostLocalEp2());
  Status st = client_->Initialize(local_endpoint_list);
  EXPECT_EQ(st, SUCCESS);
}

// Initialize 接口测试：正常场景 创建 ub 链路1条
TEST_F(HixlClientUTest, Initialize1UBTest) {
  // 启动模拟服务端
  StartServer(MockHixlServerMode::k2UbNormal);
  std::vector<EndPointConfig> local_endpoint_list;
  local_endpoint_list.push_back(MakeRoceHostLocalEp());
  local_endpoint_list.push_back(MakeUbHostLocalEp1());
  local_endpoint_list.push_back(MakeUbDeviceLocalEp3());
  Status st = client_->Initialize(local_endpoint_list);
  EXPECT_EQ(st, SUCCESS);
}

// Initialize 接口测试：正常场景 环境变量设为1，创建ROCE链路
TEST_F(HixlClientUTest, InitializeEnvTest) {
  StartServer(MockHixlServerMode::k2UbNormal);
  std::vector<EndPointConfig> local_endpoint_list;
  local_endpoint_list.push_back(MakeRoceHostLocalEp());
  local_endpoint_list.push_back(MakeUbHostLocalEp1());
  local_endpoint_list.push_back(MakeUbHostLocalEp2());
  {
    llm::EnvGuard env_guard("HCCL_INTRA_ROCE_ENABLE", "1");
    Status st = client_->Initialize(local_endpoint_list);
    EXPECT_EQ(st, SUCCESS);
    st = client_->Finalize();
    EXPECT_EQ(st, SUCCESS);
    server_->DestroyServerAndUnreg();
  }
}

// Initialize 接口测试：正常场景 两端不在同一节点，创建ROCE链路
TEST_F(HixlClientUTest, InitializeDiffNetTest) {
  StartServer(MockHixlServerMode::k2UbNormal);
  std::vector<EndPointConfig> local_endpoint_list;
  local_endpoint_list.push_back(MakeRoceDiffNetLocalEp());
  local_endpoint_list.push_back(MakeUbDiffNetLocalEp1());
  local_endpoint_list.push_back(MakeUbDiffNetLocalEp2());
  Status st = client_->Initialize(local_endpoint_list);
  EXPECT_EQ(st, SUCCESS);
  st = client_->Finalize();
  EXPECT_EQ(st, SUCCESS);
  server_->DestroyServerAndUnreg();
}

// Initialize 接口测试：异常场景 没有roce但必需roce(两端不在同一节点)
TEST_F(HixlClientUTest, InitializeNoRoceTest) {
  StartServer(MockHixlServerMode::k2UbNormal);
  std::vector<EndPointConfig> local_endpoint_list;
  local_endpoint_list.push_back(MakeUbDiffNetLocalEp1());
  local_endpoint_list.push_back(MakeUbDiffNetLocalEp2());
  Status st = client_->Initialize(local_endpoint_list);
  EXPECT_EQ(st, FAILED);
  st = client_->Finalize();
  EXPECT_EQ(st, SUCCESS);
  server_->DestroyServerAndUnreg();
}

// Initialize 接口测试：异常场景 没有可配对的 endpoint
TEST_F(HixlClientUTest, InitializeNoPairTest) {
  StartServer(MockHixlServerMode::k2UbNormal);
  std::vector<EndPointConfig> local_endpoint_list;
  local_endpoint_list.push_back(MakeRoceHostLocalEp());
  local_endpoint_list.push_back(MakeUbDeviceLocalEp3());
  local_endpoint_list.push_back(MakeUbDeviceLocalEp4());
  Status st = client_->Initialize(local_endpoint_list);
  EXPECT_EQ(st, FAILED);
  st = client_->Finalize();
  EXPECT_EQ(st, SUCCESS);
  server_->DestroyServerAndUnreg();
}

// Initialize 接口测试：异常场景 错误的magic响应
TEST_F(HixlClientUTest, InitializeBadMagicTest) {
  InitializeBadJson(MockHixlServerMode::kGetEndPointInfoResp_BadMagic);
}

// Initialize 接口测试：异常场景 错误的msg_type响应
TEST_F(HixlClientUTest, InitializeBadMsgTypeTest) {
  InitializeBadJson(MockHixlServerMode::kGetEndPointInfoResp_BadMsgType);
}

// Initialize 接口测试：异常场景 错误的body_size响应
TEST_F(HixlClientUTest, InitializeBadBodySizeTest) {
  InitializeBadJson(MockHixlServerMode::kGetEndPointInfoResp_BadBodySizeTooSmall);
}

// Initialize 接口测试：异常场景 错误的json响应
TEST_F(HixlClientUTest, InitializeBadJsonTest) {
  InitializeBadJson(MockHixlServerMode::kGetEndPointInfoResp_BadJson);
}

// Initialize 接口测试：异常场景 json 不是数组
TEST_F(HixlClientUTest, InitializeJsonIsNotArrayTest) {
  InitializeBadJson(MockHixlServerMode::kGetEndPointInfoResp_JsonIsNotArray);
}

// Initialize 接口测试：异常场景 json 缺少字段
TEST_F(HixlClientUTest, InitializeJsonMissingFieldTest) {
  InitializeBadJson(MockHixlServerMode::kGetEndPointInfoResp_MissingField);
}

// SetLocalMemInfo 接口测试：正常场景
TEST_F(HixlClientUTest, SetLocalMemInfoTest) {
  StartServer(MockHixlServerMode::k4UbNormal);
  // 初始化 roce 链路
  std::vector<EndPointConfig> local_endpoint_list;
  local_endpoint_list.push_back(MakeRoceDiffNetLocalEp());
  Status st = client_->Initialize(local_endpoint_list);
  EXPECT_EQ(st, SUCCESS);
  std::vector<MemInfo> mem_info_list = MakeMemInfoList();
  st = client_->SetLocalMemInfo(mem_info_list);
  EXPECT_EQ(st, SUCCESS);
  st = client_->Finalize();
  EXPECT_EQ(st, SUCCESS);
  server_->DestroyServerAndUnreg();
}

// Connect 接口测试：正常场景 - 成功连接并获取远程内存信息
TEST_F(HixlClientUTest, ConnectSuccessTest) {
  StartServer(MockHixlServerMode::k4UbNormal);
  // 初始化 roce 链路
  std::vector<EndPointConfig> local_endpoint_list;
  local_endpoint_list.push_back(MakeRoceDiffNetLocalEp());
  Status st = client_->Initialize(local_endpoint_list);
  EXPECT_EQ(st, SUCCESS);
  // 调用 Connect 方法
  st = client_->Connect(kDefaultTimeoutMs);
  EXPECT_EQ(st, SUCCESS);
  st = client_->Finalize();
  EXPECT_EQ(st, SUCCESS);
  server_->DestroyServerAndUnreg();
}

// Connect 接口测试：异常场景 - 未初始化
TEST_F(HixlClientUTest, ConnectNotInitializedTest) {
  StartServer(MockHixlServerMode::k4UbNormal);
  // 未初始化客户端
  Status st = client_->Connect(kDefaultTimeoutMs);
  EXPECT_EQ(st, FAILED);
  st = client_->Finalize();
  EXPECT_EQ(st, SUCCESS);
  server_->DestroyServerAndUnreg();
}

// TransferSync 接口测试：正常场景 - roce
TEST_F(HixlClientUTest, TransferSyncSuccessTest) {
  StartServer(MockHixlServerMode::k4UbNormal);
  // 初始化 roce 链路
  std::vector<EndPointConfig> local_endpoint_list;
  local_endpoint_list.push_back(MakeRoceDiffNetLocalEp());
  Status st = client_->Initialize(local_endpoint_list);
  EXPECT_EQ(st, SUCCESS);
  // 获取内存信息
  st = client_->SetLocalMemInfo(MakeMemInfoList());
  EXPECT_EQ(st, SUCCESS);
  // 调用 Connect 方法
  st = client_->Connect(kDefaultTimeoutMs);
  EXPECT_EQ(st, SUCCESS);
  // 调用 TransferSync 方法
  std::vector<TransferOpDesc> op_descs;
  TransferOpDesc op_desc;
  op_desc.local_addr = reinterpret_cast<uintptr_t>(&kLocalMems[0]);
  op_desc.remote_addr = reinterpret_cast<uintptr_t>(&kRemoteMems[0]);
  op_desc.len = sizeof(uint32_t);
  op_descs.push_back(op_desc);
  st = client_->TransferSync(op_descs, WRITE, kDefaultTimeoutMs);
  EXPECT_EQ(st, SUCCESS);
  std::cout << kRemoteMems[0] << std::endl;
  st = client_->TransferSync(op_descs, READ, kDefaultTimeoutMs);
  EXPECT_EQ(st, SUCCESS);
  std::cout << kLocalMems[0] << std::endl;
}

// TransferSync 接口测试：正常场景 - 4ub传输
TEST_F(HixlClientUTest, TransferSync4UbSuccessTest) {
  StartServerReg4Ub(MockHixlServerMode::k4UbNormal);
  // 初始化4条ub链路
  std::vector<EndPointConfig> local_endpoint_list;
  local_endpoint_list.push_back(MakeRoceHostLocalEp());
  local_endpoint_list.push_back(MakeUbHostLocalEp1());
  local_endpoint_list.push_back(MakeUbHostLocalEp2());
  local_endpoint_list.push_back(MakeUbDeviceLocalEp3());
  local_endpoint_list.push_back(MakeUbDeviceLocalEp4());
  Status st = client_->Initialize(local_endpoint_list);
  EXPECT_EQ(st, SUCCESS);
  // 设置内存信息
  st = client_->SetLocalMemInfo(Make4UbMemInfoList());
  EXPECT_EQ(st, SUCCESS);
  // 连接服务器
  st = client_->Connect(kDefaultTimeoutMs);
  EXPECT_EQ(st, SUCCESS);
  // 准备传输操作
  std::vector<TransferOpDesc> op_descs;
  for (uint32_t i = 0; i < list_num_4ub; i++) {
    TransferOpDesc op_desc;
    op_desc.local_addr = reinterpret_cast<uintptr_t>(&kLocalMems[i * 2]);
    op_desc.remote_addr = reinterpret_cast<uintptr_t>(&kRemoteMems[i * 2]);
    op_desc.len = sizeof(uint32_t);
    op_descs.push_back(op_desc);
  }
  // 执行同步传输
  st = client_->TransferSync(op_descs, WRITE, kDefaultTimeoutMs);
  EXPECT_EQ(st, SUCCESS);
}

// TransferSync 接口测试：异常场景 - 未建链
TEST_F(HixlClientUTest, TransferSyncNoConnectTest) {
  StartServer(MockHixlServerMode::k4UbNormal);
  // 初始化 roce 链路
  std::vector<EndPointConfig> local_endpoint_list;
  local_endpoint_list.push_back(MakeRoceDiffNetLocalEp());
  Status st = client_->Initialize(local_endpoint_list);
  EXPECT_EQ(st, SUCCESS);
  // 获取内存信息
  st = client_->SetLocalMemInfo(MakeMemInfoList());
  EXPECT_EQ(st, SUCCESS);
  // 调用 TransferSync 方法
  std::vector<TransferOpDesc> op_descs;
  TransferOpDesc op_desc;
  op_desc.local_addr = reinterpret_cast<uintptr_t>(&kLocalMems[0]);
  op_desc.remote_addr = reinterpret_cast<uintptr_t>(&kRemoteMems[0]);
  op_desc.len = sizeof(uint32_t);
  op_descs.push_back(op_desc);
  st = client_->TransferSync(op_descs, WRITE, kDefaultTimeoutMs);
  EXPECT_EQ(st, NOT_CONNECTED);
}

// TransferSync 接口测试：异常场景 - 未SetLocalMemInfo
TEST_F(HixlClientUTest, TransferSyncNoSetLocalMemInfoTest) {
  StartServer(MockHixlServerMode::k4UbNormal);
  // 初始化 roce 链路
  std::vector<EndPointConfig> local_endpoint_list;
  local_endpoint_list.push_back(MakeRoceDiffNetLocalEp());
  Status st = client_->Initialize(local_endpoint_list);
  EXPECT_EQ(st, SUCCESS);
  // 调用 Connect 方法
  st = client_->Connect(kDefaultTimeoutMs);
  EXPECT_EQ(st, SUCCESS);
  // 调用 TransferSync 方法
  std::vector<TransferOpDesc> op_descs;
  TransferOpDesc op_desc;
  op_desc.local_addr = reinterpret_cast<uintptr_t>(&kLocalMems[0]);
  op_desc.remote_addr = reinterpret_cast<uintptr_t>(&kRemoteMems[0]);
  op_desc.len = sizeof(uint32_t);
  op_descs.push_back(op_desc);
  st = client_->TransferSync(op_descs, WRITE, kDefaultTimeoutMs);
  EXPECT_EQ(st, PARAM_INVALID);
}

// TransferSync 接口测试：异常场景 - 空的op_descs列表
TEST_F(HixlClientUTest, TransferSyncEmptyOpDescsTest) {
  StartServer(MockHixlServerMode::k4UbNormal);
  // 初始化 roce 链路
  std::vector<EndPointConfig> local_endpoint_list;
  local_endpoint_list.push_back(MakeRoceDiffNetLocalEp());
  Status st = client_->Initialize(local_endpoint_list);
  EXPECT_EQ(st, SUCCESS);
  // 设置内存信息
  st = client_->SetLocalMemInfo(MakeMemInfoList());
  EXPECT_EQ(st, SUCCESS);
  // 连接服务器
  st = client_->Connect(kDefaultTimeoutMs);
  EXPECT_EQ(st, SUCCESS);
  // 空的操作列表
  std::vector<TransferOpDesc> op_descs;
  // 执行同步传输
  st = client_->TransferSync(op_descs, WRITE, kDefaultTimeoutMs);
  EXPECT_EQ(st, PARAM_INVALID);
}

// TransferSync 接口测试：异常场景 - 内存未注册
TEST_F(HixlClientUTest, TransferSyncMemMismatchTest) {
  StartServer(MockHixlServerMode::k4UbNormal);
  // 初始化 roce 链路
  std::vector<EndPointConfig> local_endpoint_list;
  local_endpoint_list.push_back(MakeRoceDiffNetLocalEp());
  Status st = client_->Initialize(local_endpoint_list);
  EXPECT_EQ(st, SUCCESS);
  // 获取内存信息
  st = client_->SetLocalMemInfo(MakeMemInfoList());
  EXPECT_EQ(st, SUCCESS);
  // 调用 Connect 方法
  st = client_->Connect(kDefaultTimeoutMs);
  EXPECT_EQ(st, SUCCESS);
  // 调用 TransferSync 方法
  std::vector<TransferOpDesc> op_descs1;
  TransferOpDesc op_desc;
  uint32_t tmp = 10;
  op_desc.local_addr = reinterpret_cast<uintptr_t>(&tmp);
  op_desc.remote_addr = reinterpret_cast<uintptr_t>(&kRemoteMems[0]);
  op_desc.len = sizeof(uint32_t);
  op_descs1.push_back(op_desc);
  st = client_->TransferSync(op_descs1, WRITE, kDefaultTimeoutMs);
  EXPECT_EQ(st, PARAM_INVALID);

  std::vector<TransferOpDesc> op_descs2;
  op_desc.local_addr = reinterpret_cast<uintptr_t>(&kLocalMems[0]);
  op_desc.remote_addr = reinterpret_cast<uintptr_t>(&tmp);
  op_desc.len = sizeof(uint32_t);
  op_descs2.push_back(op_desc);
  st = client_->TransferSync(op_descs2, READ, kDefaultTimeoutMs);
  EXPECT_EQ(st, PARAM_INVALID);
}

// TransferSync 接口测试：异常场景 - 传输超时
TEST_F(HixlClientUTest, TransferSyncTimeoutTest) {
  StartServer(MockHixlServerMode::k4UbNormal);
  // 初始化 roce 链路
  std::vector<EndPointConfig> local_endpoint_list;
  local_endpoint_list.push_back(MakeRoceDiffNetLocalEp());
  Status st = client_->Initialize(local_endpoint_list);
  EXPECT_EQ(st, SUCCESS);
  // 获取内存信息
  st = client_->SetLocalMemInfo(MakeMemInfoList());
  EXPECT_EQ(st, SUCCESS);
  // 调用 Connect 方法
  st = client_->Connect(kDefaultTimeoutMs);
  EXPECT_EQ(st, SUCCESS);
  // 调用 TransferSync 方法
  std::vector<TransferOpDesc> op_descs;
  TransferOpDesc op_desc;
  op_desc.local_addr = reinterpret_cast<uintptr_t>(&kLocalMems[0]);
  op_desc.remote_addr = reinterpret_cast<uintptr_t>(&kRemoteMems[0]);
  op_desc.len = sizeof(uint32_t);
  op_descs.push_back(op_desc);
  st = client_->TransferSync(op_descs, WRITE, kShortTimeoutMs);
  EXPECT_EQ(st, TIMEOUT);
}

// TransferSync 接口测试：异常场景 - Finalize中断
TEST_F(HixlClientUTest, TransferSyncFinalizeInterruptTest) {
  StartServer(MockHixlServerMode::k4UbNormal);
  // 初始化 roce 链路
  std::vector<EndPointConfig> local_endpoint_list;
  local_endpoint_list.push_back(MakeRoceDiffNetLocalEp());
  Status st = client_->Initialize(local_endpoint_list);
  EXPECT_EQ(st, SUCCESS);
  // 设置内存信息
  st = client_->SetLocalMemInfo(MakeMemInfoList());
  EXPECT_EQ(st, SUCCESS);
  // 连接服务器
  st = client_->Connect(kDefaultTimeoutMs);
  EXPECT_EQ(st, SUCCESS);
  // 准备传输操作
  std::vector<TransferOpDesc> op_descs;
  TransferOpDesc op_desc;
  op_desc.local_addr = reinterpret_cast<uintptr_t>(&kLocalMems[0]);
  op_desc.remote_addr = reinterpret_cast<uintptr_t>(&kRemoteMems[0]);
  op_desc.len = sizeof(uint32_t);
  op_descs.push_back(op_desc);

  // 创建线程执行TransferSync
  std::atomic<Status> transfer_status = SUCCESS;
  std::thread transfer_thread([&]() { transfer_status = client_->TransferSync(op_descs, WRITE, kSleepLongTimeMs); });

  // 等待一段时间确保TransferSync开始执行
  std::this_thread::sleep_for(std::chrono::milliseconds(kMilliSeconds10));

  // 调用Finalize中断传输
  client_->Finalize();

  // 等待线程结束
  if (transfer_thread.joinable()) {
    transfer_thread.join();
  }

  // 验证传输被中断
  EXPECT_EQ(transfer_status, FAILED);
}

// TransferAsync 接口测试：正常场景
TEST_F(HixlClientUTest, TransferAsyncSuccessTest) {
  StartServer(MockHixlServerMode::k4UbNormal);
  // 初始化 roce 链路
  std::vector<EndPointConfig> local_endpoint_list;
  local_endpoint_list.push_back(MakeRoceDiffNetLocalEp());
  Status st = client_->Initialize(local_endpoint_list);
  EXPECT_EQ(st, SUCCESS);
  // 获取内存信息
  st = client_->SetLocalMemInfo(MakeMemInfoList());
  EXPECT_EQ(st, SUCCESS);
  // 调用 Connect 方法
  st = client_->Connect(kDefaultTimeoutMs);
  EXPECT_EQ(st, SUCCESS);
  // 调用 TransferAsync 方法
  std::vector<TransferOpDesc> op_descs;
  TransferOpDesc op_desc;
  op_desc.local_addr = reinterpret_cast<uintptr_t>(&kLocalMems[0]);
  op_desc.remote_addr = reinterpret_cast<uintptr_t>(&kRemoteMems[0]);
  op_desc.len = sizeof(uint32_t);
  op_descs.push_back(op_desc);
  TransferReq req = nullptr;
  st = client_->TransferAsync(op_descs, WRITE, req);
  EXPECT_EQ(st, SUCCESS);
  EXPECT_NE(req, nullptr);
}

// GetTransferStatus 接口测试：正常场景 - WAITING 和 COMPLETED
TEST_F(HixlClientUTest, GetTransferStatusSuccessTest) {
  StartServer(MockHixlServerMode::k4UbNormal);
  // 初始化 roce 链路
  std::vector<EndPointConfig> local_endpoint_list;
  local_endpoint_list.push_back(MakeRoceDiffNetLocalEp());
  Status st = client_->Initialize(local_endpoint_list);
  EXPECT_EQ(st, SUCCESS);
  // 获取内存信息
  st = client_->SetLocalMemInfo(MakeMemInfoList());
  EXPECT_EQ(st, SUCCESS);
  // 调用 Connect 方法
  st = client_->Connect(kDefaultTimeoutMs);
  EXPECT_EQ(st, SUCCESS);
  // 调用 TransferAsync 方法
  std::vector<TransferOpDesc> op_descs;
  TransferOpDesc op_desc;
  op_desc.local_addr = reinterpret_cast<uintptr_t>(&kLocalMems[0]);
  op_desc.remote_addr = reinterpret_cast<uintptr_t>(&kRemoteMems[0]);
  op_desc.len = sizeof(uint32_t);
  op_descs.push_back(op_desc);
  TransferReq req = nullptr;
  st = client_->TransferAsync(op_descs, WRITE, req);
  EXPECT_EQ(st, SUCCESS);
  EXPECT_NE(req, nullptr);
  // 调用 GetTransferStatus 方法
  TransferStatus status = TransferStatus::TIMEOUT;
  st = client_->GetTransferStatus(req, status);
  EXPECT_EQ(st, SUCCESS);
  EXPECT_TRUE(status == TransferStatus::WAITING);
  std::cout << "TransferStatus: " << static_cast<int>(status) << std::endl;

  auto *query_handle = static_cast<CompleteHandle *>(req);
  uint64_t *flag = query_handle->flag_address;
  *flag = uint64_t{1};  // 手动修改标志位为1, 模拟传输完成
  st = client_->GetTransferStatus(req, status);
  EXPECT_EQ(st, SUCCESS);
  EXPECT_EQ(status, TransferStatus::COMPLETED);
  std::cout << "TransferStatus: " << static_cast<int>(status) << std::endl;
}

// GetTransferStatus 接口测试：异常场景 - 未传输
TEST_F(HixlClientUTest, GetTransferStatusNoTransferTest) {
  StartServer(MockHixlServerMode::k4UbNormal);
  // 初始化 roce 链路
  std::vector<EndPointConfig> local_endpoint_list;
  local_endpoint_list.push_back(MakeRoceDiffNetLocalEp());
  Status st = client_->Initialize(local_endpoint_list);
  EXPECT_EQ(st, SUCCESS);
  // 获取内存信息
  st = client_->SetLocalMemInfo(MakeMemInfoList());
  EXPECT_EQ(st, SUCCESS);
  // 调用 Connect 方法
  st = client_->Connect(kDefaultTimeoutMs);
  EXPECT_EQ(st, SUCCESS);
  // 调用 GetTransferStatus 方法
  TransferReq req = nullptr;
  TransferStatus status;
  st = client_->GetTransferStatus(req, status);
  EXPECT_EQ(st, FAILED);
}

// GetTransferStatus 接口测试：异常场景 - req不对
TEST_F(HixlClientUTest, GetTransferStatusReqInvalidTest) {
  StartServer(MockHixlServerMode::k4UbNormal);
  // 初始化 roce 链路
  std::vector<EndPointConfig> local_endpoint_list;
  local_endpoint_list.push_back(MakeRoceDiffNetLocalEp());
  Status st = client_->Initialize(local_endpoint_list);
  EXPECT_EQ(st, SUCCESS);
  // 获取内存信息
  st = client_->SetLocalMemInfo(MakeMemInfoList());
  EXPECT_EQ(st, SUCCESS);
  // 调用 Connect 方法
  st = client_->Connect(kDefaultTimeoutMs);
  EXPECT_EQ(st, SUCCESS);
  // 调用 TransferAsync 方法
  std::vector<TransferOpDesc> op_descs;
  TransferOpDesc op_desc;
  op_desc.local_addr = reinterpret_cast<uintptr_t>(&kLocalMems[0]);
  op_desc.remote_addr = reinterpret_cast<uintptr_t>(&kRemoteMems[0]);
  op_desc.len = sizeof(uint32_t);
  op_descs.push_back(op_desc);
  TransferReq req = nullptr;
  st = client_->TransferAsync(op_descs, WRITE, req);
  EXPECT_EQ(st, SUCCESS);
  EXPECT_NE(req, nullptr);
  // 调用 GetTransferStatus 方法
  TransferStatus status = TransferStatus::TIMEOUT;
  st = client_->GetTransferStatus(static_cast<void *>(static_cast<char *>(req) + 1), status);
  EXPECT_EQ(st, PARAM_INVALID);
  std::cout << "TransferStatus: " << static_cast<int>(status) << std::endl;
  st = client_->Finalize();
  EXPECT_EQ(st, SUCCESS);
  server_->DestroyServerAndUnreg();
}

// client 挂了，server 还在，强制建链测试
TEST_F(HixlClientUTest, ClientCrashTest) {
  StartServer(MockHixlServerMode::k4UbNormal);
  // 初始化 roce 链路
  std::vector<EndPointConfig> local_endpoint_list;
  local_endpoint_list.push_back(MakeRoceDiffNetLocalEp());
  Status st = client_->Initialize(local_endpoint_list);
  EXPECT_EQ(st, SUCCESS);
  // 获取内存信息
  st = client_->SetLocalMemInfo(MakeMemInfoList());
  EXPECT_EQ(st, SUCCESS);
  // 调用 Connect 方法
  st = client_->Connect(kDefaultTimeoutMs);
  EXPECT_EQ(st, SUCCESS);

  // 模拟client崩溃
  client_.reset();
  // 创建新的客户端实例，模拟客户端重启
  client_ = MakeUnique<HixlClient>("127.0.0.1", kServerPort);
  // 重新初始化客户端
  st = client_->Initialize(local_endpoint_list);
  EXPECT_EQ(st, SUCCESS);
  // 重新获取内存信息
  st = client_->SetLocalMemInfo(MakeMemInfoList());
  EXPECT_EQ(st, SUCCESS);
  // 强制建链测试
  st = client_->Connect(kDefaultTimeoutMs);
  EXPECT_EQ(st, SUCCESS);
}

// Segment::AddRange 函数测试：正常场景 - 添加单个内存范围
TEST_F(HixlClientUTest, SegmentAddRangeSuccessTest) {
  Segment segment(MemType::MEM_DEVICE);

  // 测试添加单个内存范围
  Status st = segment.AddRange(0x1000, 0x100);
  EXPECT_EQ(st, SUCCESS);

  // 验证范围是否被正确添加
  EXPECT_TRUE(segment.Contains(0x1000, 0x1100));
}

// Segment::AddRange 函数测试：异常场景 - 地址溢出
TEST_F(HixlClientUTest, SegmentAddRangeOverflowTest) {
  Segment segment(MemType::MEM_DEVICE);

  // 测试添加导致地址溢出的内存范围
  uint64_t start = UINT64_MAX - 0x100;
  uint64_t len = 0x200;
  Status st = segment.AddRange(start, len);
  EXPECT_EQ(st, PARAM_INVALID);
}
}  // namespace hixl