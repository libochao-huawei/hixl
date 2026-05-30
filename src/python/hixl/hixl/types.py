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

from dataclasses import dataclass, field
from enum import IntEnum


class Status(IntEnum):
    SUCCESS = 0
    PARAM_INVALID = 103900
    TIMEOUT = 103901
    NOT_CONNECTED = 103902
    ALREADY_CONNECTED = 103903
    NOTIFY_FAILED = 103904
    UNSUPPORTED = 103905
    FAILED = 503900
    RESOURCE_EXHAUSTED = 203900


class MemType(IntEnum):
    MEM_DEVICE = 0
    MEM_HOST = 1


class TransferOp(IntEnum):
    READ = 0
    WRITE = 1


class TransferStatus(IntEnum):
    WAITING = 0
    COMPLETED = 1
    TIMEOUT = 2
    FAILED = 3


@dataclass
class TransferOpDesc:
    local_addr: int = 0
    remote_addr: int = 0
    len: int = 0


@dataclass
class MemDesc:
    addr: int = 0
    len: int = 0
    reserved: bytes = field(default_factory=lambda: b"\x00" * 128)


@dataclass
class TransferArgs:
    reserved: bytes = field(default_factory=lambda: b"\x00" * 128)


@dataclass
class NotifyDesc:
    name: str = ""
    notify_msg: str = ""


OPTION_ENABLE_USE_FABRIC_MEM = "EnableUseFabricMem"
OPTION_RDMA_TRAFFIC_CLASS = "RdmaTrafficClass"
OPTION_RDMA_SERVICE_LEVEL = "RdmaServiceLevel"
OPTION_BUFFER_POOL = "BufferPool"
OPTION_GLOBAL_RESOURCE_CONFIG = "GlobalResourceConfig"
OPTION_AUTO_CONNECT = "AutoConnect"
OPTION_LOCAL_COMM_RES = "LocalCommRes"
