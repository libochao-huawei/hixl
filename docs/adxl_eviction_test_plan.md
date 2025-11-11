# ADXL 链路淘汰机制测试方案

## 1. 功能测试用例

### 1.1 Client端断链逻辑测试

#### 测试目标
验证Client端能够正确处理Server发送的`kRequestDisconnect`消息，并返回响应。

#### 测试步骤
1. 启动单机多卡环境（至少2个卡）
2. 建立Client-Server连接
3. 在Server端触发淘汰机制，选择Server通道进行淘汰
4. Server发送`kRequestDisconnect`消息给Client
5. 验证Client端：
   - 能够接收并解析`kRequestDisconnect`消息
   - 检查通道状态（`transfer_count`、`disconnect_flag`）
   - 如果通道空闲，执行`Disconnect`
   - 发送`kRequestDisconnectResp`响应给Server
   - 响应中包含正确的状态信息（`disconnected`、`error_code`等）

#### 预期结果

- Client能够正确处理断链请求
- 空闲通道能够成功断链
- 忙碌通道返回`BUSY`错误
- Server能够收到响应

#### 测试代码示例
```cpp
// 测试用例：test_client_disconnect_handling.cpp
TEST(EvictionTest, ClientDisconnectHandling) {
  // 1. 初始化引擎
  AdxlInnerEngine engine1("192.168.1.1:8080");
  AdxlInnerEngine engine2("192.168.1.2:8080");
  
  // 2. 建立连接
  engine1.Connect("192.168.1.2:8080", 5000);
  
  // 3. 等待连接建立
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  
  // 4. 在Server端触发淘汰（通过设置低水位线）
  // 5. 验证Client端收到断链请求并响应
  // 6. 验证连接已断开
}
```

### 1.2 Server端断链逻辑测试

#### 测试目标
验证Server端能够发送`kRequestDisconnect`消息，并等待Client响应。

#### 测试步骤
1. 启动单机多卡环境
2. 建立多个Client-Server连接
3. 设置水位线配置，触发淘汰机制
4. 验证Server端：
   - 能够选择需要淘汰的Server通道
   - 发送`kRequestDisconnect`消息给对应的Client
   - 等待Client响应（最多2秒）
   - 根据响应判断断链是否成功
   - 如果超时或失败，记录日志并继续

#### 预期结果
- Server能够正确发送断链请求
- Server能够等待并处理Client响应
- 成功断链的通道被正确移除
- 失败的断链请求不影响其他通道

#### 测试代码示例
```cpp
// 测试用例：test_server_disconnect_handling.cpp
TEST(EvictionTest, ServerDisconnectHandling) {
  // 1. 初始化多个引擎
  std::vector<AdxlInnerEngine> engines;
  for (int i = 0; i < 10; i++) {
    engines.emplace_back("192.168.1." + std::to_string(i+1) + ":8080");
  }
  
  // 2. 建立连接
  for (int i = 1; i < 10; i++) {
    engines[0].Connect(engines[i].GetListenInfo(), 5000);
  }
  
  // 3. 设置水位线触发淘汰
  // 4. 验证Server端发送请求并等待响应
  // 5. 验证通道数量降低到低水位线
}
```

### 1.3 原子变量计数测试

#### 测试目标
验证`transfer_count_`和`has_transfered_`原子变量的并发安全性。

#### 测试步骤
1. 创建多个线程同时进行数据传输
2. 每个线程调用`TransferSync`或`TransferAsync`
3. 验证：
   - `transfer_count_`在传输开始时增加，结束时减少
   - `has_transfered_`在第一次传输后设置为`true`
   - 多个线程并发访问时，计数正确
   - 没有数据竞争

#### 预期结果
- 所有传输完成后，`transfer_count_`为0
- `has_transfered_`正确反映传输状态
- 没有并发问题

