<!--
Copyright (c) 2026 Huawei Technologies Co., Ltd.
This program is free software, you can redistribute it and/or modify it under the terms and conditions of
CANN Open Software License Agreement Version 2.0 (the "License").
Please refer to the License for details. You may not use this file except in compliance with the License.
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
See LICENSE in the root of the software repository for the full text of the License.
-->

# ADXL 性能问题统计日志查看指南

本文供 **hixl-troubleshoot** 使用，面向“用户怀疑 ADXL 性能有问题”的场景，重点说明：

- 三种模式下统计日志怎么查。
- 建链耗时怎么看。
- 不同模式的传输耗时重点看哪些字段。

使用顺序建议固定为：

1. 先参考 [ADXL 三种传输模式总览](adxl-transfer-modes.md) 判断当前走的是 **HCCL直传**、**中转模式** 还是 **FabricMem模式**。
2. 再查看本文的统计日志，判断慢在 **建链** 还是 **传输**。

## 1. 怎么 grep 统计日志

统计日志由 ADXL `StatisticManager::Dump()` 周期打印，核心关键字如下：

```bash
grep -R "Connect statistic info\|Connect hccl detail\|Direct transfer statistic info\|Buffer transfer statistic info\|Fabric mem transfer statistic info" ~/ascend/log
```

如果已经知道是单个 channel 或单个对端有问题，可以继续按 channel 过滤：

```bash
grep -R "Connect statistic info\|Connect hccl detail\|Direct transfer statistic info\|Buffer transfer statistic info\|Fabric mem transfer statistic info" ~/ascend/log | grep "channel:"
```

排障时建议把同一时间窗口内的以下日志一起看：

- `Connect statistic info`
- `Connect hccl detail`
- 当前模式对应的传输统计日志

## 2. 先看建链统计

三种模式都先看建链统计，判断是不是链路建立本身就慢。

### 2.1 建链总览日志

日志模板如下：

```text
Connect statistic info[channel:<channel_id>, total times:<N>, max cost:<X> us, avg cost:<Y> us,
tcp times:<N1>, max cost:<X1> us, avg cost:<Y1> us,
hccl times:<N2>, max cost:<X2> us, avg cost:<Y2> us,
other avg cost:<Y3> us].
```

字段含义：

- `total`：整个建链流程总耗时。
- `tcp`：TCP 建 socket 的耗时。
- `hccl`：HCCL 建链阶段总耗时。
- `other`：其余耗时，按 `total - tcp - hccl` 计算出来。

阅读建议：

- `total` 高：先确认是建链本身慢，不要直接怀疑传输路径。
- `tcp` 高：优先怀疑控制面建 socket、网络连通性、对端启动时序。
- `hccl` 高：优先看 HCCL 建链细节日志。
- `other` 高：说明慢点不在 TCP 和 HCCL，更多落在 connect 握手、channel 建立、FabricMem 相关导入流程等外围阶段。

### 2.2 HCCL 细节日志

直传模式和 Buffer 模式建链时会有 HCCL 细节；FabricMem 模式通常为 0。

日志模板如下：

```text
Connect hccl detail[channel:<channel_id>, init times:<N1>, max cost:<X1> us, avg cost:<Y1> us,
bind times:<N2>, max cost:<X2> us, avg cost:<Y2> us,
prepare times:<N3>, max cost:<X3> us, avg cost:<Y3> us].
```

字段含义：

- `init`：`HcclCommInitClusterInfoMemConfig` 耗时。
- `bind`：`HcclCommBindMem` 耗时。
- `prepare`：`HcclCommPrepare` 耗时。

阅读建议：

- `init` 高：优先看 ranktable、设备信息和 HCCL comm 初始化阶段。
- `bind` 高：优先看建链前的内存注册规模、内存类型和绑定数量。
- `prepare` 高：优先看 HCCL 真正建链阶段，通常也是最容易和超时、双端时序、链路不一致问题关联的部分。

### 2.3 三种模式怎么理解建链统计

| 模式 | 建链统计看法 |
|---|---|
| HCCL直传 | 看 `total/tcp/hccl`，再结合 `Connect hccl detail` 找慢点。 |
| 中转模式 | Buffer 模式建链和直传一样，也看 `total/tcp/hccl` 与 HCCL 细节。 |
| FabricMem模式 | 重点看 `total/tcp`；`hccl` 相关通常为 0，`other` 主要反映控制面握手、channel 创建、导入共享内存等开销。 |

## 3. 再看三种模式的传输统计

建链没有明显异常后，再根据模式看传输统计。

### 3.1 HCCL 直传模式

