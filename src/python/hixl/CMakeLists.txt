# ----------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

if (NOT ENABLE_TEST)
    add_custom_target(hixl_python ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/hixl-0.0.1-py3-none-any.whl)
    add_custom_command(
            OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/hixl-0.0.1-py3-none-any.whl
            COMMAND echo "package hixl whl start"
            && mkdir -p ${CMAKE_CURRENT_BINARY_DIR}/wheel1
            && cp -r ${CMAKE_CURRENT_SOURCE_DIR}/setup.py ${CMAKE_CURRENT_BINARY_DIR}/wheel1/setup.py
            && cp -r ${CMAKE_CURRENT_SOURCE_DIR}/MANIFEST.in ${CMAKE_CURRENT_BINARY_DIR}/wheel1/
            && cp -r ${CMAKE_CURRENT_SOURCE_DIR}/hixl ${CMAKE_CURRENT_BINARY_DIR}/wheel1/
            && cp -r ${CMAKE_CURRENT_BINARY_DIR}/../hixl_wrapper/libhixl_wrapper.so ${CMAKE_CURRENT_BINARY_DIR}/wheel1/hixl/_capi/
            && cd ${CMAKE_CURRENT_BINARY_DIR}/wheel1
            && ${HI_PYTHON} setup.py bdist_wheel >/dev/null
            && cp -f dist/hixl-0.0.1-py3-none-any.whl ${CMAKE_CURRENT_BINARY_DIR}/
            && echo "package hixl whl end"
            DEPENDS hixl_wrapper
    )
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/hixl-0.0.1-py3-none-any.whl
            DESTINATION ${INSTALL_LIBRARY_DIR}
            COMPONENT hixl
    )
endif()
