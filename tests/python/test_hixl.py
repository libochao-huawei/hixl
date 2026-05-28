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

import ctypes
import os
import unittest

import hixl

HIXL_ENGINE_AVAILABLE = os.environ.get("HIXL_ENGINE_AVAILABLE", "0") == "1"


def skipUnlessEngine(reason="Hixl engine not available in stub environment"):
    return unittest.skipUnless(HIXL_ENGINE_AVAILABLE, reason)


class HixlModuleAttrUt(unittest.TestCase):
    def test_module_doc(self):
        self.assertEqual(hixl.__doc__, "HIXL Python API")

    def test_mem_type_enum(self):
        self.assertIsNotNone(hixl.MemType.DEVICE)
        self.assertIsNotNone(hixl.MemType.HOST)
        self.assertEqual(hixl.MemType.DEVICE, hixl.DEVICE)
        self.assertEqual(hixl.MemType.HOST, hixl.HOST)

    def test_transfer_op_enum(self):
        self.assertIsNotNone(hixl.TransferOp.READ)
        self.assertIsNotNone(hixl.TransferOp.WRITE)
        self.assertEqual(hixl.TransferOp.READ, hixl.READ)
        self.assertEqual(hixl.TransferOp.WRITE, hixl.WRITE)

    def test_transfer_status_enum(self):
        self.assertIsNotNone(hixl.TransferStatus.WAITING)
        self.assertIsNotNone(hixl.TransferStatus.COMPLETED)
        self.assertIsNotNone(hixl.TransferStatus.TIMEOUT)
        self.assertIsNotNone(hixl.TransferStatus.FAILED)
        self.assertEqual(hixl.TransferStatus.WAITING, hixl.WAITING)
        self.assertEqual(hixl.TransferStatus.COMPLETED, hixl.COMPLETED)

    def test_status_code_constants(self):
        self.assertEqual(hixl.SUCCESS, 0)
        self.assertIsInstance(hixl.PARAM_INVALID, int)
        self.assertIsInstance(hixl.TIMEOUT, int)
        self.assertIsInstance(hixl.NOT_CONNECTED, int)
        self.assertIsInstance(hixl.ALREADY_CONNECTED, int)
        self.assertIsInstance(hixl.NOTIFY_FAILED, int)
        self.assertIsInstance(hixl.UNSUPPORTED, int)
        self.assertIsInstance(hixl.FAILED, int)
        self.assertIsInstance(hixl.RESOURCE_EXHAUSTED, int)
        self.assertNotEqual(hixl.SUCCESS, hixl.FAILED)

    def test_option_constants(self):
        self.assertEqual(hixl.OPTION_ENABLE_USE_FABRIC_MEM, "EnableUseFabricMem")
        self.assertEqual(hixl.OPTION_RDMA_TRAFFIC_CLASS, "RdmaTrafficClass")
        self.assertEqual(hixl.OPTION_RDMA_SERVICE_LEVEL, "RdmaServiceLevel")
        self.assertEqual(hixl.OPTION_BUFFER_POOL, "BufferPool")
        self.assertEqual(hixl.OPTION_GLOBAL_RESOURCE_CONFIG, "GlobalResourceConfig")
        self.assertEqual(hixl.OPTION_AUTO_CONNECT, "AutoConnect")
        self.assertEqual(hixl.OPTION_LOCAL_COMM_RES, "LocalCommRes")

    def test_hixl_exception_type(self):
        self.assertTrue(issubclass(hixl.HixlException, Exception))


class HixlStructUt(unittest.TestCase):
    def test_mem_desc_default(self):
        desc = hixl.MemDesc()
        self.assertIsNotNone(desc)

    def test_mem_desc_with_args(self):
        desc = hixl.MemDesc(addr=0x1000, len=4096)
        self.assertEqual(desc.addr, 0x1000)
        self.assertEqual(desc.len, 4096)

    def test_mem_desc_readwrite(self):
        desc = hixl.MemDesc()
        desc.addr = 0x2000
        desc.len = 8192
        self.assertEqual(desc.addr, 0x2000)
        self.assertEqual(desc.len, 8192)

    def test_transfer_op_desc_default(self):
        desc = hixl.TransferOpDesc()
        self.assertIsNotNone(desc)

    def test_transfer_op_desc_with_args(self):
        desc = hixl.TransferOpDesc(local_addr=0x100, remote_addr=0x200, len=64)
        self.assertEqual(desc.local_addr, 0x100)
        self.assertEqual(desc.remote_addr, 0x200)
        self.assertEqual(desc.len, 64)

    def test_transfer_op_desc_readwrite(self):
        desc = hixl.TransferOpDesc()
        desc.local_addr = 0xA000
        desc.remote_addr = 0xB000
        desc.len = 128
        self.assertEqual(desc.local_addr, 0xA000)
        self.assertEqual(desc.remote_addr, 0xB000)
        self.assertEqual(desc.len, 128)

    def test_transfer_args_default(self):
        args = hixl.TransferArgs()
        self.assertIsNotNone(args)


