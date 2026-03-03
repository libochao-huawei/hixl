# AGENTS.md

本文件为在 HIXL 仓库中工作的 AI 编码助手提供指导。

## 构建、代码检查和测试命令

### 构建
```bash
# 标准构建（Release 模式）
./build.sh

# Debug 构建
./build.sh --build_type=Debug

# 构建示例和基准测试
./build.sh --examples

# 使用 AddressSanitizer 构建
./build.sh --asan

# 使用覆盖率构建
./build.sh --cov

# 自定义线程数
./build.sh -j16

# 详细输出
./build.sh -v
```

### 测试
```bash
# 运行所有测试（C++ 和 Python）
./tests/run_test.sh

# 仅运行 C++ 测试
./tests/run_test.sh -t=cpp

# 仅运行 Python 测试
./tests/run_test.sh -t=py

# 运行带覆盖率的测试（需要 lcov, gcov, genhtml）
./tests/run_test.sh -c

# 使用 AddressSanitizer 运行测试
./tests/run_test.sh --asan

# 详细测试输出
./tests/run_test.sh -v
```

### 运行单个测试
```bash
# 运行特定的 C++ 测试
./build_test/tests/cpp/llm_datadist/llm_datadist_test --gtest_filter=TestName.TestCaseName

# 运行特定的 Python 测试
cd build_test && python -m unittest tests.python.test_module.TestClass.test_method
```

## 代码风格指南

### C++ 代码风格

**格式化**：使用 `.clang-format`（基于 Google 风格）
- 列限制：120 字符
- 缩进：2 个空格（不使用制表符）
- 指针对齐：右对齐（`int* ptr`）
- 大括号风格：附加（`if (condition) {`）
- Include 块：保持顺序（不排序）

**命名约定**：
- 类：`PascalCase`（例如：`HixlEngine`, `CacheManager`）
- 函数/方法：`PascalCase`（例如：`Initialize`, `RegisterMem`）
- 变量：`snake_case`（例如：`mem_handle`, `is_initialized`）
- 成员变量：`snake_case_` 带尾随下划线（例如：`is_initialized_`）
- 常量：`kPascalCase` 或 `ALL_CAPS`（例如：`kMaxRetries`, `MAX_SIZE`）
- 宏：`ALL_CAPS`（例如：`MAX_CLUSTER_NAME`）
- 命名空间：`lower_case`（例如：`hixl`, `llm_datadist`）

**类型系统**：
- C++17 标准
- 谨慎使用 `auto`，优先使用显式类型以保持清晰
- 使用 `std::unique_ptr` 表示所有权，`std::shared_ptr` 表示共享所有权
- 使用 `const` 引用作为只读参数
- 使用 `std::optional` 表示可选返回值

**导入/Include**：
- 保持现有的 include 顺序（SortIncludes: false）
- 分组 include：系统头文件、第三方库、本地头文件
- 在头文件中尽可能使用前向声明

**错误处理**：
- 从函数返回 `Status` 对象（不使用异常）
- 检查返回值并传播错误
- 使用 `Status::SUCCESS` 表示成功，其他值表示失败
- 在返回错误状态前适当记录错误

**文档注释**：
- 公共 API 使用 Doxygen 风格注释
- 实现中允许使用中文注释
- 包含参数描述（`@param [in]`, `[out]`）
- 包含返回值描述（`@return`）

### Python 代码风格

**格式化**：
- 遵循 PEP 8 规范
- 行长度：120 字符（与 C++ 匹配）
- 使用类型提示作为函数签名
- 使用 `from typing import` 进行类型注解

**命名约定**：
- 类：`PascalCase`（例如：`LLMDataDist`, `CacheManager`）
- 函数/方法：`snake_case`（例如：`initialize`, `register_mem`）
- 变量：`snake_case`（例如：`mem_handle`, `is_initialized`）
- 常量：`ALL_CAPS`（例如：`MAX_CLUSTER_NAME`, `_MAX_NODE_NUM`）
- 私有成员：`_leading_underscore`（例如：`_cache_manager`）

**类型系统**：
- 一致使用类型提示
- 从 `typing` 模块导入类型
- 使用 `Optional[T]` 表示可空类型
- 使用 `Union[T1, T2]` 表示多种可能类型
- 使用 `List[T]`, `Dict[K, V]`, `Tuple[T1, T2]` 表示集合

**错误处理**：
- 使用异常进行错误处理
- 定义自定义异常类（例如：`LLMException`）
- 使用 `raise_if_false`, `raise_if_true` 辅助函数进行验证
- 提供有意义的错误消息

**导入**：
- 标准库导入在前
- 第三方库导入在中间
- 本地导入在后
- 在包内使用绝对导入

**文档**：
- 为类和公共方法使用文档字符串
- 描述参数、返回值和异常
- 实现中允许使用中文注释

### 文件组织

**目录结构**：
- `include/`：公共头文件（hixl/, adxl/, llm_datadist/）
- `src/`：实现文件（hixl/, llm_datadist/, python/）
- `tests/`：测试文件（cpp/, python/）
- `examples/`：示例代码（cpp/, python/）
- `benchmarks/`：性能基准测试
- `cmake/`：CMake 构建配置

**文件命名**：
- C++ 头文件：`.h` 扩展名（例如：`hixl_engine.h`）
- C++ 源文件：`.cpp` 扩展名（例如：`hixl_engine.cpp`）
- Python 模块：`.py` 扩展名（例如：`llm_datadist.py`）
- 测试文件：`*_test.cpp` 或 `test_*.py`

### 测试指南

**C++ 测试**：
- 使用 Google Test 框架
- 测试文件命名：`*_unittest.cpp` 或 `*_test.cpp`
- 使用 `TEST(TestSuite, TestCase)` 宏
- 使用 gmock 模拟外部依赖

**Python 测试**：
- 使用 `unittest` 框架
- 测试文件命名：`test_*.py`
- 继承自 `unittest.TestCase`
- 使用 `setUp()` 和 `tearDown()` 作为测试固件

### 许可证头

所有源文件必须在顶部包含 CANN 开源软件许可证头：

**版权年份规则**：
- 如果文件只创建于某一年：使用单一年份（如 `2025`）
- 如果文件创建于某年，后续年份有修改：使用年份范围（如 `2025-2026`）
- 需要根据 git 历史或文件实际修改情况确定正确的年份范围

```cpp
/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025-2026 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR
FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
```

### 其他说明

- 线程安全：适当使用 `std::mutex` 和 `std::atomic`
- 内存管理：优先使用 RAII 模式，避免使用原始指针
- 日志记录：使用项目特定的日志工具（例如：`hixixl_log.h`, `llm_log.h`）
- 兼容性：支持 IPv4 和 IPv6 地址
- 性能：优化零拷贝操作和高吞吐量传输
