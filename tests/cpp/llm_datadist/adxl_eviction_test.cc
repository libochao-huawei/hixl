#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <vector>
#include <atomic>
#include "adxl/channel.h"
#include "adxl/adxl_engine.h"
#include "adxl/channel_manager.h"
#include "adxl/channel_msg_handler.h"
#include "depends/llm_datadist/src/data_cache_engine_test_helper.h"
#include "depends/mmpa/src/mmpa_stub.h"
using namespace adxl;

class EvictionTest : public ::testing::Test {
 protected:
  void SetUp() override {
    SetMockRtGetDeviceWay(1);
    llm::MockMmpaForHcclApi::Install();
    llm::AutoCommResRuntimeMock::Install();
    llm::HcclAdapter::GetInstance().Initialize();
    options_["adxl.max_channel"] = "10";
    options_["adxl.high_waterline"] = "0.2";
    options_["adxl.low_waterline"] = "0.1";
  }

  void TearDown() override {
  }
  std::map<AscendString, AscendString> options_;
};

// 测试用例1：Client端断链逻辑测试 - 第一个engine作为Client与其他engine建链触发淘汰
TEST_F(EvictionTest, ClientDisconnectHandling) {
  llm::AutoCommResRuntimeMock::SetDevice(0);
  AdxlEngine client_;
  client_.Initialize("127.0.0.1:20000", options_);
  llm::AutoCommResRuntimeMock::SetDevice(1);
  AdxlEngine server_;
  server_.Initialize("127.0.0.1:20001", options_);
  llm::AutoCommResRuntimeMock::SetDevice(2);
  AdxlEngine server1_;
  server1_.Initialize("127.0.0.1:20002", options_);
  llm::AutoCommResRuntimeMock::SetDevice(3);
  AdxlEngine server2_;
  server2_.Initialize("127.0.0.1:20003", options_);
  EXPECT_EQ(client_.Connect("127.0.0.1:20001"), SUCCESS);
  EXPECT_EQ(client_.Connect("127.0.0.1:20001"), ALREADY_CONNECTED);
  EXPECT_EQ(client_.Connect("127.0.0.1:20002"), SUCCESS);
  EXPECT_EQ(client_.Connect("127.0.0.1:20003"), SUCCESS);

  std::this_thread::sleep_for(std::chrono::milliseconds(2000));

  EXPECT_EQ(client_.Connect("127.0.0.1:20001"), SUCCESS);

  client_.Finalize();
  server_.Finalize();
  server1_.Finalize();
  server2_.Finalize();
}

// 测试用例2：Server端断链逻辑测试 - 其他engine作为Client与第一个engine建链触发淘汰
TEST_F(EvictionTest, ServerDisconnectHandling) {
  llm::AutoCommResRuntimeMock::SetDevice(0);
  AdxlEngine server_;
  server_.Initialize("127.0.0.1:26000", options_);
  llm::AutoCommResRuntimeMock::SetDevice(1);
  AdxlEngine client_;
  client_.Initialize("127.0.0.1:26001", options_);
  llm::AutoCommResRuntimeMock::SetDevice(2);
  AdxlEngine client1_;
  client1_.Initialize("127.0.0.1:26002", options_);
  llm::AutoCommResRuntimeMock::SetDevice(3);
  AdxlEngine client2_;
  client2_.Initialize("127.0.0.1:26003", options_);

  EXPECT_EQ(client_.Connect("127.0.0.1:26000"), SUCCESS);
  EXPECT_EQ(client_.Connect("127.0.0.1:26000"), ALREADY_CONNECTED);
  EXPECT_EQ(client1_.Connect("127.0.0.1:26000"), SUCCESS);
  EXPECT_EQ(client2_.Connect("127.0.0.1:26000"), SUCCESS);

  std::this_thread::sleep_for(std::chrono::milliseconds(2000));
  EXPECT_EQ(client_.Connect("127.0.0.1:26000"), SUCCESS);

  server_.Finalize();
  client_.Finalize();
  client1_.Finalize();
  client2_.Finalize();
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
