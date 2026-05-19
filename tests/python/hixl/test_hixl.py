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
import pytest

from hixl import Hixl, HixlException
from hixl.types import (
    Status,
    MemType,
    TransferOp,
    TransferStatus,
    OPTION_ENABLE_USE_FABRIC_MEM,
    OPTION_RDMA_TRAFFIC_CLASS,
    OPTION_RDMA_SERVICE_LEVEL,
    OPTION_BUFFER_POOL,
    OPTION_GLOBAL_RESOURCE_CONFIG,
    OPTION_AUTO_CONNECT,
    OPTION_LOCAL_COMM_RES,
)
from hixl.exception import check_status, status_to_string


class MockHixlLib:
    """Mock hixl_lib for testing without Ascend environment."""

    def __init__(self):
        self._handle_counter = 0
        self._mem_handle_counter = 0
        self._req_handle_counter = 0
        self._initialized_handles = set()
        self._registered_mems = {}

    def HixlCreate(self):
        self._handle_counter += 1
        return ctypes.c_void_p(self._handle_counter)

    def HixlDestroy(self, handle):
        pass

    def HixlInitialize(self, handle, local_engine, keys, vals, count):
        self._initialized_handles.add(handle.value)
        return Status.SUCCESS

    def HixlFinalize(self, handle):
        self._initialized_handles.discard(handle.value)
        return Status.SUCCESS

    def HixlRegisterMem(self, handle, addr, len_, mem_type, mem_handle):
        self._mem_handle_counter += 1
        mem_handle._obj.value = self._mem_handle_counter
        self._registered_mems[self._mem_handle_counter] = (addr, len_, mem_type)
        return Status.SUCCESS

    def HixlDeregisterMem(self, handle, mem_handle):
        if mem_handle.value in self._registered_mems:
            del self._registered_mems[mem_handle.value]
        return Status.SUCCESS

    def HixlConnect(self, handle, remote_engine, timeout):
        return Status.SUCCESS

    def HixlDisconnect(self, handle, remote_engine, timeout):
        return Status.SUCCESS

    def HixlTransferSync(
        self,
        handle,
        remote_engine,
        op,
        local_addrs,
        remote_addrs,
        lengths,
        count,
        timeout,
    ):
        return Status.SUCCESS

    def HixlTransferAsync(
        self,
        handle,
        remote_engine,
        op,
        local_addrs,
        remote_addrs,
        lengths,
        count,
        args,
        req_handle,
    ):
        self._req_handle_counter += 1
        req_handle._obj.value = self._req_handle_counter
        return Status.SUCCESS

    def HixlGetTransferStatus(self, handle, req_handle, status):
        status._obj.value = TransferStatus.COMPLETED
        return Status.SUCCESS

    def HixlSendNotify(self, handle, remote_engine, name, msg, timeout):
        return Status.SUCCESS

    def HixlGetNotifies(self, handle, names, msgs, count):
        count._obj.value = 0
        return Status.SUCCESS

    def HixlFreeNotifies(self, names, msgs, count):
        pass


@pytest.fixture(autouse=True)
def mock_hixl_lib():
    """Fixture to mock hixl_lib for all tests."""
    mock_lib = MockHixlLib()
    import hixl._capi.hixl_wrapper as wrapper_module

    wrapper_module.hixl_lib._lib = mock_lib
    wrapper_module.hixl_lib._loaded = True
    yield mock_lib