#### 测试代码示例
```cpp
// 测试用例：test_atomic_counters.cpp
TEST(EvictionTest, AtomicCounters) {
  AdxlInnerEngine engine1("192.168.1.1:8080");
  AdxlInnerEngine engine2("192.168.1.2:8080");
  engine1.Connect("192.168.1.2:8080", 5000);
  
  auto channel = GetChannel("192.168.1.2:8080");
  
  // 并发传输测试
  std::vector<std::thread> threads;
  const int num_threads = 10;
  const int transfers_per_thread = 100;
  
  for (int i = 0; i < num_threads; i++) {
    threads.emplace_back([&channel, transfers_per_thread]() {
      for (int j = 0; j < transfers_per_thread; j++) {
        // 执行传输
        channel->TransferSync(...);
      }
    });
  }
  
  for (auto& t : threads) {
    t.join();
  }
  
  // 验证计数为0
  ASSERT_EQ(channel->GetTransferCount(), 0);
  ASSERT_TRUE(channel->GetHasTransferred());
}
```

### 1.4 水位线机制测试

#### 测试目标
验证水位线机制能够正常工作，达到高水位后能够降低到低水位。

#### 测试步骤
1. 设置水位线配置：
   - `max_channel = 100`
   - `high_waterline = 0.9` (90个通道)
   - `low_waterline = 0.8` (80个通道)
2. 建立连接直到达到高水位（90个通道）
3. 继续建立连接，触发淘汰机制
4. 验证：
   - 达到高水位时触发淘汰
   - 淘汰机制开始工作
   - 通道数量降低到低水位（80个通道）
   - 淘汰策略正确（优先淘汰未传输的通道）

#### 预期结果
- 达到高水位时触发淘汰
- 通道数量能够降低到低水位
- 淘汰策略按预期工作

#### 测试代码示例
```cpp
// 测试用例：test_waterline_mechanism.cpp
TEST(EvictionTest, WaterlineMechanism) {
  // 设置水位线
  std::map<AscendString, AscendString> options;
  options["adxl.max_channel"] = "100";
  options["adxl.high_waterline"] = "0.9";
  options["adxl.low_waterline"] = "0.8";
  
  AdxlInnerEngine engine("192.168.1.1:8080");
  engine.Initialize(options);
  
  // 建立连接直到高水位
  std::vector<std::string> remote_engines;
  for (int i = 0; i < 90; i++) {
    std::string remote = "192.168.1." + std::to_string(i+2) + ":8080";
    engine.Connect(remote, 5000);
    remote_engines.push_back(remote);
  }
  
  // 验证达到高水位
  ASSERT_GE(GetTotalChannelCount(), 90);
  
  // 继续建立连接，触发淘汰
  for (int i = 90; i < 95; i++) {
    std::string remote = "192.168.1." + std::to_string(i+2) + ":8080";
    engine.Connect(remote, 5000);
  }
  
  // 等待淘汰完成
  std::this_thread::sleep_for(std::chrono::seconds(5));
  
  // 验证降低到低水位
  ASSERT_LE(GetTotalChannelCount(), 80);
}
```

## 2. 性能测试方案

### 2.1 断链流程性能损耗评测

#### 测试目标
评估断链流程对系统性能的影响，包括：
- 断链请求-响应延迟
- 淘汰机制对正常连接建立的影响
- 并发断链的性能

#### 测试指标
1. **断链延迟**：
   - Server发送请求到收到响应的时间
   - 目标：< 100ms（正常情况）
   - 超时情况：2秒

2. **连接建立延迟**：
   - 在高水位时建立新连接的延迟
   - 目标：不应显著增加（< 10%）

3. **并发断链吞吐量**：
   - 每秒能够处理的断链请求数
   - 目标：> 10 req/s

4. **内存占用**：
   - 淘汰机制相关的内存开销
   - 目标：< 1MB

#### 测试方法

##### 2.1.1 断链延迟测试
```cpp
// 性能测试：benchmark_disconnect_latency.cpp
void BenchmarkDisconnectLatency() {
  // 建立连接
  AdxlInnerEngine server("192.168.1.1:8080");
  AdxlInnerEngine client("192.168.1.2:8080");
  server.Connect("192.168.1.2:8080", 5000);
  
  // 测量断链延迟
  auto start = std::chrono::high_resolution_clock::now();
  server.Disconnect("192.168.1.2:8080", 1000);
  auto end = std::chrono::high_resolution_clock::now();
  
  auto latency = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
  std::cout << "Disconnect latency: " << latency.count() << " ms" << std::endl;
  
  // 重复测试100次，计算平均值和P99
}
```

