# ----------------------------------------------------------------------------
# Copyright (c) 2025 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

if (NOT ENABLE_TEST)
    set(CMAKE_SKIP_RPATH TRUE)
    add_library(hixl_wrapper MODULE
        hixl_wrapper.cc)

    target_include_directories(hixl_wrapper PRIVATE
        ${HIXL_CODE_DIR}/include
        ${HIXL_CODE_DIR}/src/hixl
        ${CANN_INSTALL_PATH}/include
        ${CANN_INSTALL_PATH}/include/external
        $<BUILD_INTERFACE:intf_pub>
    )

    target_link_libraries(hixl_wrapper PRIVATE
        cann_hixl
        $<BUILD_INTERFACE:intf_pub>
    )

    set_target_properties(hixl_wrapper PROPERTIES
        PREFIX ""
        OUTPUT_NAME "libhixl_wrapper"
    )

    target_compile_options(hixl_wrapper PRIVATE
        -Xlinker -export-dynamic
    )

    target_link_options(hixl_wrapper PRIVATE
        -s
    )
endif()
