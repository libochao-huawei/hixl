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

import argparse
import logging
import time
import torch
import hixl

logging.basicConfig(format="%(asctime)s %(message)s", level=logging.INFO)

WAIT_REG_TIME = 5
WAIT_TRANS_TIME = 30

TOTAL_SIZE = 1 * 1024 * 1024 * 1024
CHUNK_SIZE = 1 * 1024 * 1024
NUM_CHUNKS = TOTAL_SIZE // CHUNK_SIZE
WARMUP_LOOPS = 1
TEST_LOOPS = 3


def format_size(size_bytes):
    if size_bytes >= 1024 * 1024 * 1024:
        return f"{size_bytes / (1024 * 1024 * 1024):.2f} GiB"
    if size_bytes >= 1024 * 1024:
        return f"{size_bytes / (1024 * 1024):.2f} MiB"
    if size_bytes >= 1024:
        return f"{size_bytes / 1024:.2f} KiB"
    return f"{size_bytes} Bytes"


def run_server(device_id, local_engine, transfer_mode):
    logging.info("Server start")

    torch.npu.set_device(device_id)

    api = hixl.Hixl()
    api.initialize(local_engine)
    logging.info("Initialize success")

    if transfer_mode == "d2d":
        mem_type = hixl.MemType.DEVICE
        buffer = torch.zeros(TOTAL_SIZE, dtype=torch.uint8).npu()
    else:
        mem_type = hixl.MemType.HOST
        buffer = torch.zeros(TOTAL_SIZE, dtype=torch.uint8, pin_memory=True)

    addr = int(buffer.data_ptr())

    handle_id = api.register_mem(hixl.MemDesc(addr=addr, len=TOTAL_SIZE), mem_type)
    logging.info(
        f"RegisterMem success, addr: {hex(addr)}, size: {format_size(TOTAL_SIZE)}"
    )

    with open("./tmp_benchmark", "w") as f:
        f.write(f"{addr}\n")

    logging.info(f"Waiting {WAIT_TRANS_TIME}s for client transfer")
    time.sleep(WAIT_TRANS_TIME)

    api.deregister_mem(handle_id)
    api.finalize()
    logging.info("Finalize success")
    logging.info("Server end")