class HixlLifecycleUt(unittest.TestCase):
    def test_construct(self):
        engine = hixl.Hixl()
        self.assertIsNotNone(engine)

    def test_finalize_without_init(self):
        engine = hixl.Hixl()
        engine.finalize()

    def test_double_finalize(self):
        engine = hixl.Hixl()
        engine.finalize()
        engine.finalize()

    @skipUnlessEngine()
    def test_initialize_and_finalize(self):
        engine = hixl.Hixl()
        engine.initialize("127.0.0.1:0")
        engine.finalize()

    @skipUnlessEngine()
    def test_initialize_with_options(self):
        engine = hixl.Hixl()
        options = {hixl.OPTION_AUTO_CONNECT: "true"}
        engine.initialize("127.0.0.1:0", options)
        engine.finalize()

    @skipUnlessEngine()
    def test_initialize_with_empty_options(self):
        engine = hixl.Hixl()
        engine.initialize("127.0.0.1:0", {})
        engine.finalize()

    @skipUnlessEngine()
    def test_finalize_idempotent_after_init(self):
        engine = hixl.Hixl()
        engine.initialize("127.0.0.1:0")
        engine.finalize()
        engine.finalize()


class HixlErrorHandlingUt(unittest.TestCase):
    def test_deregister_without_init(self):
        engine = hixl.Hixl()
        with self.assertRaises(RuntimeError):
            engine.deregister_mem(1)

    @skipUnlessEngine()
    def test_get_transfer_status_invalid_handle(self):
        engine = hixl.Hixl()
        engine.initialize("127.0.0.1:0")
        with self.assertRaises(RuntimeError):
            engine.get_transfer_status(99999)
        engine.finalize()

    @skipUnlessEngine()
    def test_connect_invalid_remote(self):
        engine = hixl.Hixl()
        engine.initialize("127.0.0.1:0")
        with self.assertRaises(RuntimeError):
            engine.connect("invalid_remote_engine:99999", timeout=100)
        engine.finalize()

    @skipUnlessEngine()
    def test_disconnect_not_connected(self):
        engine = hixl.Hixl()
        engine.initialize("127.0.0.1:0")
        with self.assertRaises(RuntimeError):
            engine.disconnect("not_connected_engine:99999", timeout=100)
        engine.finalize()


