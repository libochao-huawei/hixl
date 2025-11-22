#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <vector>
#include <atomic>
#include <algorithm>
#include "adxl/channel.h"
#include "adxl/channel_manager.h"
#include "adxl/channel_msg_handler.h"
#include "adxl/channel_evictor.h"
#include "adxl/buffer_transfer_service.h"
#include "hixl/hixl.h"
#include "depends/llm_datadist/src/data_cache_engine_test_helper.h"
#include "depends/mmpa/src/mmpa_stub.h"

using namespace adxl;

class MockChannelEvictor : public ChannelEvictor {
public:
  MockChannelEvictor(ChannelManager* channel_manager, ChannelMsgHandler* channel_msg_handler)
    : ChannelEvictor(channel_manager, channel_msg_handler) {}
  
  ~MockChannelEvictor() = default;

  bool ProcessEvictionByChannelId(ChannelType channel_type, const std::string& channel_id) {
    return (channel_manager_->DestroyChannel(channel_type, channel_id) == SUCCESS);
  }
};

class ChannelEvictorWhiteboxTest : public ::testing::Test {
protected:
  void SetUp() override {
    channel_options_["adxl.max_channel"] = "10";
    channel_options_["adxl_high_waterline"] = "0.8";
    channel_options_["adxl_low_waterline"] = "0.5";

    SetMockRtGetDeviceWay(1);
    llm::MockMmpaForHcclApi::Install();
    llm::AutoCommResRuntimeMock::Install();
    llm::HcclAdapter::GetInstance().Initialize();
    llm::AutoCommResRuntimeMock::SetDevice(0);

    channel_manager_ = std::make_unique<ChannelManager>();
    Status ret = channel_manager_->Initialize(buffer_transfer_service_.get());
    ASSERT_EQ(ret, SUCCESS) << "Failed to initialize ChannelManager";
    std::string listen_info = "127.0.0.1:20000";
    msg_handler_ = std::make_unique<ChannelMsgHandler>(listen_info, channel_manager_.get());
    channel_evictor_ = std::make_unique<ChannelEvictor>(channel_manager_.get(), msg_handler_.get());
  }

  void TearDown() override {
    if (channel_evictor_) {
        channel_evictor_->Finalize();
    }
    
    if (channel_manager_) {
        channel_manager_->Finalize();
    }

    if (msg_handler_) {
        msg_handler_->Finalize();
    }
    if (buffer_transfer_service_) {
        buffer_transfer_service_->Finalize();
    }

    llm::HcclAdapter::GetInstance().Finalize();
    llm::MockMmpaForHcclApi::Reset();
    llm::AutoCommResRuntimeMock::Reset();
    SetMockRtGetDeviceWay(0);
  }

  void CreateChannels(int count, ChannelType channel_type = ChannelType::kClient) {
    for (int i = 0; i < count; i++) {
      std::string channel_id = channel_type == ChannelType::kClient 
                    ? "127.0.0.1" + std::to_string(20000 + channel_manager_->GetAllClientChannel().size())
                    : "127.0.0.1" + std::to_string(26000 + channel_manager_->GetAllServerChannel().size());

      ChannelInfo channel_info{};
      channel_info.channel_type = channel_type;
      channel_info.channel_id = channel_id;
      channel_info.peer_rank_id = 1;
      channel_info.local_rank_id = 0;

      ChannelPtr created_channel;
      LLMLOGI("Create channel: %s", channel_id.c_str());
      Status ret = channel_manager_->CreateChannel(channel_info, created_channel);
      ASSERT_EQ(ret, SUCCESS) << "Failed to create channel: " << channel_id;

      created_channel_ids_.push_back(channel_id);
    }
  }

  bool ChannelExists(const std::string& channel_id, ChannelType channel_type = ChannelType::kClient) {
    ChannelPtr channel = channel_manager_->GetChannel(channel_type, channel_id);
    if (channel) {
      return true;
    }

    channel = channel_manager_->GetChannel(channel_type == ChannelType::kClient ? ChannelType::kServer : ChannelType::kClient, channel_id);
    return channel != nullptr;
  }

