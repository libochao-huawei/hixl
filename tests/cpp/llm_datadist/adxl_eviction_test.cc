#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <vector>
#include <atomic>
#include "adxl/channel.h"
#include "adxl/adxl_engine.h"
#include "adxl/channel_manager.h"
#include "adxl/channel_msg_handler.h"

using namespace adxl;

class EvictionTest : public ::testing::Test {
 protected:
  void SetUp() override {
    options_["adxl.max_channel"] = "10";
    options_["adxl.high_waterline"] = "0.8";
    options_["adxl.low_waterline"] = "0.7";

    for (int i = 0; i < 10; i++) {
      AscendString channel_id = ("127.0.0.1:" + std::to_string(20001 + i)).c_str();
      channel_ids_.emplace_back(channel_id);
      
      AdxlEngine engine;
      engines_.emplace_back(std::move(engine));
    }
  }

  void TearDown() override {
    for (auto& engine : engines_) {
      engine.Finalize();
    }
    engines_.clear();
    channel_ids_.clear();
    options_.clear();
  }

  std::map<AscendString, AscendString> options_;
  std::vector<AscendString> channel_ids_;
  std::vector<AdxlEngine> engines_;
};

// 测试用例1：Client端断链逻辑测试 - 第一个engine作为Client与其他engine建链触发淘汰
TEST_F(EvictionTest, ClientDisconnectHandling) {
  AdxlEngine engine = engines_[0];
  engine.Initialize("127.0.0.1", options_);
  for (int i = 1; i < 10; i++) {
    engines_[i].Initialize(channel_ids_[i], options_);
    Status ret = engine.Connect(channel_ids_[i], 5000);
    ASSERT_EQ(ret, SUCCESS);
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(5000));

  auto channels = engine.channel_manager_->GetAllClientChannel();

  for (auto channel : channels) {
    ASSERT_FALSE(channel->IsDisconnecting()) << "Channel " << channel->GetChannelId() << " should not be disconnecting";
  }
  ASSERT_EQ(channels.size(), 7);
}

// 测试用例2：Server端断链逻辑测试 - 其他engine作为Client与第一个engine建链触发淘汰
TEST_F(EvictionTest, ServerDisconnectHandling) {
  AdxlEngine engine = engines_[0];
  engine.Initialize(channel_ids[0], options_);
  
  for (int i = 1; i < 10; i++) {  // 索引1~9的engine作为Client连接到engine0
    engines_[i].Initialize("127.0.0.1", options_);
    Status ret = engines_[i].Connect(channel_ids_[0], 5000);
    ASSERT_EQ(ret, SUCCESS);
  }

  // 等待连接建立和淘汰处理完成
  std::this_thread::sleep_for(std::chrono::milliseconds(5000));

  // 检查所有engine的channel，验证淘汰流程
  AdxlEngine engine1 = engines_[1];
  auto engine_server_channels = engine.channel_manager_->GetAllServerChannel();
  auto engine1_client_channels = engine1.channel_manager_->GetAllClientChannel();
  
  for (auto channel : engine_server_channels) {
    ASSERT_FALSE(channel->IsDisconnecting()) << "Channel " << channel->GetChannelId() << " should not be disconnecting";
  }
  for (auto channel : engine1_client_channels) {
    ASSERT_FALSE(channel->IsDisconnecting()) << "Channel " << channel->GetChannelId() << " should not be disconnecting";
  }
  
  ASSERT_EQ(server_channel_count, 7);
  ASSERT_EQ(first_client_channel_count, 0);
}

// 测试用例3：原子变量计数测试
TEST_F(EvictionTest, AtomicCounters) {
  ChannelInfo channel_info{};
  channel_info.channel_type = ChannelType::kClient;
  channel_info.channel_id = "test_channel";
  channel_info.peer_rank_id = 1;
  channel_info.local_rank_id = 0;
  
  ChannelPtr channel = std::make_shared<Channel>(channel_info);
  channel->Initialize();
  
  ASSERT_EQ(channel->GetTransferCount(), 0);
  ASSERT_FALSE(channel->GetHasTransferred());
  ASSERT_FALSE(channel->IsDisconnecting());

  const int num_threads = 10;
  const int transfers_per_thread = 100;
  std::vector<std::thread> threads;
  
  for (int i = 0; i < num_threads; i++) {
    threads.emplace_back([&channel, transfers_per_thread]() {
      for (int j = 0; j < transfers_per_thread; j++) {
        channel->IncrementTransferCount();
        channel->SetHasTransferred(true);
        std::this_thread::sleep_for(std::chrono::microseconds(10));
        channel->DecrementTransferCount();
      }
    });
  }
  
  for (auto& t : threads) {
    t.join();
  }
  ASSERT_EQ(channel->GetTransferCount(), 0);
  ASSERT_TRUE(channel->GetHasTransferred());
}

// 主函数
int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
