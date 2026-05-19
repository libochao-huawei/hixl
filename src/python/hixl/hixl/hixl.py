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
from typing import Dict, List, Tuple, Optional
from ._capi.hixl_wrapper import hixl_lib
from .types import MemType, TransferOp, TransferStatus, Status
from .exception import check_status, HixlException


class Hixl:
    """Python wrapper for HIXL C++ class.

    HIXL is a single-sided communication library for Ascend chips,
    supporting D2D/D2H/H2D data transfer via HCCS and RDMA.
    """

    def __init__(self):
        self._handle = hixl_lib.HixlCreate()
        self._is_initialized = False
        self._mem_handles: Dict[int, int] = {}
        self._req_handles: Dict[int, int] = {}
        self._mem_handle_counter = 0
        self._req_handle_counter = 0

    def __del__(self):
        try:
            if self._handle is not None:
                self.finalize()
                hixl_lib.HixlDestroy(self._handle)
                self._handle = None
        except Exception as e:
            import logging

            logging.warning(f"Hixl cleanup failed in __del__: {e}")

    def initialize(
        self, local_engine: str, options: Optional[Dict[str, str]] = None
    ) -> None:
        """Initialize HIXL.

        Args:
            local_engine: HIXL unique identifier (e.g., "host_ip:host_port").
                          If host_port > 0, acts as server listening on that port.
            options: Optional configuration options.

        Raises:
            HixlException: If initialization fails.
            UnicodeEncodeError: If encoding fails.
        """
        if self._is_initialized:
            return

        options = options or {}
        keys = list(options.keys())
        vals = list(options.values())

        try:
            local_engine_encoded = local_engine.encode("utf-8")
        except UnicodeEncodeError as e:
            raise HixlException(
                Status.PARAM_INVALID, f"Failed to encode local_engine: {e}"
            )

        if keys:
            try:
                keys_arr = (ctypes.c_char_p * len(keys))(
                    *[k.encode("utf-8") for k in keys]
                )
                vals_arr = (ctypes.c_char_p * len(vals))(
                    *[v.encode("utf-8") for v in vals]
                )
            except UnicodeEncodeError as e:
                raise HixlException(
                    Status.PARAM_INVALID, f"Failed to encode options: {e}"
                )
            status = hixl_lib.HixlInitialize(
                self._handle, local_engine_encoded, keys_arr, vals_arr, len(keys)
            )
        else:
            status = hixl_lib.HixlInitialize(
                self._handle, local_engine_encoded, None, None, 0
            )

        check_status(status, f"Initialize failed for {local_engine}")
        self._is_initialized = True

    def finalize(self) -> None:
        """Clean up HIXL resources."""
        if not self._is_initialized:
            return

        errors = []
        for handle_id, mem_handle_ptr in list(self._mem_handles.items()):
            try:
                status = hixl_lib.HixlDeregisterMem(
                    self._handle, ctypes.c_void_p(mem_handle_ptr)
                )
                if status != 0:
                    errors.append(
                        f"DeregisterMem failed for handle={handle_id}: status={status}"
                    )
            except Exception as e:
                errors.append(f"DeregisterMem exception for handle={handle_id}: {e}")
        self._mem_handles.clear()
        self._req_handles.clear()

        try:
            hixl_lib.HixlFinalize(self._handle)
        except Exception as e:
            errors.append(f"HixlFinalize exception: {e}")

        self._is_initialized = False

        if errors:
            import warnings

            warnings.warn(f"Errors during finalize: {errors}")

    def register_mem(self, addr: int, length: int, mem_type: MemType) -> int:
        """Register memory for transfer.

        Args:
            addr: Memory address (uintptr_t).
            length: Memory length in bytes.
            mem_type: Memory type (DEVICE or HOST).

        Returns:
            Memory handle ID for deregistration.

        Raises:
            HixlException: If registration fails or HIXL not initialized.
        """
        if not self._is_initialized:
            raise HixlException(
                Status.NOT_CONNECTED, "HIXL not initialized, call initialize() first"
            )

        mem_handle = ctypes.c_void_p()
        status = hixl_lib.HixlRegisterMem(
            self._handle, addr, length, mem_type.value, ctypes.byref(mem_handle)
        )
        check_status(status, f"RegisterMem failed for addr={addr}, len={length}")

        self._mem_handle_counter += 1
        handle_id = self._mem_handle_counter
        self._mem_handles[handle_id] = mem_handle.value if mem_handle.value else 0
        return handle_id

    def deregister_mem(self, handle_id: int) -> None:
        """Deregister previously registered memory.

        Args:
            handle_id: Memory handle ID returned by register_mem.

        Raises:
            HixlException: If deregistration fails.
        """
        if handle_id not in self._mem_handles:
            return

        mem_handle_ptr = self._mem_handles[handle_id]
        status = hixl_lib.HixlDeregisterMem(
            self._handle, ctypes.c_void_p(mem_handle_ptr)
        )
        check_status(status, f"DeregisterMem failed for handle={handle_id}")
        del self._mem_handles[handle_id]

    def connect(self, remote_engine: str, timeout: int = 1000) -> None:
        """Connect to remote HIXL instance.

        Args:
            remote_engine: Remote HIXL identifier.
            timeout: Timeout in milliseconds.

        Raises:
            HixlException: If connection fails or HIXL not initialized.
            UnicodeEncodeError: If encoding fails.
        """
        if not self._is_initialized:
            raise HixlException(
                Status.NOT_CONNECTED, "HIXL not initialized, call initialize() first"
            )

        try:
            remote_engine_encoded = remote_engine.encode("utf-8")
        except UnicodeEncodeError as e:
            raise HixlException(
                Status.PARAM_INVALID, f"Failed to encode remote_engine: {e}"
            )

        status = hixl_lib.HixlConnect(self._handle, remote_engine_encoded, timeout)
        check_status(status, f"Connect failed to {remote_engine}")

    def disconnect(self, remote_engine: str, timeout: int = 1000) -> None:
        """Disconnect from remote HIXL instance.

        Args:
            remote_engine: Remote HIXL identifier.
            timeout: Timeout in milliseconds.

        Raises:
            HixlException: If disconnection fails or HIXL not initialized.
            UnicodeEncodeError: If encoding fails.
        """
        if not self._is_initialized:
            raise HixlException(
                Status.NOT_CONNECTED, "HIXL not initialized, call initialize() first"
            )

        try:
            remote_engine_encoded = remote_engine.encode("utf-8")
        except UnicodeEncodeError as e:
            raise HixlException(
                Status.PARAM_INVALID, f"Failed to encode remote_engine: {e}"
            )

        status = hixl_lib.HixlDisconnect(self._handle, remote_engine_encoded, timeout)
        check_status(status, f"Disconnect failed from {remote_engine}")

    def transfer_sync(
        self,
        remote_engine: str,
        operation: TransferOp,
        op_descs: List[Tuple[int, int, int]],
        timeout: int = 1000,
    ) -> None:
        """Batch synchronous transfer.

        Args:
            remote_engine: Remote HIXL identifier.
            operation: READ or WRITE.
            op_descs: List of (local_addr, remote_addr, length) tuples.
            timeout: Timeout in milliseconds.

        Raises:
            HixlException: If transfer fails or HIXL not initialized.
            UnicodeEncodeError: If encoding fails.
        """
        if not self._is_initialized:
            raise HixlException(
                Status.NOT_CONNECTED, "HIXL not initialized, call initialize() first"
            )

        if not op_descs:
            return

        try:
            remote_engine_encoded = remote_engine.encode("utf-8")
        except UnicodeEncodeError as e:
            raise HixlException(
                Status.PARAM_INVALID, f"Failed to encode remote_engine: {e}"
            )

        count = len(op_descs)
        local_addrs = (ctypes.c_size_t * count)(*[d[0] for d in op_descs])
        remote_addrs = (ctypes.c_size_t * count)(*[d[1] for d in op_descs])
        lengths = (ctypes.c_size_t * count)(*[d[2] for d in op_descs])

        status = hixl_lib.HixlTransferSync(
            self._handle,
            remote_engine_encoded,
            operation.value,
            local_addrs,
            remote_addrs,
            lengths,
            count,
            timeout,
        )
        check_status(status, f"TransferSync failed to {remote_engine}")

    def transfer_async(
        self,
        remote_engine: str,
        operation: TransferOp,
        op_descs: List[Tuple[int, int, int]],
        optional_args: Optional[bytes] = None,
    ) -> int:
        """Batch asynchronous transfer.

        Args:
            remote_engine: Remote HIXL identifier.
            operation: READ or WRITE.
            op_descs: List of (local_addr, remote_addr, length) tuples.
            optional_args: Optional transfer arguments (max 128 bytes).

        Returns:
            Request handle ID for status checking.

        Raises:
            HixlException: If transfer submission fails or HIXL not initialized.
            ValueError: If op_descs is empty or optional_args exceeds 128 bytes.
            UnicodeEncodeError: If encoding fails.
        """
        if not self._is_initialized:
            raise HixlException(
                Status.NOT_CONNECTED, "HIXL not initialized, call initialize() first"
            )

        if not op_descs:
            raise ValueError("op_descs must not be empty")

        if optional_args and len(optional_args) > 128:
            raise ValueError(
                f"optional_args exceeds 128 bytes: got {len(optional_args)}"
            )

        try:
            remote_engine_encoded = remote_engine.encode("utf-8")
        except UnicodeEncodeError as e:
            raise HixlException(
                Status.PARAM_INVALID, f"Failed to encode remote_engine: {e}"
            )

        count = len(op_descs)
        local_addrs = (ctypes.c_size_t * count)(*[d[0] for d in op_descs])
        remote_addrs = (ctypes.c_size_t * count)(*[d[1] for d in op_descs])
        lengths = (ctypes.c_size_t * count)(*[d[2] for d in op_descs])

        args_ptr = None
        if optional_args:
            args_ptr = (ctypes.c_char * 128)(*optional_args)

        req_handle = ctypes.c_void_p()
        status = hixl_lib.HixlTransferAsync(
            self._handle,
            remote_engine_encoded,
            operation.value,
            local_addrs,
            remote_addrs,
            lengths,
            count,
            args_ptr,
            ctypes.byref(req_handle),
        )
        check_status(status, f"TransferAsync failed to {remote_engine}")

        self._req_handle_counter += 1
        req_handle_id = self._req_handle_counter
        self._req_handles[req_handle_id] = req_handle.value if req_handle.value else 0
        return req_handle_id

    def get_transfer_status(
        self, req_handle_id: int, auto_cleanup: bool = True
    ) -> TransferStatus:
        """Get transfer request status.

        Args:
            req_handle_id: Request handle ID from transfer_async.
            auto_cleanup: If True, remove handle from tracking when COMPLETED/TIMEOUT/FAILED.

        Returns:
            TransferStatus enum value.

        Raises:
            HixlException: If status query fails or HIXL not initialized.
            ValueError: If req_handle_id is invalid.
        """
        if not self._is_initialized:
            raise HixlException(
                Status.NOT_CONNECTED, "HIXL not initialized, call initialize() first"
            )

        req_handle_ptr = self._req_handles.get(req_handle_id)
        if req_handle_ptr is None:
            raise ValueError(f"Invalid request handle ID: {req_handle_id}")

        status_int = ctypes.c_int()
        req_handle = ctypes.c_void_p(req_handle_ptr)

        status = hixl_lib.HixlGetTransferStatus(
            self._handle, req_handle, ctypes.byref(status_int)
        )
        check_status(status, f"GetTransferStatus failed for req={req_handle_id}")

        result = TransferStatus(status_int.value)

        if auto_cleanup and result in (
            TransferStatus.COMPLETED,
            TransferStatus.TIMEOUT,
            TransferStatus.FAILED,
        ):
            del self._req_handles[req_handle_id]

        return result

    def send_notify(
        self, remote_engine: str, notify_name: str, notify_msg: str, timeout: int = 1000
    ) -> None:
        """Send notify message to remote HIXL.

        Args:
            remote_engine: Remote HIXL identifier.
            notify_name: Notify name.
            notify_msg: Notify message.
            timeout: Timeout in milliseconds.

        Raises:
            HixlException: If notify fails or HIXL not initialized.
            UnicodeEncodeError: If encoding fails.
        """
        if not self._is_initialized:
            raise HixlException(
                Status.NOT_CONNECTED, "HIXL not initialized, call initialize() first"
            )

        try:
            remote_engine_encoded = remote_engine.encode("utf-8")
            notify_name_encoded = notify_name.encode("utf-8")
            notify_msg_encoded = notify_msg.encode("utf-8")
        except UnicodeEncodeError as e:
            raise HixlException(
                Status.PARAM_INVALID, f"Failed to encode notify parameters: {e}"
            )

        status = hixl_lib.HixlSendNotify(
            self._handle,
            remote_engine_encoded,
            notify_name_encoded,
            notify_msg_encoded,
            timeout,
        )
        check_status(status, f"SendNotify failed to {remote_engine}")

    def get_notifies(self) -> List[Tuple[str, str]]:
        """Get all received notifies and clear buffer.

        Returns:
            List of (name, message) tuples.

        Raises:
            HixlException: If query fails or HIXL not initialized.
        """
        if not self._is_initialized:
            raise HixlException(
                Status.NOT_CONNECTED, "HIXL not initialized, call initialize() first"
            )

        notify_names = ctypes.POINTER(ctypes.c_char_p)()
        notify_msgs = ctypes.POINTER(ctypes.c_char_p)()
        count = ctypes.c_int()

        status = hixl_lib.HixlGetNotifies(
            self._handle,
            ctypes.byref(notify_names),
            ctypes.byref(notify_msgs),
            ctypes.byref(count),
        )
        check_status(status, "GetNotifies failed")

        result = []
        if count.value > 0 and notify_names and notify_msgs:
            for i in range(count.value):
                try:
                    if notify_names[i] is not None:
                        name = notify_names[i].decode("utf-8", errors="replace")
                    else:
                        name = ""
                    if notify_msgs[i] is not None:
                        msg = notify_msgs[i].decode("utf-8", errors="replace")
                    else:
                        msg = ""
                    result.append((name, msg))
                except (UnicodeDecodeError, AttributeError) as e:
                    import warnings

                    warnings.warn(f"Failed to decode notify at index {i}: {e}")
                    result.append(("", ""))

            try:
                hixl_lib.HixlFreeNotifies(notify_names, notify_msgs, count.value)
            except Exception as e:
                import warnings

                warnings.warn(f"Failed to free notifies: {e}")

        return result
