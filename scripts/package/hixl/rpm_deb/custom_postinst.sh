#!/bin/bash
# -----------------------------------------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# -----------------------------------------------------------------------------------------------------------

sourcedir="${INSTALL_PATH}"
WHL_INSTALL_DIR_PATH="${sourcedir}/python/site-packages"
unset PYTHONPATH
export PIP_BREAK_SYSTEM_PACKAGES=1

WHL_LLM_DATADIST="${sourcedir}/hixl/lib/llm_datadist-0.0.1-py3-none-any.whl"

run_pip() { python3 -m pip "$@" || pip3 "$@"; }

set_perms() {
    local path="$1"
    local type="$2"
    local mode="$3"
    find "${path}" -type "${type}" -exec chmod "${mode}" {} \; 2>/dev/null || true
}

[ -f "${WHL_LLM_DATADIST}" ] && echo "[hixl] installing ${WHL_LLM_DATADIST}" && run_pip install --disable-pip-version-check --upgrade --no-deps --force-reinstall -t "${WHL_INSTALL_DIR_PATH}" "${WHL_LLM_DATADIST}" || true

if [ -d "${WHL_INSTALL_DIR_PATH}" ]; then
    set_perms "${sourcedir}/python" d 750
    set_perms "${WHL_INSTALL_DIR_PATH}/llm_datadist" d 550
    set_perms "${WHL_INSTALL_DIR_PATH}/llm_datadist" f 440
    set_perms "${WHL_INSTALL_DIR_PATH}/llm_datadist-"*.dist-info d 550
    set_perms "${WHL_INSTALL_DIR_PATH}/llm_datadist-"*.dist-info f 440
    set_perms "${WHL_INSTALL_DIR_PATH}/LICENSE" f 440
fi

 rm -rf "${sourcedir}/hixl" 2>/dev/null || true
