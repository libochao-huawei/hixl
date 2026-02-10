# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Overview

HIXL (Huawei Xfer Library) is a high-performance, one-sided zero-copy communication library for Huawei Ascend AI processors. It provides low-latency, high-bandwidth data transfer capabilities for distributed AI workloads, supporting multiple transport protocols (RDMA, HCCS) and memory types (D2D, D2H, H2D). The project includes:

- **HIXL Engine**: Core transfer engine with minimalist C++ API (~10 core functions)
- **LLM-DataDist**: Higher-level API with KV Cache semantics for LLM inference engines (vLLM, SGLang)
- **Python bindings**: Full Python interface for integration with AI frameworks
- **Cross-platform support**: Works on both x86_64 and aarch64 architectures

## Build System

The project uses **CMake** (3.16+) with **C++17**. Key build options are controlled via `build.sh` script:

```bash
# Basic build (Release mode, 8 threads)
bash build.sh

# Build with examples and benchmarks
bash build.sh --examples

# Debug build with AddressSanitizer
bash build.sh --build_type=Debug --asan

# Build with coverage instrumentation
bash build.sh --cov

# Specify third-party dependencies path (offline builds)
bash build.sh --cann_3rd_lib_path=/path/to/third_party

# Show all options
bash build.sh -h
```

**Output**: Build artifacts are placed in `build_out/` directory, including `cann-hixl_${version}_linux-${arch}.run` installable package.

## Common Development Commands

### Building
```bash
# Standard build
bash build.sh

# Debug build with tests enabled (via separate test script)
bash tests/run_test.sh --test=cpp  # Builds and runs C++ tests
```

### Testing
```bash
# Run all tests (C++ and Python)
bash tests/run_test.sh

# Run only C++ tests
bash tests/run_test.sh --test=cpp

# Run only Python tests
bash tests/run_test.sh --test=py

# Run tests with coverage reporting (requires lcov, gcov, genhtml)
bash tests/run_test.sh --cov

# Run with AddressSanitizer for memory leak detection
bash tests/run_test.sh --asan
```

### Code Formatting
The project uses **clang-format** with custom style defined in `.clang-format` (120 column limit, 2-space indentation, Google-based style). To format C++ code:

```bash
# Check formatting (manual)
find src include -name "*.h" -o -name "*.cpp" | xargs clang-format --dry-run --Werror

# Apply formatting
find src include -name "*.h" -o -name "*.cpp" | xargs clang-format -i
```

### Installation
After building, install the generated package:
```bash
./build_out/cann-hixl_${version}_linux-${arch}.run --full --quiet --pylocal
```

## Architecture

### Core Components
1. **HIXL Engine** (`src/hixl/`, `include/hixl/`)
   - Low-level transport layer supporting multiple protocols (HCCS, RDMA)
   - Manages memory registration, connection establishment, and data transfer
   - Implements one-sided zero-copy communication
   - Primary class: `hixl::Hixl` with PIMPL pattern

2. **LLM-DataDist** (`src/llm_datadist/`, `include/llm_datadist/`)
   - Higher-level API with KV Cache semantics for LLM inference
   - Built on top of HIXL Engine
   - Provides specialized interfaces for AI framework integration

3. **Python Bindings** (`src/python/`)
   - PyBind11-based wrappers for both HIXL and LLM-DataDist
   - Enables integration with Python AI ecosystems

### Directory Structure
```
├── src/
│   ├── hixl/          # HIXL Engine core implementation (C++)
│   ├── llm_datadist/  # LLM-DataDist layer (C++)
│   └── python/        # Python bindings and wrapper code
├── include/
│   ├── hixl/          # HIXL Engine public headers
│   ├── adxl/          # ADXL (Ascend Data Transfer Library) headers
│   └── llm_datadist/  # LLM-DataDist public headers
├── tests/             # Test suites (C++ and Python)
├── examples/          # Usage examples (C++ and Python)
├── benchmarks/        # Performance benchmarks
├── docs/              # Documentation
└── cmake/             # CMake build system configuration
```

### Key Dependencies
- **CANN Toolkit**: Huawei Ascend Compute Architecture for Neural Networks (required)
- **Third-party**: googletest (1.14.0), json (3.11.3), pybind11 (2.13.6), makeself (2.5.0)
- **Python**: pyyaml, numpy, scipy, protobuf (see `requirements.txt`)

## Development Notes

### Environment Setup
1. **CANN Installation**: Must install CANN toolkit (community edition) and set environment variables:
   ```bash
   source /usr/local/Ascend/cann/set_env.sh  # Default path
   ```

2. **Python Versions**: Supported Python versions: 3.9, 3.11, 3.12

3. **Docker Builds**: Pre-configured Docker images available for x86 and ARM architectures:
   - `swr.cn-north-4.myhuaweicloud.com/ci_cann/ubuntu20.04.05_x86:lv4_latest`
   - `swr.cn-north-4.myhuaweicloud.com/ci_cann/ubuntu20.04.05_arm:lv4_latest`

### Testing Strategy
- **C++ Tests**: GoogleTest-based, located in `tests/cpp/`
- **Python Tests**: unittest-based, located in `tests/python/`
- **Coverage**: Use `--cov` flag for both C++ (lcov) and Python (coverage.py) coverage reports
- **Memory Checking**: AddressSanitizer enabled via `--asan` flag

### Performance Considerations
- The library achieves up to 119GB/s bandwidth on Ascend A3 chips with HCCS protocol
- Small batch transfers are optimized for latency
- Supports both synchronous (`TransferSync`) and asynchronous (`TransferAsync`) transfers

### Integration Points
- **AI Frameworks**: vLLM, SGLang, Mooncake, DeepLink
- **Transport Protocols**: RDMA, HCCS (hardware-accelerated)
- **Memory Types**: Device-to-Device (D2D), Device-to-Host (D2H), Host-to-Device (H2D)

## References
- [README.md](README.md) - Project overview and performance data
- [docs/build.md](docs/build.md) - Detailed build instructions
- [docs/cpp/README.md](docs/cpp/README.md) - C++ API documentation
- [docs/python/README.md](docs/python/README.md) - Python API documentation
- [CONTRIBUTING.md](CONTRIBUTING.md) - Contribution guidelines