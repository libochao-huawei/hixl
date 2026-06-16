# HIXL Agent Development Guide

This guide provides essential information for AI agents working with the HIXL codebase.

## Build Commands

### Building the Project
```bash
# Standard build (Release mode)
sh build.sh

# Debug build
sh build.sh --build_type=Debug

# Build with examples and benchmarks
sh build.sh --examples

# Enable AddressSanitizer
sh build.sh --asan

# Enable coverage
sh build.sh --cov

# Verbose build output
sh build.sh --verbose

# Custom thread count
sh build.sh -j16
```

### Running Tests
```bash
# Run all tests (C++ and Python)
sh tests/run_test.sh

# Run only C++ tests
sh tests/run_test.sh -t cpp

# Run only Python tests
sh tests/run_test.sh -t py

# Run tests with coverage
sh tests/run_test.sh -c

# Run tests with AddressSanitizer
sh tests/run_test.sh --asan
```

### Running Single Tests
```bash
# Build tests first
cd build_test && cmake -D ENABLE_TEST=ON .. && make -j8

# Run specific test suite
./tests/cpp/hixl/hixl_test

# Run specific test case with gtest filter
./tests/cpp/hixl/hixl_test --gtest_filter=HixlServerTest.InitializePortZero

# Run all tests in a test fixture
./tests/cpp/hixl/hixl_test --gtest_filter=HixlServerTest.*
```

## Code Style Guidelines

### Formatting
- Uses `.clang-format` with Google-based style
- Line limit: 120 characters
- Indentation: 2 spaces
- Pointer alignment: Right (int* ptr)
- Only format modified code, not the entire file
- Always format with clang-format before committing

### Naming Conventions
- **Classes**: PascalCase (e.g., `Hixl`, `EndpointStore`, `HixlServer`)
- **Functions**: PascalCase for public methods (e.g., `Initialize`, `Finalize`, `RegisterMem`)
- **Variables**: snake_case (e.g., `local_engine`, `mem_handle`)
- **Member variables**: snake_case with trailing underscore (e.g., `impl_`, `mutex_`, `endpoints_`)
- **Constants**: kPascalCase (e.g., `kMemAddr1`, `kPort`)
- **Namespaces**: lowercase (e.g., `hixl`, `llm`)
- **Files**: snake_case (e.g., `endpoint_store.h`, `hixl_server.cc`)

### Include Order
1. System headers (e.g., `<memory>`, `<vector>`)
2. CANN headers (from `$HOME/Ascend/cann/include` and `$HOME/Ascend/cann/pkg_inc`)
3. Project headers (e.g., `"hixl_types.h"`, `"hixl.h"`)
4. Local headers (e.g., `"endpoint.h"`, `"hixl_inner_types.h"`)

### CANN Dependencies
- CANN headers are installed in `$HOME/Ascend/cann/include` and `$HOME/Ascend/cann/pkg_inc`
- When searching for headers, check these directories for CANN-specific dependencies
- CANN libraries and runtime are required for building and running HIXL
- Exception: `hcomm/hcomm_res_defs.h` should use the version from `src/hixl/proxy/`, not from CANN headers

### Type System
- C++17 standard
- Use `auto` for complex types when type is obvious
- Use `nullptr` instead of `NULL` or `0`
- Use `uint32_t`, `int32_t` from `<cstdint>` for fixed-width integers
- Use `std::unique_ptr` for ownership, raw pointers for non-owning references
- Use pimpl idiom for ABI stability (see `Hixl` class)

### Error Handling
- Return `Status` enum values: `SUCCESS`, `FAILED`, `PARAM_INVALID`, etc.
- Check return values and handle errors appropriately
- Use `EXPECT_EQ` and `ASSERT_EQ` in tests for status checks
- Never ignore return values without explicit reason

### Memory Management
- Use RAII patterns (smart pointers, scope guards)
- Prefer `std::unique_ptr` over raw pointers
- Use `std::make_unique` for creating unique_ptr
- Register/deregister memory pairs must be balanced
- Check for nullptr before dereferencing

