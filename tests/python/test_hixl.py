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

from hixl import Hixl, HixlException
from hixl.types import (
    Status,
    OPTION_BUFFER_POOL,
)
from hixl.exception import check_status, status_to_string


class TestHixlLifecycle(unittest.TestCase):
    """Test Hixl create/destroy and initialize/finalize."""

    def setUp(self):
        print("Begin ", self._testMethodName)
        self._hixl_instances = []

    def tearDown(self):
        print("End ", self._testMethodName)
        for hixl in self._hixl_instances:
            if hixl._handle is not None:
                from hixl._capi.hixl_wrapper import hixl_lib

                hixl_lib.HixlDestroy(hixl._handle)
                hixl._handle = None

    def test_create_destroy(self):
        hixl = Hixl()
        self._hixl_instances.append(hixl)
        self.assertIsNotNone(hixl._handle)
        self.assertEqual(hixl._is_initialized, False)

    def test_initialize_without_options(self):
        hixl = Hixl()
        self._hixl_instances.append(hixl)
        hixl.initialize("127.0.0.1:26000")
        self.assertEqual(hixl._is_initialized, True)

    def test_initialize_with_options(self):
        hixl = Hixl()
        self._hixl_instances.append(hixl)
        options = {
            OPTION_BUFFER_POOL: "0:0",
        }
        hixl.initialize("127.0.0.1:26001", options)
        self.assertEqual(hixl._is_initialized, True)


class TestExceptionHandling(unittest.TestCase):
    """Test HixlException and check_status."""

    def setUp(self):
        print("Begin ", self._testMethodName)

    def tearDown(self):
        print("End ", self._testMethodName)

    def test_exception_creation(self):
        exc = HixlException(Status.PARAM_INVALID, "Test error")
        self.assertEqual(exc.status, Status.PARAM_INVALID)
        self.assertEqual(exc.message, "Test error")
        self.assertIn("[Status 103900]", str(exc))
        self.assertIn("Test error", str(exc))

    def test_exception_with_custom_status(self):
        exc = HixlException(999, "Custom error")
        self.assertEqual(exc.status, 999)
        self.assertIn("Custom error", str(exc))

    def test_check_status_success(self):
        check_status(Status.SUCCESS, "Should not raise")

    def test_check_status_failure_raises(self):
        with self.assertRaises(HixlException) as exc_info:
            check_status(Status.PARAM_INVALID, "Param error")
        self.assertEqual(exc_info.exception.status, Status.PARAM_INVALID)

    def test_check_status_without_context(self):
        with self.assertRaises(HixlException) as exc_info:
            check_status(Status.FAILED)
        self.assertEqual(exc_info.exception.message, "HIXL operation failed")

    def test_status_to_string_known(self):
        self.assertEqual(status_to_string(Status.SUCCESS), "SUCCESS")
        self.assertEqual(status_to_string(Status.PARAM_INVALID), "PARAM_INVALID")

    def test_status_to_string_unknown(self):
        self.assertEqual(status_to_string(999999), "UNKNOWN(999999)")