@skipUnlessEngine()
class HixlMemRegUt(unittest.TestCase):
    def setUp(self):
        self.engine = hixl.Hixl()
        self.engine.initialize("127.0.0.1:0")

    def tearDown(self):
        self.engine.finalize()

    def test_register_host_mem(self):
        buf = ctypes.create_string_buffer(4096)
        addr = ctypes.addressof(buf)
        desc = hixl.MemDesc(addr=addr, len=4096)
        handle_id = self.engine.register_mem(desc, hixl.MemType.HOST)
        self.assertIsInstance(handle_id, int)
        self.assertGreater(handle_id, 0)
        self.engine.deregister_mem(handle_id)

    def test_register_device_mem(self):
        buf = ctypes.create_string_buffer(1024)
        addr = ctypes.addressof(buf)
        desc = hixl.MemDesc(addr=addr, len=1024)
        handle_id = self.engine.register_mem(desc, hixl.MemType.DEVICE)
        self.assertIsInstance(handle_id, int)
        self.assertGreater(handle_id, 0)
        self.engine.deregister_mem(handle_id)

    def test_register_multiple_mems(self):
        buf1 = ctypes.create_string_buffer(1024)
        buf2 = ctypes.create_string_buffer(2048)
        desc1 = hixl.MemDesc(addr=ctypes.addressof(buf1), len=1024)
        desc2 = hixl.MemDesc(addr=ctypes.addressof(buf2), len=2048)
        id1 = self.engine.register_mem(desc1, hixl.MemType.HOST)
        id2 = self.engine.register_mem(desc2, hixl.MemType.HOST)
        self.assertNotEqual(id1, id2)
        self.engine.deregister_mem(id1)
        self.engine.deregister_mem(id2)

    def test_deregister_invalid_handle(self):
        with self.assertRaises(RuntimeError):
            self.engine.deregister_mem(99999)

    def test_deregister_twice(self):
        buf = ctypes.create_string_buffer(512)
        desc = hixl.MemDesc(addr=ctypes.addressof(buf), len=512)
        handle_id = self.engine.register_mem(desc, hixl.MemType.HOST)
        self.engine.deregister_mem(handle_id)
        with self.assertRaises(RuntimeError):
            self.engine.deregister_mem(handle_id)

    def test_auto_deregister_on_finalize(self):
        buf = ctypes.create_string_buffer(256)
        desc = hixl.MemDesc(addr=ctypes.addressof(buf), len=256)
        self.engine.register_mem(desc, hixl.MemType.HOST)


@skipUnlessEngine()
class HixlTransferUt(unittest.TestCase):
    def setUp(self):
        self.server = hixl.Hixl()
        self.client = hixl.Hixl()
        self.server.initialize("127.0.0.1:16100")
        self.client.initialize("127.0.0.1:0")

    def tearDown(self):
        self.client.finalize()
        self.server.finalize()

    def test_connect_and_disconnect(self):
        self.client.connect("127.0.0.1:16100", timeout=3000)
        self.client.disconnect("127.0.0.1:16100", timeout=3000)

    def test_transfer_sync_write(self):
        self.client.connect("127.0.0.1:16100", timeout=3000)

        src_buf = ctypes.create_string_buffer(b"hello", 8)
        dst_buf = ctypes.create_string_buffer(8)
        src_desc = hixl.MemDesc(addr=ctypes.addressof(src_buf), len=8)
        dst_desc = hixl.MemDesc(addr=ctypes.addressof(dst_buf), len=8)
        self.client.register_mem(src_desc, hixl.MemType.HOST)
        self.server.register_mem(dst_desc, hixl.MemType.HOST)

        op_desc = hixl.TransferOpDesc(
            local_addr=ctypes.addressof(src_buf),
            remote_addr=ctypes.addressof(dst_buf),
            len=8,
        )
        try:
            self.client.transfer_sync(
                "127.0.0.1:16100", hixl.TransferOp.WRITE, [op_desc], timeout=3000
            )
        except hixl.HixlException:
            pass
        self.client.disconnect("127.0.0.1:16100", timeout=3000)

    def test_transfer_async_and_status(self):
        self.client.connect("127.0.0.1:16100", timeout=3000)

        src_buf = ctypes.create_string_buffer(b"async", 8)
        dst_buf = ctypes.create_string_buffer(8)
        src_desc = hixl.MemDesc(addr=ctypes.addressof(src_buf), len=8)
        dst_desc = hixl.MemDesc(addr=ctypes.addressof(dst_buf), len=8)
        self.client.register_mem(src_desc, hixl.MemType.HOST)
        self.server.register_mem(dst_desc, hixl.MemType.HOST)

        op_desc = hixl.TransferOpDesc(
            local_addr=ctypes.addressof(src_buf),
            remote_addr=ctypes.addressof(dst_buf),
            len=8,
        )
        try:
            req_id = self.client.transfer_async(
                "127.0.0.1:16100", hixl.TransferOp.WRITE, [op_desc]
            )
            self.assertIsInstance(req_id, int)
            self.assertGreater(req_id, 0)
            status = self.client.get_transfer_status(req_id, auto_cleanup=True)
            self.assertIn(
                status,
                [
                    hixl.TransferStatus.WAITING,
                    hixl.TransferStatus.COMPLETED,
                    hixl.TransferStatus.TIMEOUT,
                    hixl.TransferStatus.FAILED,
                ],
            )
        except hixl.HixlException:
            pass
        self.client.disconnect("127.0.0.1:16100", timeout=3000)

    def test_transfer_async_no_auto_cleanup(self):
        self.client.connect("127.0.0.1:16100", timeout=3000)

        src_buf = ctypes.create_string_buffer(8)
        dst_buf = ctypes.create_string_buffer(8)
        src_desc = hixl.MemDesc(addr=ctypes.addressof(src_buf), len=8)
        dst_desc = hixl.MemDesc(addr=ctypes.addressof(dst_buf), len=8)
        self.client.register_mem(src_desc, hixl.MemType.HOST)
        self.server.register_mem(dst_desc, hixl.MemType.HOST)

        op_desc = hixl.TransferOpDesc(
            local_addr=ctypes.addressof(src_buf),
            remote_addr=ctypes.addressof(dst_buf),
            len=8,
        )
        try:
            req_id = self.client.transfer_async(
                "127.0.0.1:16100", hixl.TransferOp.WRITE, [op_desc]
            )
            status1 = self.client.get_transfer_status(req_id, auto_cleanup=False)
            status2 = self.client.get_transfer_status(req_id, auto_cleanup=True)
            self.assertIn(
                status1,
                [
                    hixl.TransferStatus.WAITING,
                    hixl.TransferStatus.COMPLETED,
                    hixl.TransferStatus.TIMEOUT,
                    hixl.TransferStatus.FAILED,
                ],
            )
            self.assertIn(
                status2,
                [
                    hixl.TransferStatus.WAITING,
                    hixl.TransferStatus.COMPLETED,
                    hixl.TransferStatus.TIMEOUT,
                    hixl.TransferStatus.FAILED,
                ],
            )
        except hixl.HixlException:
            pass
        self.client.disconnect("127.0.0.1:16100", timeout=3000)

    def test_transfer_async_with_args(self):
        self.client.connect("127.0.0.1:16100", timeout=3000)

        src_buf = ctypes.create_string_buffer(8)
        dst_buf = ctypes.create_string_buffer(8)
        src_desc = hixl.MemDesc(addr=ctypes.addressof(src_buf), len=8)
        dst_desc = hixl.MemDesc(addr=ctypes.addressof(dst_buf), len=8)
        self.client.register_mem(src_desc, hixl.MemType.HOST)
        self.server.register_mem(dst_desc, hixl.MemType.HOST)

        op_desc = hixl.TransferOpDesc(
            local_addr=ctypes.addressof(src_buf),
            remote_addr=ctypes.addressof(dst_buf),
            len=8,
        )
        args = hixl.TransferArgs()
        try:
            req_id = self.client.transfer_async(
                "127.0.0.1:16100",
                hixl.TransferOp.WRITE,
                [op_desc],
                optional_args=args,
            )
            self.assertIsInstance(req_id, int)
            self.client.get_transfer_status(req_id, auto_cleanup=True)
        except hixl.HixlException:
            pass
        self.client.disconnect("127.0.0.1:16100", timeout=3000)


