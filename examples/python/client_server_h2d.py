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

import argparse
import logging
import time
import torch
from hixl import Hixl, MemType, TransferOp, HixlException

logging.basicConfig(format="%(asctime)s %(message)s", level=logging.INFO)

WAIT_REG_TIME = 5
WAIT_TRANS_TIME = 20
SRC_VALUE = 2


def run_server(device_id: int, local_engine: str):
    logging.info("server start")

    torch.npu.set_device(device_id)

    hixl = Hixl()
    hixl.initialize(local_engine)
    logging.info("Initialize success")

    dst = 1
    buffer = torch.zeros(1, dtype=torch.int32).npu()
    buffer.fill_(dst)

    addr = int(buffer.data_ptr())
    length = 4

    handle_id = hixl.register_mem(addr, length, MemType.DEVICE)
    logging.info(f"RegisterMem success, dst addr: {hex(addr)}")

    with open("./tmp", "w") as f:
        f.write(f"{addr}\n")

    logging.info(f"Waiting {WAIT_TRANS_TIME}s for client transfer")
    time.sleep(WAIT_TRANS_TIME)

    dst_value = buffer.item()
    logging.info(f"After transfer, dst value: {dst_value}")

    hixl.deregister_mem(handle_id)
    hixl.finalize()
    logging.info("Finalize success")
    logging.info("server Sample end")


def run_client(device_id: int, local_engine: str, remote_engine: str):
    logging.info("client start")

    torch.npu.set_device(device_id)

    hixl = Hixl()
    hixl.initialize(local_engine)
    logging.info("Initialize success")

    src = torch.zeros(1, dtype=torch.int32, pin_memory=True)

    local_addr = int(src.data_ptr())
    length = 4

    handle_id = hixl.register_mem(local_addr, length, MemType.HOST)
    logging.info("RegisterMem success")

    logging.info(f"Waiting {WAIT_REG_TIME}s for server registration")
    time.sleep(WAIT_REG_TIME)

    hixl.connect(remote_engine)
    logging.info("Connect success")

    with open("./tmp", "r") as f:
        remote_addr = int(f.read().strip())

    op_descs = [(local_addr, remote_addr, length)]

    hixl.transfer_sync(remote_engine, TransferOp.READ, op_descs)
    logging.info(f"TransferSync read success, src = {src.item()}")

    src.fill_(SRC_VALUE)
    hixl.transfer_sync(remote_engine, TransferOp.WRITE, op_descs)
    logging.info(f"TransferSync write success, src = {src.item()}")

    hixl.disconnect(remote_engine)
    logging.info("Disconnect success")

    hixl.deregister_mem(handle_id)
    hixl.finalize()
    logging.info("Finalize success")
    logging.info("client Sample end")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="HIXL Client-Server H2D Sample")
    parser.add_argument("device_id", type=int, help="device id")
    parser.add_argument("local_engine", type=str, help="local engine address (ip:port)")
    parser.add_argument(
        "remote_engine",
        type=str,
        nargs="?",
        default=None,
        help="remote engine address (ip:port) - only for client",
    )
    args = parser.parse_args()

    if args.device_id not in range(8):
        raise RuntimeError(f"Not supported device id: {args.device_id}")

    logging.info(
        f"Sample start, device_id={args.device_id}, local_engine={args.local_engine}"
    )

    try:
        if args.remote_engine:
            logging.info(f"Running as client, remote_engine={args.remote_engine}")
            run_client(args.device_id, args.local_engine, args.remote_engine)
        else:
            logging.info("Running as server")
            run_server(args.device_id, args.local_engine)
    except HixlException as e:
        logging.error(f"HIXL error: {e}")
        raise
    except Exception as e:
        logging.error(f"Unexpected error: {e}")
        raise