  std::vector<std::string> GetCurrentChannelIds() const {
    std::vector<std::string> channel_ids;

    auto client_channels = channel_manager_->GetAllClientChannel();
    for (const auto& channel : client_channels) {
      channel_ids.push_back(channel->GetChannelId());
    }

    auto server_channels = channel_manager_->GetAllServerChannel();
    for (const auto& channel : server_channels) {
      channel_ids.push_back(channel->GetChannelId());
    }

    return channel_ids;
  }

  int GetCurrentChannelCount() const {
    return channel_manager_->GetAllClientChannel().size() + channel_manager_->GetAllServerChannel().size();
  }

  std::unique_ptr<BufferTransferService> buffer_transfer_service_;
  std::unique_ptr<ChannelManager> channel_manager_;
  std::unique_ptr<ChannelMsgHandler> msg_handler_;
  std::unique_ptr<ChannelEvictor> channel_evictor_;

  std::map<AscendString, AscendString> channel_options_;
  std::vector<std::string> created_channel_ids_;
};

//测试水位解析
TEST_F(ChannelEvictorWhiteboxTest, TestWaterline) {
  channel_options_["adxl.high_waterline"] = "0.8";
  channel_options_["adxl.low_waterline"] = "0.9";
  EXPECT_EQ(channel_evictor_->Initialize(channel_options_), PARAM_INVALID);
  channel_options_["adxl.high_waterline"] = "1.2";
  EXPECT_EQ(channel_evictor_->Initialize(channel_options_), PARAM_INVALID);
  channel_options_["adxl.high_waterline"] = "abx";
  EXPECT_EQ(channel_evictor_->Initialize(channel_options_), PARAM_INVALID);
  channel_options_["adlx.low_waterline"] = "0.5";
  EXPECT_EQ(channel_evictor_->Initialize(channel_options_), SUCCESS);
}

// 测试淘汰控制函数
TEST_F(ChannelEvictorWhiteboxTest, TestTrigger) {
  Status ret = channel_evictor_->Initialize(channel_options_);
  ASSERT_EQ(ret, SUCCESS);

  EXPECT_EQ(GetCurrentChannelCount(), 0);
  EXPECT_EQ(created_channel_ids_.size(), 0);

  EXPECT_FALSE(channel_evictor_->ShouldTriggerEviction());
  EXPECT_TRUE(channel_evictor_->ShouldStopEviction());

  CreateChannels(3, ChannelType::kClient);
  EXPECT_EQ(GetCurrentChannelCount(), 3);
  EXPECT_FALSE(channel_evictor_->ShouldTriggerEviction());

  CreateChannels(5, ChannelType::kClient);
  EXPECT_EQ(GetCurrentChannelCount(), 8);
  EXPECT_TRUE(channel_evictor_->ShouldTriggerEviction());

  CreateChannels(1, ChannelType::kClient);
  EXPECT_EQ(GetCurrentChannelCount(), 9);
  EXPECT_TRUE(channel_evictor_->ShouldTriggerEviction());
  EXPECT_FALSE(channel_evictor_->ShouldStopEviction());
}

// 无数据传输时，Client是否按照创建时间顺序断链
TEST_F(ChannelEvictorWhiteboxTest, TestChannelEvictionByCreateTime) { 
  Status ret = channel_evictor_->Initialize(channel_options_);
  ASSERT_EQ(ret, SUCCESS);

  CreateChannels(8, ChannelType::kClient);
  EXPECT_TRUE(channel_evictor_->ShouldTriggerEviction());

  auto current_channels = GetCurrentChannelIds();
  EXPECT_EQ(current_channels.size(), 8);

  for (const auto& channel_id : created_channel_ids_) {
    EXPECT_TRUE(std::find(current_channels.begin(), current_channels.end(), channel_id) != current_channels.end());
  }

  CreateChannels(1, ChannelType::kClient);
  channel_evictor_->MaybeScheduleEviction();
  std::this_thread::sleep_for(std::chrono::milliseconds(5000));
  EXPECT_EQ(GetCurrentChannelCount(), 5);
  EXPECT_FALSE(ChannelExists("127.0.0.1:20000", ChannelType::kClient));
  EXPECT_FALSE(ChannelExists("127.0.0.1:20001", ChannelType::kClient));
  EXPECT_FALSE(ChannelExists("127.0.0.1:20002", ChannelType::kClient));
  EXPECT_FALSE(ChannelExists("127.0.0.1:20003", ChannelType::kClient));
  EXPECT_TRUE(ChannelExists("127.0.0.1:20004", ChannelType::kClient));
}

