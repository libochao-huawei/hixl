#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# ----------------------------------------------------------------------------
# Copyright (c) 2025 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

"""pytest configuration for hixl tests."""


class _BasicMockLib:
    """Minimal mock to allow imports."""

    def __getattr__(self, name):
        return lambda *args: 0


def pytest_configure(config):
    """Setup minimal mock library before any imports."""
    import hixl._capi.hixl_wrapper as wrapper_module

    wrapper_module.hixl_lib._lib = _BasicMockLib()
    wrapper_module.hixl_lib._loaded = True