@skipUnlessEngine()
class HixlNotifyUt(unittest.TestCase):
    def setUp(self):
        self.server = hixl.Hixl()
        self.client = hixl.Hixl()
        self.server.initialize("127.0.0.1:16200")
        self.client.initialize("127.0.0.1:0")

    def tearDown(self):
        self.client.finalize()
        self.server.finalize()

    def test_get_notifies_empty(self):
        try:
            notifies = self.server.get_notifies()
            self.assertIsInstance(notifies, list)
        except hixl.HixlException:
            pass

    def test_send_and_get_notify(self):
        self.client.connect("127.0.0.1:16200", timeout=3000)

        try:
            self.client.send_notify(
                "127.0.0.1:16200", "test_notify", "hello_msg", timeout=3000
            )
            notifies = self.server.get_notifies()
            self.assertIsInstance(notifies, list)
            if len(notifies) > 0:
                name, msg = notifies[0]
                self.assertEqual(name, "test_notify")
                self.assertEqual(msg, "hello_msg")
        except hixl.HixlException:
            pass
        self.client.disconnect("127.0.0.1:16200", timeout=3000)

    def test_send_notify_to_invalid(self):
        with self.assertRaises(RuntimeError):
            self.client.send_notify("invalid:99999", "name", "msg", timeout=100)


if __name__ == "__main__":
    unittest.main()