class TestHixlLifecycle:
    """Test Hixl create/destroy and initialize/finalize."""

    def test_create_destroy(self, mock_hixl_lib):
        hixl = Hixl()
        assert hixl._handle is not None
        assert hixl._is_initialized is False
        assert hixl._mem_handles == {}
        assert hixl._req_handles == {}
        del hixl

    def test_initialize_without_options(self, mock_hixl_lib):
        hixl = Hixl()
        hixl.initialize("127.0.0.1:8080")
        assert hixl._is_initialized is True

    def test_initialize_with_options(self, mock_hixl_lib):
        hixl = Hixl()
        options = {
            OPTION_ENABLE_USE_FABRIC_MEM: "true",
            OPTION_RDMA_TRAFFIC_CLASS: "0",
        }
        hixl.initialize("127.0.0.1:8080", options)
        assert hixl._is_initialized is True

    def test_initialize_idempotent(self, mock_hixl_lib):
        hixl = Hixl()
        hixl.initialize("127.0.0.1:8080")
        hixl.initialize("127.0.0.1:8080")
        assert hixl._is_initialized is True

    def test_finalize_without_initialize(self, mock_hixl_lib):
        hixl = Hixl()
        hixl.finalize()
        assert hixl._is_initialized is False

    def test_finalize_after_initialize(self, mock_hixl_lib):
        hixl = Hixl()
        hixl.initialize("127.0.0.1:8080")
        hixl.finalize()
        assert hixl._is_initialized is False

    def test_finalize_clears_mem_handles(self, mock_hixl_lib):
        hixl = Hixl()
        hixl.initialize("127.0.0.1:8080")
        hixl.register_mem(0x1000, 1024, MemType.DEVICE)
        hixl.register_mem(0x2000, 2048, MemType.HOST)
        assert len(hixl._mem_handles) == 2
        hixl.finalize()
        assert hixl._mem_handles == {}

    def test_finalize_clears_req_handles(self, mock_hixl_lib):
        hixl = Hixl()
        hixl.initialize("127.0.0.1:8080")
        hixl.transfer_async("remote", TransferOp.READ, [(0x1000, 0x2000, 1024)])
        hixl.transfer_async("remote", TransferOp.WRITE, [(0x3000, 0x4000, 2048)])
        assert len(hixl._req_handles) == 2
        hixl.finalize()
        assert hixl._req_handles == {}


class TestMemoryManagement:
    """Test register_mem and deregister_mem."""

    def test_register_mem_device(self, mock_hixl_lib):
        hixl = Hixl()
        hixl.initialize("127.0.0.1:8080")
        handle_id = hixl.register_mem(0x1000, 1024, MemType.DEVICE)
        assert handle_id == 1
        assert handle_id in hixl._mem_handles

    def test_register_mem_host(self, mock_hixl_lib):
        hixl = Hixl()
        hixl.initialize("127.0.0.1:8080")
        handle_id = hixl.register_mem(0x2000, 2048, MemType.HOST)
        assert handle_id == 1
        assert handle_id in hixl._mem_handles

    def test_register_mem_multiple(self, mock_hixl_lib):
        hixl = Hixl()
        hixl.initialize("127.0.0.1:8080")
        id1 = hixl.register_mem(0x1000, 1024, MemType.DEVICE)
        id2 = hixl.register_mem(0x2000, 2048, MemType.DEVICE)
        id3 = hixl.register_mem(0x3000, 4096, MemType.HOST)
        assert id1 == 1
        assert id2 == 2
        assert id3 == 3
        assert len(hixl._mem_handles) == 3

    def test_deregister_mem_valid(self, mock_hixl_lib):
        hixl = Hixl()
        hixl.initialize("127.0.0.1:8080")
        handle_id = hixl.register_mem(0x1000, 1024, MemType.DEVICE)
        hixl.deregister_mem(handle_id)
        assert handle_id not in hixl._mem_handles

    def test_deregister_mem_invalid_handle_id(self, mock_hixl_lib):
        hixl = Hixl()
        hixl.initialize("127.0.0.1:8080")
        hixl.deregister_mem(999)
        assert len(hixl._mem_handles) == 0

    def test_deregister_mem_twice(self, mock_hixl_lib):
        hixl = Hixl()
        hixl.initialize("127.0.0.1:8080")
        handle_id = hixl.register_mem(0x1000, 1024, MemType.DEVICE)
        hixl.deregister_mem(handle_id)
        hixl.deregister_mem(handle_id)
        assert handle_id not in hixl._mem_handles


