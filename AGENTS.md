# AGENT.md

本文件为 Agent 在本仓库中工作提供指引。

## 项目概述

HIXL（Huawei Xfer Library）是面向昇腾芯片的单边通信库，用于分布式 AI 场景（LLM 推理 KV Cache 传输、PD 分离等）。支持通过 HCCS 和 RDMA 协议进行点对点 D2D/D2H/H2D 数据传输，并通过 pybind11 提供 Python 绑定。

两个核心组件：
- **HIXL Engine**（`src/hixl/`）：底层传输引擎，支持多种内存类型和传输协议。
- **LLM-DataDist**（`src/llm_datadist/`）：基于 HIXL 构建的上层 KV Cache 传输接口，对接 vLLM/SGLang。

## 构建与测试命令

前置条件：已安装 Ascend CANN toolkit，且已执行 `source /usr/local/Ascend/cann/set_env.sh`。

```bash
# 构建
bash build.sh                              # Release 构建，输出到 build_out/
bash build.sh --build-type=Debug --asan    # Debug 构建并开启 AddressSanitizer
bash build.sh --examples                   # 同时编译示例和 benchmark

# 测试
bash tests/run_test.sh                     # 执行全部 C++ 和 Python 测试
bash tests/run_test.sh -t cpp              # 仅执行 C++ 测试
bash tests/run_test.sh -t py               # 仅执行 Python 测试
bash tests/run_test.sh --cov               # 按覆盖率模式构建并运行测试

# 代码检查
pre-commit run                 # clang-format、ruff、codespell、OAT 合规检查
pip3 install -r requirements.txt           # 安装 Python 依赖
```

## 代码风格与命名约定

- **C++**：`.clang-format`（基于 Google 风格）— 2 空格缩进、120 列限制、附着式大括号、`SortIncludes: false`。类型用 `PascalCase`，函数用 `camelCase`。
- **Python**：`ruff-check` + `ruff-format`，模块和测试文件用 `snake_case`。
- 测试文件命名：C++ 以 `_unittest.cc` 或 `_ut.cc` 结尾，Python 以 `test_*.py` 命名。

## 测试要求

- 行为变更必须补齐或更新测试。
- C++ 用例按模块放置，例如 `tests/cpp/hixl/engine/`、`tests/cpp/llm_datadist/`。
- Python 用例放在 `tests/python/test_*.py`。

## 提交规范

使用类型前缀：`[feat]`、`[bugfix]`、`[refactor]`、`[docs]`，搭配祈使句摘要。
示例：`[bugfix] fix channel cleanup race`

## 关键约束

- 构建依赖 Ascend CANN >= 9.0.0，运行依赖 >= 8.5。未 source CANN 环境变量将无法构建。
- 禁止提交 `build_out/`、`build_test/` 或手动下载的第三方压缩包。
- 始终使用 `bash build.sh` / `bash tests/run_test.sh`，不要手写零散 CMake 命令。
- OAT（开源审计工具）合规通过 pre-commit hook 强制执行，新文件需要添加正确的许可证头。
