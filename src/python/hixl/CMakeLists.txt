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
    add_library(pybind11::headers IMPORTED INTERFACE)
    set_target_properties(pybind11::headers PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${pybind11_INCLUDE_DIR}")
    include(${CANN_3RD_LIB_PATH}/pybind11/tools/pybind11Common.cmake)

    set(CMAKE_SKIP_RPATH TRUE)
    pybind11_add_module(hixl hixl_py.cpp)
    set_target_properties(hixl PROPERTIES SUFFIX ".so")

    target_include_directories(hixl PRIVATE
            ${HIXL_CODE_DIR}/include
    )

    target_link_libraries(hixl PRIVATE
            $<BUILD_INTERFACE:intf_pub>
            $<BUILD_INTERFACE:hccl_headers>
            $<BUILD_INTERFACE:slog_headers>
            $<BUILD_INTERFACE:mmpa_headers>
            $<BUILD_INTERFACE:metadef_headers>
            $<BUILD_INTERFACE:msprof_headers>
            cann_hixl
            alog
    )

    target_compile_definitions(hixl PRIVATE
            PYBIND11_NO_ASSERT_GIL_HELD_INCREF_DECREF
    )

    target_link_options(hixl PRIVATE
            -s
    )

    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/hixl.so
            DESTINATION ${INSTALL_LIBRARY_DIR}
            COMPONENT hixl
    )
endif ()
