# UBOE 功能测试用例总结

## 概述

本文档总结了为 commit `26f6085d0b5ed35b694523e3c8dcdac9ed7419b2` 之后的 UBOE 功能新增的测试用例。

## 新增测试文件

### 1. `tests/cpp/hixl/cs/host_register_proxy_ut.cc` (9 个测试用例)

测试 `HostRegisterProxy` 类的功能，该类负责管理 host 内存的设备注册。

#### 测试用例列表：

1. **RegisterSuccess** - 验证注册 host 内存成功
2. **UnregisterSuccess** - 验证注销注册成功
3. **UnregisterUnregisteredAddress** - 验证注销未注册地址返回成功
4. **GetRegisteredDeviceAddrSuccess** - 验证获取已注册设备地址成功
5. **GetRegisteredDeviceAddrFailOnUnregistered** - 验证获取未注册地址失败
6. **RegisterFailOnNullptr** - 验证空指针注册返回参数错误
7. **RegisterMultipleAddresses** - 验证注册多个不同地址成功
8. **RegisterSameAddressTwice** - 验证重复注册同一地址返回缓存地址
9. **RegisterOnDifferentDevices** - 验证在不同设备上注册同一地址

### 2. `tests/cpp/hixl/cs/hixl_mem_store_uboe_ut.cc` (14 个测试用例)

测试 `HixlMemStore` 类新增的 UBOE 相关功能，包括 `is_host_mem` 和 `register_dev_addr` 字段。

#### 测试用例列表：

1. **RecordHostMemoryWithDeviceAddr** - 验证记录 host 内存及其设备地址
2. **RecordDeviceMemoryWithoutDeviceAddr** - 验证记录设备内存（无设备地址）
3. **RecordMemoryWithNullptrDeviceAddr** - 验证记录 host 内存但设备地址为空
4. **FindMemoryRegionSucceeds** - 验证查找内存区域成功
5. **FindMemoryRegionWithinRange** - 验证查找区域内的地址
6. **FindMemoryRegionOutOfRange** - 验证查找区域外的地址返回错误
7. **FindMemoryRegionWithNullptrAddrReturnsInvalidParam** - 验证空指针地址返回参数错误
8. **FindMemoryRegionClientSide** - 验证客户端内存区域查找
9. **CheckRegionsContiguousWithDeviceAddr** - 验证设备地址连续的内存区域检查
10. **CheckRegionsNonContiguousWithDeviceAddr** - 验证设备地址不连续的内存区域检查
11. **CheckRegionsContiguousWithoutDeviceAddr** - 验证无设备地址时的连续性检查
12. **CheckRegionsNonContiguousAddr** - 验证地址不连续的内存区域检查
13. **MultipleRegionsWithMixedDeviceAddr** - 验证混合设备地址的多个区域
14. **FindMemoryRegionUnregistered** - 验证查找未注册的内存区域

### 3. `tests/cpp/hixl/engine/hixl_engine_uboe_unittest.cc` (11 个测试用例)

测试 `HixlEngine` 类的 UBOE 协议支持。

#### 测试用例列表：

1. **InitializeWithUboeProtocol** - 验证使用 UBOE 协议初始化
2. **InitializeWithMixedProtocols** - 验证使用混合协议（UBOE + ROCE）初始化
3. **InitializeWithEmptyEndpointList** - 验证空 endpoint_list 的处理
4. **InitializeWithProtocolDesc** - 验证带有 protocol_desc 的配置初始化
5. **RegisterHostMemoryWithUboe** - 验证 UBOE 协议下 host 内存注册
6. **RegisterDeviceMemoryWithUboe** - 验证 UBOE 协议下 device 内存注册
7. **ConnectTwoUboeEngines** - 验证两个 UBOE endpoint 的连接
8. **PreferUboeProtocolForConnection** - 验证优先使用 UBOE 协议连接
9. **RegisterAndDeregisterMemoryPaired** - 验证内存注册和注销配对
10. **RegisterSameMemoryTwice** - 验证重复注册同一内存
11. **InitializeWithGlobalResourceConfig** - 验证通过 OPTION_GLOBAL_RESOURCE_CONFIG 触发 GenDefaultUboeEndpointConfig

## 测试覆盖范围

### 功能覆盖

1. **HostRegisterProxy 类**
   - 单例模式实现
   - Host 内存注册/注销
   - 设备地址查询
   - 错误处理

2. **HixlMemStore 类**
   - Host 内存记录（带设备地址）
   - Device 内存记录
   - 内存区域查找
   - 内存区域连续性检查
   - 混合设备地址场景

3. **UBOE 协议支持**
   - 协议类型定义
   - Endpoint 配置
   - 内存注册
   - 连接管理
   - 协议优先级

### 边界条件测试

- 空指针地址
- 未注册地址
- 重复注册
- 零大小内存
- 大内存
- 地址连续性检查
- 设备地址连续性检查

### 错误处理测试

