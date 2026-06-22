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

from setuptools import setup, Distribution

class BinaryDistribution(Distribution):
    """Distribution that always has a .so to install."""
    def has_ext_modules(self):
        return True

setup(
    name='hixl_engine',
    version='0.0.1',
    description='hixl engine api',
    packages=[],               # ← 没有 Python 包
    ext_modules=[],            # ← .so 由 CMake 外部提供
    distclass=BinaryDistribution,
    package_data={'': ['*.so']},  # ← 把 wheel1/ 里的 .so 当 data 打入
)
