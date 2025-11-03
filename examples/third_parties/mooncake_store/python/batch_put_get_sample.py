#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# ----------------------------------------------------------------------------
# This program is free software, you can redistribute it and/or modify it.
# Copyright (c) 2025 Huawei Technologies Co., Ltd.
# This file is a part of the CANN Open Software.
# Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

import os
import time
import argparse
import logging
import torch
import torch_npu
import torch.distributed as dist
from mooncake.store import MooncakeDistributedStore, ReplicateConfig

logging.basicConfig(format="%(asctime)s %(message)s", level=logging.INFO)

SEGMENT_SIZE = 1024 * 1024 * 1024
LOCAL_BUFFER = 20 * 1024 * 1024
ALIGNMENT = 2 * 1024 * 1024


def init_mooncake_store(args) -> MooncakeDistributedStore:
    store = MooncakeDistributedStore()
    port = 12345 + int(args.device_id)
    store.setup(
        "127.0.0.1:" + str(port),
        "http://localhost:8080/metadata",
        SEGMENT_SIZE,
        LOCAL_BUFFER,
        "ascend",
        "",
        "localhost:50051"
    )
    return store


def init_process_group(args):
    os.environ["MASTER_ADDR"] = "127.0.0.1"
    os.environ["MASTER_PORT"] = "29500"
    dist.init_process_group("gloo", rank=args.device_id % 2, world_size=2)
    dist.barrier(group=dist.group.WORLD)


def run_batch_put_get_sample(store: MooncakeDistributedStore, args):
    if args.schema.startswith("h"):
        tensor = torch.ones(33, 61, 144 * 1024, dtype=torch.int8, pin_memory=True).cpu()
    else:
        tensor = torch.ones(33, 61, 144 * 1024, dtype=torch.int8).npu()
    if args.schema.endswith("h"):
        target_tensor = torch.zeros(33, 61, 144 * 1024, dtype=torch.int8, pin_memory=True).cpu()
    else:
        target_tensor = torch.zeros(33, 61, 144 * 1024, dtype=torch.int8).npu()
    data_ptr = tensor.data_ptr()
    addr = (data_ptr + ALIGNMENT - 1) // ALIGNMENT * ALIGNMENT
    logging.info(f"dataptr:{data_ptr}, addr:{addr}")
    store.register_buffer(addr, 61 * 32 * 144 * 1024)

    target_data_ptr = target_tensor.data_ptr()
    remote_addr = (target_data_ptr + ALIGNMENT - 1) // ALIGNMENT * ALIGNMENT
    logging.info(f"dataptr:{target_data_ptr}, addr:{remote_addr}")
    store.register_buffer(remote_addr, 61 * 32 * 144 * 1024)

    for block_i in range(32):
        local_addrs = []
        remote_addrs = []
        sizes = []
        keys = []
        get_keys = []
        for layer in range(61):
            keys.append("hello_source" + "_" + str(block_i) + "_" + str(layer))
            get_keys.append("hello_target" + "_" + str(block_i) + "_" + str(layer))
            local_addrs.append(addr)
            remote_addrs.append(remote_addr)
            sizes.append(144 * 1024)
            addr += 144 * 1024
            remote_addr += 144 * 1024

        store.batch_put_from(keys, local_addrs, sizes)
        time.sleep(0.5)
        results = store.batch_get_into(get_keys, remote_addrs, sizes)
        for key, result in zip(get_keys, results):
            if result > 0:
                logging.info(f"Retrieved {key} : {result} bytes")
            else:
                logging.info(f"Failed to retrieve {key}: error {result}")
    store.close()


if __name__ == "__main__":
    supported_schema = ["h2h", "h2d", "d2h", "d2d"]
    parser = argparse.ArgumentParser()
    parser.add_argument("--device_id", type=int, default=0, help="device id")
    parser.add_argument("--schema", type=str, default="d2d",
                        help="transport schema, should in ['h2h', 'h2d', 'd2h', 'd2d']")
    args = parser.parse_args()
    args.schema = args.schema.lower()

    if args.schema not in supported_schema:
        raise RuntimeError(f"Unsupported Schema: {args.schema}")
    torch.npu.set_device(args.device_id)
    logging.info(f"Running on device: {args.device_id}")

    init_process_group(args)
    mooncake_store = init_mooncake_store(args)

    run_batch_put_get_sample(mooncake_store, args)
    logging.info("Sample finish.")