- 参数无效错误
- 未注册地址错误
- 重复注册错误
- 内存访问越界错误

## 构建系统集成

新增测试文件已添加到 `tests/cpp/hixl/CMakeLists.txt`：

```cmake
set(HIXL_TEST_FILES
        ...
        cs/hixl_mem_store_uboe_ut.cc
        cs/host_register_proxy_ut.cc
        ...
        engine/hixl_engine_uboe_unittest.cc
        ...
)
```

## 测试执行结果

```
[==========] Running 34 tests from 3 test suites.
[  PASSED  ] 34 tests.
```

所有新增测试用例均已通过，且未破坏现有测试：

```
[==========] Running 172 tests from 15 test suites ran. (7805 ms total)
[  PASSED  ] 172 tests.
```

## 关键代码变更点

本次测试覆盖了以下主要代码变更：

1. **HostRegisterProxy** (`src/hixl/cs/host_register_proxy.h`, `src/hixl/cs/host_register_proxy.cc`)
   - 新增的 host 内存设备注册代理类

2. **HixlMemStore** (`src/hixl/cs/hixl_mem_store.h`, `src/hixl/cs/hixl_mem_store.cc`)
   - `RecordMemory` 方法新增 `is_host_mem` 和 `register_dev_addr` 参数
   - 新增 `FindMemoryRegion` 方法
   - 内存区域连续性检查逻辑更新

3. **Endpoint** (`src/hixl/cs/endpoint.cc`)
   - `RegisterMem` 支持 UBOE 协议下的 host 内存设备注册
   - `DeregisterMem` 支持注销设备注册的内存

4. **HixlClient** (`src/hixl/engine/hixl_client.h`, `src/hixl/engine/hixl_client.cc`)
   - 新增 `CommType::COMM_TYPE_UBOE`
   - 新增 `TryMatchUboeEndpoints` 方法
   - 连接时优先尝试 UBOE 协议

5. **HixlEngine** (`src/hixl/engine/hixl_engine.h`, `src/hixl/engine/hixl_engine.cc`)
   - 解析 `comm_resource_config.protocol_desc` 配置
   - 生成默认 endpoint 配置
   - `ParseEndPoint` 方法支持 `endpoint_list` 不存在的场景

6. **类型定义** (`src/hixl/common/hixl_inner_types.h`)
   - 新增 `kProtocolUboe` 常量

7. **CheckOptions** (`src/hixl/common/hixl_utils.cc`)
   - 新增 `OPTION_GLOBAL_RESOURCE_CONFIG` 到允许的选项列表

8. **EngineFactory** (`src/hixl/engine/engine_factory.h`, `src/hixl/engine/engine_factory.cc`)
   - 新增 `UseUboe` 方法：检查配置中是否包含 "uboe:device"
   - 当配置了 UBOE 时强制使用 HixlEngine

## 测试设计原则

本次测试遵循以下原则：

1. **避免无意义测试**：删除了仅测试常量定义、结构体赋值等基础语言功能的测试
2. **专注业务逻辑**：重点测试内存注册、区域管理、协议匹配等核心功能
3. **边界条件覆盖**：测试各种边界条件和错误场景
4. **集成测试**：验证组件间的交互（如 Engine 层的 UBOE 支持）
5. **简化测试实现**：优先使用默认 stub 实现，只在必要时使用 mock（失败场景）
6. **测试稳定性**：避免复杂的 mock 设置，确保测试稳定可靠
7. **环境模拟**：使用临时脚本和 PATH 环境变量模拟真实命令行工具（如 hccn_tool）

## 打桩方案

### GetBondIpAddress 打桩方案

`GetBondIpAddress` 函数依赖系统命令 `hccn_tool` 获取 bond IP 地址。为在单元测试环境中模拟该功能，采用以下方案：

1. **MmpaStub 打桩**：通过 `BondIpMmpaStub` 类 mock `mmAccess` 函数，使其对 `kHccnToolPath` 返回错误，从而跳过全路径检查
2. **创建 mock hccn_tool**：`CreateHccnTool` 方法在临时目录创建可执行的 `hccn_tool` 脚本，输出指定的 IP 地址
3. **PATH 环境变量**：将临时目录添加到 PATH 前面，确保系统优先找到 mock 的 `hccn_tool`

此方案参考了 `HixlUtilsUTest` 中的 `DeviceIpMmpaStub` 和 `CreateHccnTool` 方法，实现了对命令行工具的完整模拟。

## 建议

1. **集成测试**：建议添加端到端的集成测试，验证 UBOE 协议在实际场景下的完整工作流程。

2. **性能测试**：建议添加性能基准测试，验证 UBOE 协议下的内存注册和传输性能。

3. **压力测试**：建议添加并发场景下的压力测试，验证多线程环境下的稳定性。

4. **Mock 改进**：当前的 stub 实现较为简单，建议完善 mock 行为以更准确地模拟真实场景。
