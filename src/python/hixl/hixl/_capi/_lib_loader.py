#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# ----------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

"""Internal utility for loading shared libraries with fallback paths."""

import ctypes
import os
from typing import Optional


def load_lib_from_path(
    lib_name: str, lib_dir: str, mode: Optional[int] = None
) -> ctypes.CDLL:
    """Load library from file system with fallback search paths.

    Args:
        lib_name: Library name or absolute path.
        lib_dir: Directory to search for the library if lib_name is not absolute.
        mode: Optional dlopen mode flags (e.g., os.RTLD_GLOBAL | os.RTLD_NOW).

    Returns:
        Loaded ctypes.CDLL object.

    Raises:
        OSError: If library cannot be loaded from any candidate path.
    """
    candidates = (
        [lib_name]
        if os.path.isabs(lib_name)
        else [os.path.join(lib_dir, lib_name), lib_name]
    )

    errors = []
    for path in candidates:
        try:
            if mode is None:
                return ctypes.CDLL(path)
            return ctypes.CDLL(path, mode=mode)
        except OSError as exc:
            errors.append(f"try to load {path} failed: {exc}")

    raise OSError("\n".join(errors))
