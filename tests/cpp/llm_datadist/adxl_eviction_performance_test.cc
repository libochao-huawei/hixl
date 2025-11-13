/**
 * ADXL链路淘汰机制性能测试
 * 
 * 测试目标：
 * 1. 断链延迟测试
 * 2. 连接建立延迟测试（高水位时）
 * 3. 并发断链吞吐量测试
 */

#include <gtest/gtest.h>
#include <chrono>
#include <vector>
#include <thread>
#include <atomic>
#include <numeric>
#include <algorithm>
#include "adxl/adxl_inner_engine.h"

using namespace adxl;
using namespace std::chrono;

class EvictionPerformanceTest : public ::testing::Test {
 protected:
  void SetUp() override {
    options_["adxl.max_channel"] = "10";
    options_["adxl.high_waterline"] = "0.8";
    options_["adxl.low_waterline"] = "0.7";

    for (int i = 0; i < 10; i++) {
      std::string channel_id = "127.0.0.1:" + std::to_string(20001 + i);
      channel_ids_.emplace_back(channel_id);
      
      AdxlInnerEngine engine(channel_id);
      engine.Initialize(options_);
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
  std::vector<std::string> channel_ids_;
  std::vector<AdxlInnerEngine> engines_;
  
  // 计算统计信息
  struct Statistics {
    double mean;
    double p50;
    double p99;
    double max;
  };
  
  Statistics CalculateStatistics(const std::vector<int>& values) {
    Statistics stats{};
    if (values.empty()) {
      return stats;
    }
    
    std::vector<int> sorted = values;
    std::sort(sorted.begin(), sorted.end());
    
    stats.mean = std::accumulate(sorted.begin(), sorted.end(), 0.0) / sorted.size();
    stats.p50 = sorted[sorted.size() * 0.5];
    stats.p99 = sorted[sorted.size() * 0.99];
    stats.max = sorted.back();
    
    return stats;
  }
};

// 性能测试1：断链延迟测试
TEST_F(EvictionPerformanceTest, DisconnectLatency) {
  // 建立连接
  std::string local_engine = "192.168.1.1:8080";
  std::string remote_engine = "192.168.1.2:8080";
  
  AdxlInnerEngine server(local_engine);
  AdxlInnerEngine client(remote_engine);
  server.Connect(remote_engine, 5000);
  
  // 测量断链延迟
  std::vector<int> latencies;
  const int num_tests = 100;
  
  for (int i = 0; i < num_tests; i++) {
    // 重新建立连接
    server.Connect(remote_engine, 5000);
    std::this_thread::sleep_for(milliseconds(10));
    
    auto start = high_resolution_clock::now();
    server.Disconnect(remote_engine, 1000);
    auto end = high_resolution_clock::now();
    
    auto latency = duration_cast<milliseconds>(end - start).count();
    latencies.push_back(latency);
  }
  
  // 计算统计信息
  Statistics stats = CalculateStatistics(latencies);
  
  std::cout << "Disconnect Latency Statistics:" << std::endl;
  std::cout << "  Mean: " << stats.mean << " ms" << std::endl;
  std::cout << "  P50: " << stats.p50 << " ms" << std::endl;
  std::cout << "  P99: " << stats.p99 << " ms" << std::endl;
  std::cout << "  Max: " << stats.max << " ms" << std::endl;
  
  // 验证目标：平均延迟 < 100ms
  ASSERT_LT(stats.mean, 100);
}

// 性能测试2：连接建立延迟测试（高水位时）
TEST_F(EvictionPerformanceTest, ConnectLatencyUnderWaterline) {
  // 设置水位线
  std::map<AscendString, AscendString> options;
  options["adxl.max_channel"] = "100";
  options["adxl.high_waterline"] = "0.9";
  options["adxl.low_waterline"] = "0.8";
  
  AdxlInnerEngine engine("192.168.1.1:8080");
  engine.Initialize(options);
  
  // 建立连接到高水位
  for (int i = 0; i < 90; i++) {
    std::string remote = "192.168.1." + std::to_string(i+2) + ":8080";
    engine.Connect(remote, 5000);
  }
  
  // 测量在高水位时建立新连接的延迟
  std::vector<int> latencies;
  const int num_tests = 100;
  
  for (int i = 0; i < num_tests; i++) {
    std::string remote = "192.168.1.102:8080";
    
    auto start = high_resolution_clock::now();
    engine.Connect(remote, 5000);
    auto end = high_resolution_clock::now();
    
    auto latency = duration_cast<milliseconds>(end - start).count();
    latencies.push_back(latency);
    
    engine.Disconnect(remote, 1000);
  }
  
  // 计算统计信息
  Statistics stats = CalculateStatistics(latencies);
  
  std::cout << "Connect Latency Under Waterline Statistics:" << std::endl;
  std::cout << "  Mean: " << stats.mean << " ms" << std::endl;
  std::cout << "  P50: " << stats.p50 << " ms" << std::endl;
  std::cout << "  P99: " << stats.p99 << " ms" << std::endl;
  std::cout << "  Max: " << stats.max << " ms" << std::endl;
}

// 性能测试3：并发断链吞吐量测试
TEST_F(EvictionPerformanceTest, ConcurrentDisconnectThroughput) {
  // 建立100个连接
  AdxlInnerEngine engine("192.168.1.1:8080");
  std::vector<std::string> channels;
  for (int i = 0; i < 100; i++) {
    std::string remote = "192.168.1." + std::to_string(i+2) + ":8080";
    engine.Connect(remote, 5000);
    channels.push_back(remote);
  }
  
  // 并发断链
  auto start = high_resolution_clock::now();
  std::vector<std::thread> threads;
  
  for (const auto& channel : channels) {
    threads.emplace_back([&engine, &channel]() {
      engine.Disconnect(channel, 1000);
    });
  }
  
  for (auto& t : threads) {
    t.join();
  }
  auto end = high_resolution_clock::now();
  
  auto duration = duration_cast<milliseconds>(end - start).count();
  double throughput = 100.0 / (duration / 1000.0);
  
  std::cout << "Concurrent Disconnect Throughput: " << throughput << " req/s" << std::endl;
  std::cout << "Total time: " << duration << " ms" << std::endl;
}

int main(int argc, char **argv) {
  // ::testing::InitGoogleTest(&argc, argv);
  // return RUN_ALL_TESTS();
}

