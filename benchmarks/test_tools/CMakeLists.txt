# ----------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

add_executable(test_generate_local_comm_res test_generate_local_comm_res.cc)

target_compile_features(test_generate_local_comm_res PRIVATE cxx_std_17)

target_include_directories(test_generate_local_comm_res PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${HIXL_INC_DIR}
    ${HIXL_CODE_DIR}/src/hixl
    ${HIXL_CODE_DIR}/src/hixl/engine
    ${HIXL_CODE_DIR}/src/hixl/proxy
    ${HIXL_CODE_DIR}/src/hixl/common
    ${HIXL_CODE_DIR}/src/llm_datadist
    ${CANN_INSTALL_PATH}/include
)

target_compile_options(test_generate_local_comm_res PRIVATE
    -ftrapv
    -O2
    -fno-common
    -Wfloat-equal
    -Wall
    -Werror
    -Wextra
)

target_link_libraries(test_generate_local_comm_res PRIVATE
    cann_hixl
    adxl_static
    acl_rt_headers
    acl_rt
    -lpthread
    -lrt
    stdc++fs
)

target_link_ascend_hal(test_generate_local_comm_res)
