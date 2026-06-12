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
pkg_arch_name=$(uname -m)
WHL_INSTALL_DIR_PATH="${sourcedir}/python/site-packages"
unset PYTHONPATH
export PIP_BREAK_SYSTEM_PACKAGES=1

WHL_LLM_DATADIST="${sourcedir}/${pkg_arch_name}-linux/lib64/llm_datadist-0.0.1-py3-none-any.whl"

run_pip() { python3 -m pip "$@" || pip3 "$@"; }

[ -f "${WHL_LLM_DATADIST}" ] && echo "[hixl] installing ${WHL_LLM_DATADIST}" && run_pip install --disable-pip-version-check --upgrade --no-deps --force-reinstall -t "${WHL_INSTALL_DIR_PATH}" "${WHL_LLM_DATADIST}" || true

exit 0
