# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.

import importlib.util
from pathlib import Path

_MODULE_PATH = (
    Path(__file__).resolve().parents[2] / "scripts" / "kernel" / "parser_ini.py"
)
_SPEC = importlib.util.spec_from_file_location("hixl_parser_ini", _MODULE_PATH)
_MODULE = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(_MODULE)


def test_classify_paths_requires_complete_extensions():
    ini_paths, output = _MODULE._classify_paths(
        ["kernel.mini", "ops.INI", "report.JSON"]
    )

    assert ini_paths == ["ops.INI"]
    assert output == "report.JSON"


def test_classify_paths_ignores_unrelated_suffixes():
    ini_paths, output = _MODULE._classify_paths(
        ["kernel.ini.backup", "report.json.tmp"]
    )

    assert ini_paths == []
    assert output == "tf_kernel.json"
