# ----------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

add_library(base_headers INTERFACE)
target_include_directories(base_headers INTERFACE
        ${ASCEND_CANN_PACKAGE_PATH}/${AARCH_PREFIX}-linux/pkg_inc/base
)

add_library(cann_headers INTERFACE)
target_include_directories(cann_headers INTERFACE
        ${ASCEND_CANN_PACKAGE_PATH}/${AARCH_PREFIX}-linux/include
        ${ASCEND_CANN_PACKAGE_PATH}/${AARCH_PREFIX}-linux/include/hccl
)

add_library(compile_options INTERFACE)
target_compile_options(compile_options INTERFACE
        $<$<CONFIG:Debug>:-g>
        $<$<CONFIG:Debug>:-O0>
        $<$<CONFIG:Release>:-O3>
        -fstack-protector-all
)

add_library(link_options INTERFACE)
target_link_options(link_options INTERFACE
        -Wl,-z,relro
        -Wl,-z,now
        -Wl,-z,noexecstack
        $<$<CONFIG:Release>:-s>
)

set(INCLUDE_LIST
        ${HIXL_DIR}/src/hixl
        ${HIXL_DIR}/include
)

add_library(hixl_kernel SHARED
        hixl_batch_transfer.cc
)

target_include_directories(hixl_kernel PRIVATE
        ${INCLUDE_LIST}
        $<BUILD_INTERFACE:${base_headers}>
        $<BUILD_INTERFACE:${cann_headers}>
)

target_compile_options(hixl_kernel PRIVATE
        $<BUILD_INTERFACE:${compile_options}>
)

target_link_options(hixl_kernel PRIVATE
        $<BUILD_INTERFACE:${link_options}>
)

target_compile_definitions(hixl_kernel PRIVATE
        -DAICPU_COMPILE
)

target_link_libraries(hixl_kernel PRIVATE
        -Wl,--no-as-needed
        ccl_kernel
        slog
        -Wl,--no-as-needed
)

# 基于ini生成json文件
message(STATUS "HIXL_DIR = ${HIXL_DIR}")
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/libcann_hixl_kernel.json
        COMMAND python3 ${HIXL_DIR}/scripts/kernel/parser_ini.py ${HIXL_DIR}/src/ops/libcann_hixl_kernel.ini ${CMAKE_CURRENT_BINARY_DIR}/libcann_hixl_kernel.json
)
add_custom_target(aicpu_kernel_json DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/libcann_hixl_kernel.json)
add_dependencies(hixl_kernel aicpu_kernel_json)

install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/libcann_hixl_kernel.json
        DESTINATION hixl/aicpu_kernel OPTIONAL
)