日志模板如下：

```text
Direct transfer statistic info[channel:<channel_id>, transfer times:<N>, max cost:<X> us, avg cost:<Y> us].
```

重点字段：

- `transfer`：直传模式单次传输的整体耗时。

判断建议：

- `transfer avg cost` 高：说明慢在直传数据面本身，通常继续结合业务侧单次传输大小、批次数量、同步等待耗时一起分析。
- 如果建链统计正常、直传 `transfer` 明显偏高，优先沿 HCCL 数据面继续查。

### 3.2 中转（Buffer）模式

日志模板如下：

```text
Buffer transfer statistic info[channel:<channel_id>, transfer times:<N>, max cost:<X> us, avg cost:<Y> us,
client copy times:<N1>, max cost:<X1> us, avg cost:<Y1> us,
server comm times:<N2>, max cost:<X2> us, avg cost:<Y2> us,
server copy times:<N3>, max cost:<X3> us, avg cost:<Y3> us].
```

字段含义：

- `transfer`：Buffer 模式整体传输耗时。
- `client copy`：client 侧拷贝耗时。
- `server comm`：server 侧 D2D / 通信阶段耗时。
- `server copy`：server 侧 copy 阶段耗时。

判断建议：

- `transfer` 高：先确认整体确实慢在 Buffer 路径。
- `client copy` 高：优先看 client 侧地址转换、分批、copy 开销。
- `server comm` 高：优先看 server 侧通信阶段。
- `server copy` 高：优先看 server 侧数据搬运阶段。

### 3.3 FabricMem 模式

日志模板如下：

```text
Fabric mem transfer statistic info[channel:<channel_id>, transfer times:<N>, max cost:<X> us, avg cost:<Y> us,
real copy times:<N1>, max cost:<X1> us, avg cost:<Y1> us].
```

字段含义：

- `transfer`：FabricMem 模式整体传输耗时。
- `real copy`：真实 copy 阶段耗时。

判断建议：

- `transfer` 高、`real copy` 也高：优先怀疑 FabricMem 数据拷贝本身慢。
- `transfer` 高、但 `real copy` 不高：说明慢点更可能在地址转换、任务拆分、流同步等外围逻辑。

## 4. 典型判断套路

### 4.1 用户怀疑建链慢

固定顺序：

1. 看 `Connect statistic info`。
2. 比较 `total`、`tcp`、`hccl`。
3. 如果 `hccl` 高，再看 `Connect hccl detail`。
4. 如果是 FabricMem 且 `hccl` 为 0，则重点看 `tcp` 和 `other`。

### 4.2 用户怀疑 HCCL 直传慢

固定顺序：

1. 先确认模式是 HCCL 直传。
2. 看建链统计是否正常。
3. 再看 `Direct transfer statistic info`。
4. 如果建链正常但 `transfer avg cost` 高，问题大概率在直传数据面。

### 4.3 用户怀疑 Buffer 模式慢

固定顺序：

1. 先确认本次同步传输确实进入 Buffer 路径。
2. 看建链统计，确认不是链路建立慢。
3. 看 `Buffer transfer statistic info`。
4. 按 `transfer -> client copy -> server comm -> server copy` 的顺序定位慢点。

### 4.4 用户怀疑 FabricMem 模式慢

固定顺序：

1. 先确认模式是 FabricMem。
2. 看建链 `total/tcp/other`。
3. 看 `Fabric mem transfer statistic info`。
4. 重点比较 `transfer` 和 `real copy`，判断慢在真实 copy 还是外围流程。

## 5. 常见结论写法

当只看统计日志、还没有拿到更完整上下文时，建议结论写法保持克制：

- “从统计日志看，当前慢点主要落在建链阶段，且 HCCL prepare 占比较高。”
- “从统计日志看，当前建链没有明显异常，主要慢在 Buffer 模式的 server copy 阶段。”
- “从统计日志看，FabricMem 模式下 real copy 耗时不高，传输整体耗时主要消耗在外围流程。”

避免只凭一条统计日志直接下最终结论，最好再结合：

- 当前模式判断结果
- 首条致命错误日志
- 双端时间点
- 传输大小与调用频次

## 6. 关键日志索引

| 关键字 | 用途 |
|---|---|
| `Connect statistic info` | 看建链总览 |
| `Connect hccl detail` | 看 HCCL 建链细节 |
| `Direct transfer statistic info` | 看 HCCL 直传耗时 |
| `Buffer transfer statistic info` | 看 Buffer 模式耗时拆分 |
| `Fabric mem transfer statistic info` | 看 FabricMem 模式耗时拆分 |