def run_client(
    device_id, local_engine, remote_engine, transfer_mode, transfer_op, loops
):
    logging.info("Client start")

    torch.npu.set_device(device_id)

    api = hixl.Hixl()
    api.initialize(local_engine)
    logging.info("Initialize success")

    if transfer_mode in ("d2d", "d2h"):
        local_mem_type = hixl.MemType.DEVICE
        local_buffer = torch.zeros(TOTAL_SIZE, dtype=torch.uint8).npu()
    else:
        local_mem_type = hixl.MemType.HOST
        local_buffer = torch.zeros(TOTAL_SIZE, dtype=torch.uint8, pin_memory=True)

    local_addr = int(local_buffer.data_ptr())

    handle_id = api.register_mem(
        hixl.MemDesc(addr=local_addr, len=TOTAL_SIZE), local_mem_type
    )
    logging.info(
        f"RegisterMem success, local addr: {hex(local_addr)}, size: {format_size(TOTAL_SIZE)}"
    )

    logging.info(f"Waiting {WAIT_REG_TIME}s for server registration")
    time.sleep(WAIT_REG_TIME)

    api.connect(remote_engine)
    logging.info("Connect success")

    with open("./tmp_benchmark", "r") as f:
        remote_addr = int(f.read().strip())

    op = hixl.TransferOp.WRITE if transfer_op == "write" else hixl.TransferOp.READ

    op_descs = []
    for i in range(NUM_CHUNKS):
        chunk_local_addr = local_addr + i * CHUNK_SIZE
        chunk_remote_addr = remote_addr + i * CHUNK_SIZE
        op_descs.append(
            hixl.TransferOpDesc(
                local_addr=chunk_local_addr,
                remote_addr=chunk_remote_addr,
                len=CHUNK_SIZE,
            )
        )

    logging.info(
        f"Benchmark config: total_size={format_size(TOTAL_SIZE)}, "
        f"chunk_size={format_size(CHUNK_SIZE)}, num_chunks={NUM_CHUNKS}, "
        f"transfer_mode={transfer_mode}, transfer_op={transfer_op}, loops={loops}"
    )

    total_loops = WARMUP_LOOPS + loops
    results = []

    for loop_idx in range(total_loops):
        if loop_idx < WARMUP_LOOPS:
            logging.info(f"Warmup loop {loop_idx + 1}/{WARMUP_LOOPS}")
        else:
            logging.info(f"Test loop {loop_idx - WARMUP_LOOPS + 1}/{loops}")

        t_start = time.perf_counter()
        api.transfer_sync(remote_engine, op, op_descs, timeout=60000)
        t_end = time.perf_counter()

        elapsed_us = (t_end - t_start) * 1e6
        throughput_gbps = TOTAL_SIZE / (t_end - t_start) / 1e9

        logging.info(
            f"Transfer loop {loop_idx + 1}/{total_loops}: "
            f"total_size={format_size(TOTAL_SIZE)}, "
            f"chunk_size={format_size(CHUNK_SIZE)}, "
            f"num_chunks={NUM_CHUNKS}, "
            f"elapsed={elapsed_us:.2f} us, "
            f"throughput={throughput_gbps:.3f} GB/s"
        )

        if loop_idx >= WARMUP_LOOPS:
            results.append((elapsed_us, throughput_gbps))

    if results:
        avg_time_us = sum(r[0] for r in results) / len(results)
        avg_throughput = sum(r[1] for r in results) / len(results)
        min_throughput = min(r[1] for r in results)
        max_throughput = max(r[1] for r in results)

        logging.info(
            f"Summary: avg_time={avg_time_us:.2f} us, "
            f"avg_throughput={avg_throughput:.3f} GB/s, "
            f"min_throughput={min_throughput:.3f} GB/s, "
            f"max_throughput={max_throughput:.3f} GB/s"
        )

    api.disconnect(remote_engine)
    logging.info("Disconnect success")

    api.deregister_mem(handle_id)
    api.finalize()
    logging.info("Finalize success")
    logging.info("Client end")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="HIXL Transfer Bandwidth Benchmark")
    parser.add_argument("device_id", type=int, help="device id")
    parser.add_argument("local_engine", type=str, help="local engine address (ip:port)")
    parser.add_argument(
        "remote_engine",
        type=str,
        nargs="?",
        default=None,
        help="remote engine address (ip:port) - only for client",
    )
    parser.add_argument(
        "--transfer_mode",
        type=str,
        default="d2d",
        choices=["d2d", "h2d", "d2h", "h2h"],
        help="transfer mode: d2d, h2d, d2h, h2h",
    )
    parser.add_argument(
        "--transfer_op",
        type=str,
        default="write",
        choices=["read", "write"],
        help="transfer operation: read or write (client side)",
    )
    parser.add_argument(
        "--loops",
        type=int,
        default=TEST_LOOPS,
        help=f"number of test loops (default {TEST_LOOPS}, excluding {WARMUP_LOOPS} warmup)",
    )
    args = parser.parse_args()

    if args.device_id not in range(8):
        raise RuntimeError(f"Not supported device id: {args.device_id}")

    logging.info(
        f"Benchmark start, device_id={args.device_id}, "
        f"local_engine={args.local_engine}, transfer_mode={args.transfer_mode}"
    )

    try:
        if args.remote_engine:
            logging.info(f"Running as client, remote_engine={args.remote_engine}")
            run_client(
                args.device_id,
                args.local_engine,
                args.remote_engine,
                args.transfer_mode,
                args.transfer_op,
                args.loops,
            )
        else:
            logging.info("Running as server")
            run_server(args.device_id, args.local_engine, args.transfer_mode)
    except hixl.HixlException as e:
        logging.error(f"HIXL error: {e}")
        raise
    except Exception as e:
        logging.error(f"Unexpected error: {e}")
        raise
