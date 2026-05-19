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

from ._lib_loader import load_lib_from_path

LIB_NAME = "libhixl_wrapper.so"
_dir = os.path.dirname(os.path.abspath(__file__))

c_void_p = ctypes.c_void_p
c_char_p = ctypes.c_char_p
c_int = ctypes.c_int
c_int32 = ctypes.c_int32
c_uint32 = ctypes.c_uint32
c_size_t = ctypes.c_size_t
c_uintptr_t = ctypes.c_size_t


class _LazyLibLoader:
    """Lazy loader for hixl_wrapper library.

    Library is loaded only when first accessed, allowing tests to mock before import.
    """

    _lib = None
    _loaded = False

    def _load_lib(self):
        if not self._loaded:
            self._lib = load_lib_from_path(LIB_NAME, _dir)
            self._setup_argtypes()
            self._loaded = True
        return self._lib

    def _setup_argtypes(self):
        lib = self._lib
        lib.HixlCreate.argtypes = []
        lib.HixlCreate.restype = c_void_p

        lib.HixlDestroy.argtypes = [c_void_p]
        lib.HixlDestroy.restype = None

        lib.HixlInitialize.argtypes = [
            c_void_p,
            c_char_p,
            ctypes.POINTER(c_char_p),
            ctypes.POINTER(c_char_p),
            c_int,
        ]
        lib.HixlInitialize.restype = c_uint32

        lib.HixlFinalize.argtypes = [c_void_p]
        lib.HixlFinalize.restype = None

        lib.HixlRegisterMem.argtypes = [
            c_void_p,
            c_uintptr_t,
            c_size_t,
            c_int,
            ctypes.POINTER(c_void_p),
        ]
        lib.HixlRegisterMem.restype = c_uint32

        lib.HixlDeregisterMem.argtypes = [c_void_p, c_void_p]
        lib.HixlDeregisterMem.restype = c_uint32

        lib.HixlConnect.argtypes = [c_void_p, c_char_p, c_int32]
        lib.HixlConnect.restype = c_uint32

        lib.HixlDisconnect.argtypes = [c_void_p, c_char_p, c_int32]
        lib.HixlDisconnect.restype = c_uint32

        lib.HixlTransferSync.argtypes = [
            c_void_p,
            c_char_p,
            c_int,
            ctypes.POINTER(c_uintptr_t),
            ctypes.POINTER(c_uintptr_t),
            ctypes.POINTER(c_size_t),
            c_int,
            c_int32,
        ]
        lib.HixlTransferSync.restype = c_uint32

        lib.HixlTransferAsync.argtypes = [
            c_void_p,
            c_char_p,
            c_int,
            ctypes.POINTER(c_uintptr_t),
            ctypes.POINTER(c_uintptr_t),
            ctypes.POINTER(c_size_t),
            c_int,
            c_void_p,
            ctypes.POINTER(c_void_p),
        ]
        lib.HixlTransferAsync.restype = c_uint32

        lib.HixlGetTransferStatus.argtypes = [c_void_p, c_void_p, ctypes.POINTER(c_int)]
        lib.HixlGetTransferStatus.restype = c_uint32

        lib.HixlSendNotify.argtypes = [c_void_p, c_char_p, c_char_p, c_char_p, c_int32]
        lib.HixlSendNotify.restype = c_uint32

        lib.HixlGetNotifies.argtypes = [
            c_void_p,
            ctypes.POINTER(ctypes.POINTER(c_char_p)),
            ctypes.POINTER(ctypes.POINTER(c_char_p)),
            ctypes.POINTER(c_int),
        ]
        lib.HixlGetNotifies.restype = c_uint32

        lib.HixlFreeNotifies.argtypes = [
            ctypes.POINTER(c_char_p),
            ctypes.POINTER(c_char_p),
            c_int,
        ]
        lib.HixlFreeNotifies.restype = None

    def __getattr__(self, name):
        lib = self._load_lib()
        return getattr(lib, name)


hixl_lib = _LazyLibLoader()


def get_hixl_lib():
    """Get the HIXL wrapper library handle.

    Returns:
        ctypes.CDLL: The loaded libhixl_wrapper.so library handle.
    """
    return hixl_lib._load_lib()


def is_hixl_lib_loaded():
    """Check if HIXL library is loaded.

    Returns:
        bool: True if library is loaded successfully.
    """
    return hixl_lib._loaded