### Threading
- Use `std::mutex` for synchronization
- Use `std::lock_guard` or `std::unique_lock` for scoped locking
- Member mutexes should be named with trailing underscore (e.g., `mutex_`)
- Always lock in consistent order to avoid deadlocks

### Testing
- Use Google Test framework
- Create test fixtures with `TEST_F` for shared setup
- Use `SetUp()` and `TearDown()` for fixture lifecycle
- Test names should be descriptive: `TEST_F(FixtureName, TestDescription)`
- Test both success and failure cases
- Test boundary conditions and edge cases

### Copyright Headers
All source files must include the copyright header:
```cpp
/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
```

### API Visibility
- Use `ASCEND_FUNC_VISIBILITY` macro for public API functions
- Internal implementation should be in private or separate namespace
- Pimpl pattern helps maintain ABI stability

### Project Structure
- `include/hixl/`, `include/llm_datadist/`: Public headers
- `src/hixl/`, `src/llm_datadist/`: Implementation files
- `tests/cpp/`: C++ unit tests
- `tests/python/`: Python tests
- `examples/cpp/`: Example usage
- `benchmarks/`: Performance benchmarks

### Common Patterns
- Initialize/Finalize pattern for resource management
- Register/Deregister pattern for memory registration
- Connect/Disconnect pattern for connection management
- Status return values for all operations
- Options map for configuration (std::map<AscendString, AscendString>)

### Logging
- Use `HIXL_LOGI`, `HIXL_LOGE`, `HIXL_LOGW`, `HIXL_LOGD` macros for logging
- Format: `HIXL_LOGI("[ClassName] Message with params: %s", param.c_str())`
- Use descriptive context in log messages (e.g., "[HixlEngine] Initialization started")

### When Making Changes
1. Format code with clang-format
2. Add copyright header to new files
3. Write tests for new functionality
4. Run tests before committing
5. Check for memory leaks with AddressSanitizer
6. Ensure backward compatibility for public APIs
7. IMPORTANT: Only modify code directly related to the task. Do not modify unrelated code, even if formatting is incorrect

### Making Precise Changes
When modifying code, follow these guidelines to avoid unintended changes:

1. **Use Edit tool carefully**:
   - Ensure `oldString` and `newString` contain only the exact code that needs to be changed
   - Include sufficient context (surrounding lines) to make the match unique
   - Never include unrelated code in the replacement

2. **Check changes immediately**:
   - After each modification, run `git diff` to verify what was changed
   - If unrelated changes are detected, immediately restore the file and redo the modification
   - Use `git diff --stat` to see summary of changes

3. **Use precise sed commands**:
   - When using sed, specify exact line numbers instead of global replacement
   - Example: `sed -i '225s/old/new/' file.cc` (only line 225)
   - Avoid: `sed -i 's/old/new/g' file.cc` (all occurrences)
   - Use `sed -i '225,230d' file.cc` for precise line deletion

4. **For simple replacements**:
   - Use line-number based replacement: `sed -i 'line_number s/old/new/' file`
   - Verify with `git diff` after each replacement

5. **Batch operations**:
   - When making multiple similar changes, use a single sed command with multiple patterns
   - Or use a loop with exact line numbers
   - Always verify the final result with `git diff`

6. **Avoid format changes**:
   - If the editor auto-formats code, restore and use more precise methods
   - Use line-based edits instead of block-based edits to minimize format changes
   - Never rely on auto-formatting to "fix" unrelated code

### Additional Notes
- This is a CANN (Compute Architecture for Neural Networks) project for Huawei Ascend
- CANN headers are located in `$HOME/Ascend/cann/include` and `$HOME/Ascend/cann/pkg_inc`
- The project uses both .cpp and .cc file extensions for C++ source files
- Exception: `hcomm/hcomm_res_defs.h` should use the version from `src/hixl/proxy/`, not from CANN headers
