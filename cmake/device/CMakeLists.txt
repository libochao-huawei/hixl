# -----------------------------------------------------------------------------------------------------------
# Copyright (c) 2025 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# -----------------------------------------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.16)
project (device_hixl_ops)

set(PRODUCT_SIDE device)
include(../function.cmake)

set(CMAKE_CXX_STANDARD 17)
set(HIXL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../" CACHE PATH "Project top directory")
set(TARGET_SYSTEM_NAME Linux)

set(STUBS
    slog
    ccl_kernel
)
foreach(STUB ${STUBS})
    if(NOT TARGET ${STUB})
        generate_stub(${STUB})
    endif()
endforeach()

if(DEFINED ENV{ASCEND_HOME_PATH})
    set(ASCEND_CANN_PACKAGE_PATH  $ENV{ASCEND_HOME_PATH})
elseif(DEFINED ENV{ASCEND_OPP_PATH})
    get_filename_component(ASCEND_CANN_PACKAGE_PATH "$ENV{ASCEND_OPP_PATH}/.." ABSOLUTE)
else()
    set(ASCEND_CANN_PACKAGE_PATH  "/usr/local/Ascend/ascend-toolkit/latest")
endif()

add_subdirectory(${HIXL_DIR}/src/ops ${CMAKE_BINARY_DIR}/src/ops)
pack_targets_and_files(
    OUTPUT_TARGET "pack_aicpu_hixl"
    OUTPUT aicpu_hixl.tar.gz
    FILES ""
    TARGETS hixl_kernel
)

sign_file(
    INPUT "aicpu_hixl.tar.gz"
    CONFIG "${HIXL_DIR}/scripts/sign/hixl_check_cfg.xml"
    RESULT_VAR "aicpu_hixl_sign_file"
    DEPENDS pack_aicpu_hixl
)

install(FILES
  ${aicpu_hixl_sign_file}
  DESTINATION hixl/aicpu_kernel
)