##### 2.1.2 连接建立延迟测试
```cpp
// 性能测试：benchmark_connect_latency_under_waterline.cpp
void BenchmarkConnectLatencyUnderWaterline() {
  AdxlInnerEngine engine("192.168.1.1:8080");
  
  // 建立连接到高水位
  for (int i = 0; i < 90; i++) {
    engine.Connect("192.168.1." + std::to_string(i+2) + ":8080", 5000);
  }
  
  // 测量在高水位时建立新连接的延迟
  std::vector<int> latencies;
  for (int i = 0; i < 100; i++) {
    auto start = std::chrono::high_resolution_clock::now();
    engine.Connect("192.168.1.102:8080", 5000);
    auto end = std::chrono::high_resolution_clock::now();
    latencies.push_back(
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count()
    );
    engine.Disconnect("192.168.1.102:8080", 1000);
  }
  
  // 计算统计信息
  CalculateStatistics(latencies);
}
```

##### 2.1.3 并发断链吞吐量测试
```cpp
// 性能测试：benchmark_concurrent_disconnect_throughput.cpp
void BenchmarkConcurrentDisconnectThroughput() {
  // 建立100个连接
  AdxlInnerEngine engine("192.168.1.1:8080");
  std::vector<std::string> channels;
  for (int i = 0; i < 100; i++) {
    std::string remote = "192.168.1." + std::to_string(i+2) + ":8080";
    engine.Connect(remote, 5000);
    channels.push_back(remote);
  }
  
  // 并发断链
  auto start = std::chrono::high_resolution_clock::now();
  std::vector<std::thread> threads;
  for (const auto& channel : channels) {
    threads.emplace_back([&engine, &channel]() {
      engine.Disconnect(channel, 1000);
    });
  }
  
  for (auto& t : threads) {
    t.join();
  }
  auto end = std::chrono::high_resolution_clock::now();
  
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
  double throughput = 100.0 / (duration.count() / 1000.0);
  std::cout << "Concurrent disconnect throughput: " << throughput << " req/s" << std::endl;
}
```

### 2.2 性能基准测试脚本

#### 单机多卡测试脚本
```bash
#!/bin/bash
# test_eviction_performance.sh

# 配置
MAX_CHANNEL=100
HIGH_WATERLINE=0.9
LOW_WATERLINE=0.8
NUM_CARDS=8

# 编译测试程序
make test_eviction_performance

# 运行测试
for test in disconnect_latency connect_latency concurrent_disconnect; do
  echo "Running $test..."
  ./test_eviction_performance --test=$test \
    --max_channel=$MAX_CHANNEL \
    --high_waterline=$HIGH_WATERLINE \
    --low_waterline=$LOW_WATERLINE \
    --num_cards=$NUM_CARDS \
    --output=results_${test}.json
done

# 生成报告
python generate_performance_report.py results_*.json
```

### 2.3 性能测试报告模板

```markdown
# 性能测试报告

## 测试环境
- 硬件：单机8卡
- 软件版本：xxx
- 测试时间：2025-01-XX

## 测试结果

### 1. 断链延迟
- 平均值：XX ms
- P50：XX ms
- P99：XX ms
- 最大延迟：XX ms

### 2. 连接建立延迟（高水位时）
- 平均值：XX ms
- 增加比例：XX%
- 是否满足目标：是/否

### 3. 并发断链吞吐量
- 吞吐量：XX req/s
- 是否满足目标：是/否

### 4. 内存占用
- 淘汰机制内存：XX KB
- 是否满足目标：是/否

## 结论
[总结性能测试结果，是否满足要求]
```

## 3. 测试执行计划

### 3.1 测试环境准备
1. 准备单机多卡环境（至少4卡，推荐8卡）
2. 编译测试程序
3. 配置网络环境

### 3.2 测试顺序
1. 先执行功能测试，确保基本功能正常
2. 再执行性能测试，评估性能影响
3. 根据测试结果优化代码

### 3.3 回归测试
每次代码修改后，执行关键测试用例：
- Client端断链逻辑测试
- Server端断链逻辑测试
- 水位线机制测试

## 4. 注意事项

1. **测试环境隔离**：确保测试环境不影响生产环境
2. **资源清理**：测试完成后清理所有连接和资源
3. **日志收集**：收集测试过程中的日志，便于问题分析
4. **性能基线**：建立性能基线，用于后续对比

