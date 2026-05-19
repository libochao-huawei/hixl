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

from .types import Status


class HixlException(Exception):
    """HIXL runtime exception."""

    def __init__(self, status: int, message: str):
        self.status = status
        self.message = message
        super().__init__(f"[Status {status}] {message}")


def check_status(status: int, context: str = "") -> None:
    """Raise exception if status != SUCCESS.

    Args:
        status: Status code returned by HIXL API.
        context: Optional context message for the exception.

    Raises:
        HixlException: If status != SUCCESS.
    """
    if status != Status.SUCCESS:
        raise HixlException(status, context or "HIXL operation failed")


def status_to_string(status: int) -> str:
    """Convert status code to human-readable string.

    Args:
        status: Status code.

    Returns:
        Human-readable status name.
    """
    try:
        return Status(status).name
    except ValueError:
        return f"UNKNOWN({status})"