class TestConnection:
    """Test connect and disconnect."""

    def test_connect_default_timeout(self, mock_hixl_lib):
        hixl = Hixl()
        hixl.initialize("127.0.0.1:8080")
        hixl.connect("127.0.0.1:9090")

    def test_connect_custom_timeout(self, mock_hixl_lib):
        hixl = Hixl()
        hixl.initialize("127.0.0.1:8080")
        hixl.connect("127.0.0.1:9090", timeout=5000)

    def test_disconnect_default_timeout(self, mock_hixl_lib):
        hixl = Hixl()
        hixl.initialize("127.0.0.1:8080")
        hixl.connect("127.0.0.1:9090")
        hixl.disconnect("127.0.0.1:9090")

    def test_disconnect_custom_timeout(self, mock_hixl_lib):
        hixl = Hixl()
        hixl.initialize("127.0.0.1:8080")
        hixl.connect("127.0.0.1:9090")
        hixl.disconnect("127.0.0.1:9090", timeout=3000)


class TestTransfer:
    """Test transfer_sync, transfer_async, and get_transfer_status."""

    def test_transfer_sync_empty_list(self, mock_hixl_lib):
        hixl = Hixl()
        hixl.initialize("127.0.0.1:8080")
        hixl.transfer_sync("127.0.0.1:9090", TransferOp.READ, [])

    def test_transfer_sync_single_desc(self, mock_hixl_lib):
        hixl = Hixl()
        hixl.initialize("127.0.0.1:8080")
        hixl.transfer_sync(
            "127.0.0.1:9090",
            TransferOp.READ,
            [(0x1000, 0x2000, 1024)],
        )

    def test_transfer_sync_multiple_descs(self, mock_hixl_lib):
        hixl = Hixl()
        hixl.initialize("127.0.0.1:8080")
        hixl.transfer_sync(
            "127.0.0.1:9090",
            TransferOp.WRITE,
            [
                (0x1000, 0x2000, 1024),
                (0x3000, 0x4000, 2048),
                (0x5000, 0x6000, 4096),
            ],
        )

    def test_transfer_sync_custom_timeout(self, mock_hixl_lib):
        hixl = Hixl()
        hixl.initialize("127.0.0.1:8080")
        hixl.transfer_sync(
            "127.0.0.1:9090",
            TransferOp.READ,
            [(0x1000, 0x2000, 1024)],
            timeout=5000,
        )

    def test_transfer_async_empty_list_raises(self, mock_hixl_lib):
        hixl = Hixl()
        hixl.initialize("127.0.0.1:8080")
        with pytest.raises(ValueError, match="op_descs must not be empty"):
            hixl.transfer_async("127.0.0.1:9090", TransferOp.READ, [])

    def test_transfer_async_single_desc(self, mock_hixl_lib):
        hixl = Hixl()
        hixl.initialize("127.0.0.1:8080")
        req_id = hixl.transfer_async(
            "127.0.0.1:9090",
            TransferOp.READ,
            [(0x1000, 0x2000, 1024)],
        )
        assert req_id == 1
        assert req_id in hixl._req_handles

    def test_transfer_async_multiple_descs(self, mock_hixl_lib):
        hixl = Hixl()
        hixl.initialize("127.0.0.1:8080")
        req_id = hixl.transfer_async(
            "127.0.0.1:9090",
            TransferOp.WRITE,
            [
                (0x1000, 0x2000, 1024),
                (0x3000, 0x4000, 2048),
            ],
        )
        assert req_id == 1

    def test_transfer_async_with_optional_args(self, mock_hixl_lib):
        hixl = Hixl()
        hixl.initialize("127.0.0.1:8080")
        args = b"\x00" * 64
        req_id = hixl.transfer_async(
            "127.0.0.1:9090",
            TransferOp.READ,
            [(0x1000, 0x2000, 1024)],
            optional_args=args,
        )
        assert req_id == 1

    def test_transfer_async_multiple_requests(self, mock_hixl_lib):
        hixl = Hixl()
        hixl.initialize("127.0.0.1:8080")
        id1 = hixl.transfer_async(
            "127.0.0.1:9090", TransferOp.READ, [(0x1000, 0x2000, 1024)]
        )
        id2 = hixl.transfer_async(
            "127.0.0.1:9090", TransferOp.WRITE, [(0x3000, 0x4000, 2048)]
        )
        assert id1 == 1
        assert id2 == 2
        assert len(hixl._req_handles) == 2

    def test_get_transfer_status_valid(self, mock_hixl_lib):
        hixl = Hixl()
        hixl.initialize("127.0.0.1:8080")
        req_id = hixl.transfer_async(
            "127.0.0.1:9090", TransferOp.READ, [(0x1000, 0x2000, 1024)]
        )
        status = hixl.get_transfer_status(req_id)
        assert status == TransferStatus.COMPLETED
        assert req_id not in hixl._req_handles

    def test_get_transfer_status_no_auto_cleanup(self, mock_hixl_lib):
        hixl = Hixl()
        hixl.initialize("127.0.0.1:8080")
        req_id = hixl.transfer_async(
            "127.0.0.1:9090", TransferOp.READ, [(0x1000, 0x2000, 1024)]
        )
        status = hixl.get_transfer_status(req_id, auto_cleanup=False)
        assert status == TransferStatus.COMPLETED
        assert req_id in hixl._req_handles

    def test_get_transfer_status_invalid_handle(self, mock_hixl_lib):
        hixl = Hixl()
        hixl.initialize("127.0.0.1:8080")
        with pytest.raises(ValueError, match="Invalid request handle ID"):
            hixl.get_transfer_status(999)


