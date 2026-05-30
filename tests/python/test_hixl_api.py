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

import unittest

import hixl


class HixlLifecycleUt(unittest.TestCase):
    def test_construct(self):
        engine = hixl.Hixl()
        self.assertIsNotNone(engine)

    def test_initialize_finalize(self):
        engine = hixl.Hixl()
        status = engine.initialize("127.0.0.1:0")
        self.assertIsInstance(status, int)
        engine.finalize()


if __name__ == "__main__":
    unittest.main()
