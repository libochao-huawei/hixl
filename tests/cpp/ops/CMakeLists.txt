# ----------------------------------------------------------------------------
# This program is free software, you can redistribute it and/or modify it.
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This file is a part of the CANN Open Software.
# Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

set(HIXL_TEST_FILES
        hixl_kernel/hixl_kernel_basic_unittest.cc
)

file(GLOB HIXL_SRC_LIST
        "${HIXL_CODE_DIR}/src/ops/hixl_kernel/*.cc"
)

add_executable(ops_kernel_test
        ${HIXL_TEST_FILES}
        ${HIXL_SRC_LIST}
        )

target_compile_options(ops_kernel_test PRIVATE
        ${DT_COMMON_COMPILE_OPTION}
        -Wno-deprecated-declarations
        -Wall -Wfloat-equal
        )

target_include_directories(ops_kernel_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${HIXL_CODE_DIR}/include
        ${HIXL_CODE_DIR}/src/hixl
        ${HIXL_CODE_DIR}/src/llm_datadist
        ${HIXL_CODE_DIR}/src/hixl/cs
        ${HIXL_CODE_DIR}/src/hixl/engine
        ${HIXL_CODE_DIR}/src/ops
        ${HIXL_CODE_DIR}/tests
        ${HIXL_CODE_DIR}/tests/depends/ascendcl/src
        )

set_target_properties(ops_kernel_test PROPERTIES CXX_STANDARD 17)

target_link_libraries(ops_kernel_test
        ${COMMON_LINK_OPTION}
        c_sec
        mmpa_headers
        hccl_headers
        runtime_headers
        metadef_headers
        intf_pub
        json
        -ldl
        slog_stub
        mmpa_stub
        ascendcl_stub
        runtime_stub
        msprof_headers
        GTest::gtest GTest::gtest_main
        GTest::gmock
        -Wl,--no-as-needed
        hccl_stub
        error_manager_stub
        -Wl,--as-needed)

include(CTest)
enable_testing()