class TestInitStateCheck:
    """Test initialization state checks."""

    def test_register_mem_without_init(self, mock_hixl_lib):
        hixl = Hixl()
        with pytest.raises(HixlException, match="not initialized"):
            hixl.register_mem(0x1000, 1024, MemType.DEVICE)

    def test_connect_without_init(self, mock_hixl_lib):
        hixl = Hixl()
        with pytest.raises(HixlException, match="not initialized"):
            hixl.connect("127.0.0.1:9090")

    def test_disconnect_without_init(self, mock_hixl_lib):
        hixl = Hixl()
        with pytest.raises(HixlException, match="not initialized"):
            hixl.disconnect("127.0.0.1:9090")

    def test_transfer_sync_without_init(self, mock_hixl_lib):
        hixl = Hixl()
        with pytest.raises(HixlException, match="not initialized"):
            hixl.transfer_sync(
                "127.0.0.1:9090", TransferOp.READ, [(0x1000, 0x2000, 1024)]
            )

    def test_transfer_async_without_init(self, mock_hixl_lib):
        hixl = Hixl()
        with pytest.raises(HixlException, match="not initialized"):
            hixl.transfer_async(
                "127.0.0.1:9090", TransferOp.READ, [(0x1000, 0x2000, 1024)]
            )

    def test_get_transfer_status_without_init(self, mock_hixl_lib):
        hixl = Hixl()
        hixl._req_handles[1] = 1
        with pytest.raises(HixlException, match="not initialized"):
            hixl.get_transfer_status(1)

    def test_send_notify_without_init(self, mock_hixl_lib):
        hixl = Hixl()
        with pytest.raises(HixlException, match="not initialized"):
            hixl.send_notify("127.0.0.1:9090", "name", "msg")

    def test_get_notifies_without_init(self, mock_hixl_lib):
        hixl = Hixl()
        with pytest.raises(HixlException, match="not initialized"):
            hixl.get_notifies()


class TestOptionalArgsValidation:
    """Test optional_args length validation."""

    def test_optional_args_exceed_128_bytes(self, mock_hixl_lib):
        hixl = Hixl()
        hixl.initialize("127.0.0.1:8080")
        with pytest.raises(ValueError, match="exceeds 128 bytes"):
            hixl.transfer_async(
                "127.0.0.1:9090",
                TransferOp.READ,
                [(0x1000, 0x2000, 1024)],
                optional_args=b"\x00" * 129,
            )

    def test_optional_args_exact_128_bytes(self, mock_hixl_lib):
        hixl = Hixl()
        hixl.initialize("127.0.0.1:8080")
        req_id = hixl.transfer_async(
            "127.0.0.1:9090",
            TransferOp.READ,
            [(0x1000, 0x2000, 1024)],
            optional_args=b"\x00" * 128,
        )
        assert req_id == 1