// Client有传输任务时，断链优先选择没有传输过的
TEST_F(ChannelEvictorWhiteboxTest, TestClientEvictionByTransferFlag) {
  Status ret = channel_evictor_->Initialize(channel_options_);
  ASSERT_EQ(ret, SUCCESS);

  CreateChannels(8, ChannelType::kClient);
  EXPECT_TRUE(channel_evictor_->ShouldTriggerEviction());

  auto current_channels = GetCurrentChannelIds();
  EXPECT_EQ(current_channels.size(), 8);

  for (const auto& channel_id : created_channel_ids_) {
    EXPECT_TRUE(std::find(current_channels.begin(), current_channels.end(), channel_id) != current_channels.end());
  }

  ChannelPtr trans_channel = channel_manager_->GetChannel(ChannelType::kClient, "127.0.0.1:20000");
  trans_channel->SetHasTransferred(true);
  CreateChannels(1, ChannelType::kClient);
  channel_evictor_->MaybeScheduleEviction();
  std::this_thread::sleep_for(std::chrono::milliseconds(5000));
  EXPECT_EQ(GetCurrentChannelCount(), 5);
  EXPECT_TRUE(ChannelExists("127.0.0.1:20000", ChannelType::kClient));
  EXPECT_FALSE(ChannelExists("127.0.0.1:20001", ChannelType::kClient));
  EXPECT_FALSE(ChannelExists("127.0.0.1:20002", ChannelType::kClient));
  EXPECT_FALSE(ChannelExists("127.0.0.1:20003", ChannelType::kClient));
  EXPECT_FALSE(ChannelExists("127.0.0.1:20004", ChannelType::kClient));
}

TEST_F(ChannelEvictorWhiteboxTest, TestMixChannelStrategy) {
  std::unique_ptr<MockChannelEvictor> mock_channel_evictor_;
  std::string mock_listen_info = "127.0.0.1:24000";
  mock_channel_evictor_ = std::make_unique<MockChannelEvictor>(channel_manager_.get(), msg_handler_.get());
  Status ret = mock_channel_evictor_->Initialize(channel_options_);
  ASSERT_EQ(ret, SUCCESS);

  CreateChannels(4, ChannelType::kClient);
  CreateChannels(4, ChannelType::kServer);
  EXPECT_TRUE(mock_channel_evictor_->ShouldTriggerEviction());

  auto current_channels = GetCurrentChannelIds();
  EXPECT_EQ(current_channels.size(), 8);

  for(const auto& channel_id : created_channel_ids_) {
    EXPECT_TRUE(std::find(current_channels.begin(), current_channels.end(), channel_id) != current_channels.end());
  }

  ChannelPtr trans_channel = channel_manager_->GetChannel(ChannelType::kClient, "127.0.0.1:20000");
  trans_channel->SetHasTransferred(true);
  ChannelPtr server_trans_channel = channel_manager_->GetChannel(ChannelType::kServer, "127.0.0.1:26000");
  server_trans_channel->SetHasTransferred(true);
  CreateChannels(1, ChannelType::kClient);
  while(!mock_channel_evictor_->ShouldStopEviction()) {
    std::optional<EvictItem> item = mock_channel_evictor_->SelectOneEvictionCandidate();
    if(item.has_value()) {
      mock_channel_evictor_->ProcessEvictionByChannelId(item->channel_type, item->channel_id);
    }
  }
  EXPECT_EQ(GetCurrentChannelCount(), 5);
  EXPECT_TRUE(ChannelExists("127.0.0.1:20000", ChannelType::kClient));
  EXPECT_TRUE(ChannelExists("127.0.0.1:26000", ChannelType::kServer));
  EXPECT_FALSE(ChannelExists("127.0.0.1:20001", ChannelType::kClient));
  EXPECT_FALSE(ChannelExists("127.0.0.1:20002", ChannelType::kClient));
  EXPECT_FALSE(ChannelExists("127.0.0.1:20003", ChannelType::kClient));
  EXPECT_FALSE(ChannelExists("127.0.0.1:26001", ChannelType::kServer));

  EXPECT_EQ(channel_manager_->GetAllClientChannel().size(), 2);
  EXPECT_EQ(channel_manager_->GetAllServerChannel().size(), 3);
  mock_channel_evictor_->Finalize();
}