# ----------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

set(HIXL_COMM_BENCHMARK_COMMON_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/common/benchmark_config.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/common/tcp_client_server.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/common/client_runner.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/common/server_runner.cc"
)

# hixl_cs benchmarks only need the TCP helper (they drive hixl_cs C ABI directly).
set(HIXL_CS_BENCHMARK_COMMON_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/common/tcp_client_server.cpp"
)

set(HIXL_COMM_BENCHMARK_INC_DIR
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/common
    ${HIXL_INC_DIR}
    ${ASCEND_INSTALL_PATH}/include
    ${HIXL_CODE_DIR}/src/llm_datadist
    ${HIXL_CODE_DIR}/src
    ${HIXL_CODE_DIR}/third_party/json/include
)

set(HIXL_COMM_BENCHMARK_COMPILE_OPTIONS
    -ftrapv
    -O2
    -fno-common
    -Wfloat-equal
    -Wall
    -Werror
    -Wextra
)

set(HIXL_COMM_BENCHMARK_LINK_LIBRARIES
    cann_hixl
    adxl_static
    acl_rt_headers
    acl_rt
    -lpthread
    -lrt
    stdc++fs
    ascend_hal
)

# hixl_comm_bench: high-level Hixl API benchmark
add_executable(hixl_comm_bench hixl_comm_bench.cpp ${HIXL_COMM_BENCHMARK_COMMON_SOURCES})
target_compile_features(hixl_comm_bench PRIVATE cxx_std_17)
target_include_directories(hixl_comm_bench PRIVATE ${HIXL_COMM_BENCHMARK_INC_DIR})
target_compile_options(hixl_comm_bench PRIVATE ${HIXL_COMM_BENCHMARK_COMPILE_OPTIONS})
target_link_libraries(hixl_comm_bench PRIVATE ${HIXL_COMM_BENCHMARK_LINK_LIBRARIES})

# hixl_cs_* benchmarks: low-level hixl_cs C ABI benchmarks.
# Each file has its own main() and its own from_json definitions, so they
# must be compiled as separate executables.
foreach(_cs_bench_name
    hixl_cs_benchmark
    hixl_cs_benchmark_exception
    hixl_cs_benchmark_large
    hixl_cs_benchmark_mixed_role
    hixl_cs_benchmark_multi_pair
    hixl_cs_benchmark_multi_thread)
  add_executable(${_cs_bench_name}
      ${_cs_bench_name}.cpp
      ${HIXL_CS_BENCHMARK_COMMON_SOURCES})
  target_compile_features(${_cs_bench_name} PRIVATE cxx_std_17)
  target_include_directories(${_cs_bench_name} PRIVATE ${HIXL_COMM_BENCHMARK_INC_DIR})
  target_compile_options(${_cs_bench_name} PRIVATE ${HIXL_COMM_BENCHMARK_COMPILE_OPTIONS})
  target_link_libraries(${_cs_bench_name} PRIVATE ${HIXL_COMM_BENCHMARK_LINK_LIBRARIES})
endforeach()