class TestNotify:
    """Test send_notify and get_notifies."""

    def test_send_notify_default_timeout(self, mock_hixl_lib):
        hixl = Hixl()
        hixl.initialize("127.0.0.1:8080")
        hixl.send_notify("127.0.0.1:9090", "kv_ready", "block_0")

    def test_send_notify_custom_timeout(self, mock_hixl_lib):
        hixl = Hixl()
        hixl.initialize("127.0.0.1:8080")
        hixl.send_notify("127.0.0.1:9090", "kv_ready", "block_0", timeout=2000)

    def test_get_notifies_empty(self, mock_hixl_lib):
        hixl = Hixl()
        hixl.initialize("127.0.0.1:8080")
        notifies = hixl.get_notifies()
        assert notifies == []


class TestTypesEnums:
    """Test all enum types."""

    def test_status_values(self):
        assert Status.SUCCESS == 0
        assert Status.PARAM_INVALID == 103900
        assert Status.TIMEOUT == 103901
        assert Status.NOT_CONNECTED == 103902
        assert Status.ALREADY_CONNECTED == 103903
        assert Status.NOTIFY_FAILED == 103904
        assert Status.UNSUPPORTED == 103905
        assert Status.FAILED == 503900
        assert Status.RESOURCE_EXHAUSTED == 203900

    def test_mem_type_values(self):
        assert MemType.DEVICE == 0
        assert MemType.HOST == 1

    def test_transfer_op_values(self):
        assert TransferOp.READ == 0
        assert TransferOp.WRITE == 1

    def test_transfer_status_values(self):
        assert TransferStatus.WAITING == 0
        assert TransferStatus.COMPLETED == 1
        assert TransferStatus.TIMEOUT == 2
        assert TransferStatus.FAILED == 3

    def test_status_int_conversion(self):
        assert int(Status.SUCCESS) == 0
        assert Status(0) == Status.SUCCESS
        assert Status(103900) == Status.PARAM_INVALID

    def test_option_constants(self):
        assert OPTION_ENABLE_USE_FABRIC_MEM == "EnableUseFabricMem"
        assert OPTION_RDMA_TRAFFIC_CLASS == "RdmaTrafficClass"
        assert OPTION_RDMA_SERVICE_LEVEL == "RdmaServiceLevel"
        assert OPTION_BUFFER_POOL == "BufferPool"
        assert OPTION_GLOBAL_RESOURCE_CONFIG == "GlobalResourceConfig"
        assert OPTION_AUTO_CONNECT == "AutoConnect"
        assert OPTION_LOCAL_COMM_RES == "LocalCommRes"


class TestExceptionHandling:
    """Test HixlException and check_status."""

    def test_exception_creation(self):
        exc = HixlException(Status.PARAM_INVALID, "Test error")
        assert exc.status == Status.PARAM_INVALID
        assert exc.message == "Test error"
        assert "[Status 103900]" in str(exc)
        assert "Test error" in str(exc)

    def test_exception_with_custom_status(self):
        exc = HixlException(999, "Custom error")
        assert exc.status == 999
        assert "Custom error" in str(exc)

    def test_check_status_success(self):
        check_status(Status.SUCCESS, "Should not raise")

    def test_check_status_failure_raises(self):
        with pytest.raises(HixlException) as exc_info:
            check_status(Status.PARAM_INVALID, "Param error")
        assert exc_info.value.status == Status.PARAM_INVALID

    def test_check_status_without_context(self):
        with pytest.raises(HixlException) as exc_info:
            check_status(Status.FAILED)
        assert exc_info.value.message == "HIXL operation failed"

    def test_status_to_string_known(self):
        assert status_to_string(Status.SUCCESS) == "SUCCESS"
        assert status_to_string(Status.PARAM_INVALID) == "PARAM_INVALID"

    def test_status_to_string_unknown(self):
        assert status_to_string(999999) == "UNKNOWN(999999)"


