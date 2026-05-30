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

import ctypes
import logging
from typing import Dict, List, Optional
from ._capi.hixl_wrapper import hixl_lib
from .types import (
    Status,
    MemType,
    TransferOp,
    TransferStatus,
    TransferOpDesc,
    MemDesc,
    TransferArgs,
    NotifyDesc,
)
from .exception import check_status, HixlException


class Hixl:
    """Python wrapper for HIXL C++ class.

    HIXL is a single-sided communication library for Ascend chips,
    supporting D2D/D2H/H2D data transfer via HCCS and RDMA.
    """

    def __init__(self):
        self._handle = hixl_lib.HixlCreate()
        if not self._handle:
            raise HixlException(Status.FAILED, "Failed to create Hixl instance")
        self._is_initialized = False

    def __del__(self):
        try:
            handle = getattr(self, "_handle", None)
            if not handle:
                hixl_lib.HixlDestroy(handle)
                self._handle = None
        except Exception as e:
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

        options = options if options is not None else {}
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
        raise HixlException(Status.UNSUPPORTED, "finalize is not supported")

    def register_mem(self, mem_desc: MemDesc, mem_type: MemType) -> int:
        raise HixlException(Status.UNSUPPORTED, "register_mem is not supported")

    def deregister_mem(self, mem_handle: int) -> None:
        raise HixlException(Status.UNSUPPORTED, "deregister_mem is not supported")

    def connect(self, remote_engine: str, timeout_in_millis: int = 1000) -> None:
        raise HixlException(Status.UNSUPPORTED, "connect is not supported")

    def disconnect(self, remote_engine: str, timeout_in_millis: int = 1000) -> None:
        raise HixlException(Status.UNSUPPORTED, "disconnect is not supported")

    def transfer_sync(
        self,
        remote_engine: str,
        operation: TransferOp,
        op_descs: List[TransferOpDesc],
        timeout_in_millis: int = 1000,
    ) -> None:
        raise HixlException(Status.UNSUPPORTED, "transfer_sync is not supported")

    def transfer_async(
        self,
        remote_engine: str,
        operation: TransferOp,
        op_descs: List[TransferOpDesc],
        optional_args: Optional[TransferArgs] = None,
    ) -> int:
        raise HixlException(Status.UNSUPPORTED, "transfer_async is not supported")

    def get_transfer_status(self, req: int) -> TransferStatus:
        raise HixlException(Status.UNSUPPORTED, "get_transfer_status is not supported")

    def send_notify(
        self, remote_engine: str, notify: NotifyDesc, timeout_in_millis: int = 1000
    ) -> None:
        raise HixlException(Status.UNSUPPORTED, "send_notify is not supported")

    def get_notifies(self) -> List[NotifyDesc]:
        raise HixlException(Status.UNSUPPORTED, "get_notifies is not supported")