class TestHandleTracking:
    """Test handle tracking logic."""

    def test_mem_handle_counter_increments(self, mock_hixl_lib):
        hixl = Hixl()
        hixl.initialize("127.0.0.1:8080")
        id1 = hixl.register_mem(0x1000, 1024, MemType.DEVICE)
        id2 = hixl.register_mem(0x2000, 2048, MemType.HOST)
        id3 = hixl.register_mem(0x3000, 4096, MemType.DEVICE)
        assert id1 == 1
        assert id2 == 2
        assert id3 == 3
        assert hixl._mem_handle_counter == 3

    def test_req_handle_counter_increments(self, mock_hixl_lib):
        hixl = Hixl()
        hixl.initialize("127.0.0.1:8080")
        id1 = hixl.transfer_async("remote", TransferOp.READ, [(0x1000, 0x2000, 1024)])
        id2 = hixl.transfer_async("remote", TransferOp.WRITE, [(0x3000, 0x4000, 2048)])
        assert id1 == 1
        assert id2 == 2
        assert hixl._req_handle_counter == 2

    def test_mem_handles_stores_pointer_values(self, mock_hixl_lib):
        hixl = Hixl()
        hixl.initialize("127.0.0.1:8080")
        handle_id = hixl.register_mem(0x1000, 1024, MemType.DEVICE)
        assert handle_id in hixl._mem_handles
        assert isinstance(hixl._mem_handles[handle_id], int)

    def test_req_handles_stores_pointer_values(self, mock_hixl_lib):
        hixl = Hixl()
        hixl.initialize("127.0.0.1:8080")
        req_id = hixl.transfer_async(
            "remote", TransferOp.READ, [(0x1000, 0x2000, 1024)]
        )
        assert req_id in hixl._req_handles
        assert isinstance(hixl._req_handles[req_id], int)


class TestErrorHandling:
    """Test error handling with mocked failures."""

    def test_initialize_failure(self, mock_hixl_lib):
        mock_hixl_lib.HixlInitialize = lambda *args: Status.PARAM_INVALID
        hixl = Hixl()
        with pytest.raises(HixlException) as exc_info:
            hixl.initialize("invalid")
        assert exc_info.value.status == Status.PARAM_INVALID

    def test_register_mem_failure(self, mock_hixl_lib):
        mock_hixl_lib.HixlRegisterMem = lambda *args: Status.PARAM_INVALID
        hixl = Hixl()
        hixl._is_initialized = True
        with pytest.raises(HixlException) as exc_info:
            hixl.register_mem(0, 0, MemType.DEVICE)
        assert exc_info.value.status == Status.PARAM_INVALID

    def test_connect_failure(self, mock_hixl_lib):
        mock_hixl_lib.HixlConnect = lambda *args: Status.NOT_CONNECTED
        hixl = Hixl()
        hixl._is_initialized = True
        with pytest.raises(HixlException) as exc_info:
            hixl.connect("invalid_remote")
        assert exc_info.value.status == Status.NOT_CONNECTED

    def test_transfer_sync_failure(self, mock_hixl_lib):
        mock_hixl_lib.HixlTransferSync = lambda *args: Status.TIMEOUT
        hixl = Hixl()
        hixl._is_initialized = True
        with pytest.raises(HixlException) as exc_info:
            hixl.transfer_sync("remote", TransferOp.READ, [(0x1000, 0x2000, 1024)])
        assert exc_info.value.status == Status.TIMEOUT

    def test_get_transfer_status_failure(self, mock_hixl_lib):
        mock_hixl_lib.HixlGetTransferStatus = lambda *args: Status.FAILED
        hixl = Hixl()
        hixl._is_initialized = True
        req_id = hixl.transfer_async(
            "remote", TransferOp.READ, [(0x1000, 0x2000, 1024)]
        )
        with pytest.raises(HixlException) as exc_info:
            hixl.get_transfer_status(req_id)
        assert exc_info.value.status == Status.FAILED
