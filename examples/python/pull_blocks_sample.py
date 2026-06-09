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

import torch
import torch.distributed as dist
from llm_datadist import BlocksCacheKey, Cache, CacheDesc, DataType, LLMRole, Placement
from pull_sample_common import init_llm_datadist, init_process_group, link

logging.basicConfig(format="%(asctime)s %(message)s", level=logging.INFO)


def _allocate_cpu_cache(block_size, num_block, num_tensors):
    cpu_addrs = []
    cpu_tensors = []
    for _ in range(num_tensors):
        kv_tensor = torch.rand(
            size=(num_block, block_size), dtype=torch.float32, device="cpu"
        )
        cpu_addrs.append(kv_tensor.data_ptr())
        cpu_tensors.append(kv_tensor)
    cpu_cache_desc = CacheDesc(
        num_tensors=num_tensors,
        shape=[num_block, block_size],
        data_type=DataType.DT_FLOAT,
        placement=Placement.HOST,
    )
    return Cache.create_cpu_cache(cpu_cache_desc, cpu_addrs), cpu_tensors


def run_decoder_sample(datadist, device_id: int, is_single: bool, host_ip: str):
    cache_manager = datadist.cache_manager
    cache_desc = CacheDesc(
        num_tensors=1,
        shape=[2, 1024 * 1024],
        data_type=DataType.DT_FLOAT,
        placement=Placement.DEVICE,
    )
    tensor = torch.ones(2, 1024 * 1024, dtype=torch.float).npu()
    addr = int(tensor.data_ptr())
    cache = cache_manager.register_blocks_cache(cache_desc, [addr])
    logging.info("[register_blocks_cache] success")

    comm_id = link(datadist, device_id, is_single, host_ip)

    dist.barrier()  # cache ready
    cache_manager.pull_blocks(
        BlocksCacheKey(1, 0), cache, src_blocks=[0, 1], dst_blocks=[0, 1]
    )
    logging.info(f"after pull, tensor={tensor.cpu()}")
    # swap blocks
    cpu_cache, cpu_tensors = _allocate_cpu_cache(1024 * 1024, 2, 1)
    # swap out
    cache_manager.swap_blocks(cache, cpu_cache, {0: 0, 1: 1})
    # swap in
    cache_manager.swap_blocks(cpu_cache, cache, {0: 0, 1: 1})

    dist.barrier()  # pull_blocks end
    datadist.unlink(comm_id)
    dist.barrier()  # wait peer unlink end
    datadist.finalize()


def run_prompt_sample(datadist, device_id: int, is_single: bool, host_ip: str):
    cache_manager = datadist.cache_manager
    cache_desc = CacheDesc(
        num_tensors=1,
        shape=[2, 1024 * 1024],
        data_type=DataType.DT_FLOAT,
        placement=Placement.DEVICE,
    )
    tensor = torch.ones(2, 1024 * 1024, dtype=torch.float).npu()
    addr = int(tensor.data_ptr())
    _ = cache_manager.register_blocks_cache(cache_desc, [addr], BlocksCacheKey(1, 0))
    logging.info("[register_blocks_cache] success")

    comm_id = link(datadist, device_id, is_single, host_ip)
    dist.barrier()  # cache ready
    dist.barrier()  # decoder pull_blocks end
    datadist.unlink(comm_id)
    dist.barrier()  # wait peer unlink end
    datadist.finalize()
    logging.info("[finalize] success")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--device_id", type=int, default=0, help="device id")
    parser.add_argument("--cluster_id", type=int, default=1, help="cluster id")
    parser.add_argument("--is_single", type=str, help="whether run on a single machine")
    parser.add_argument("--host_ip", type=str, help="host ip")
    args = parser.parse_args()
    if args.cluster_id not in [1, 2]:
        raise RuntimeError("Not supported cluster id")
    if args.device_id not in [0, 1, 2, 3, 4, 5, 6, 7]:
        raise RuntimeError("Not supported device id")
    is_single = False
    if args.is_single:
        is_single = True
    logging.info(
        f"Sample start, device_id = {args.device_id}, cluster_id = {args.cluster_id}"
    )
    torch.npu.set_device(args.device_id)
    role = LLMRole.PROMPT if args.cluster_id == 1 else LLMRole.DECODER
    init_process_group(args.cluster_id, is_single, args.host_ip)
    datadist = init_llm_datadist(
        role, args.cluster_id, args.device_id, enable_mem_pool=False
    )
    if role == LLMRole.PROMPT:
        run_prompt_sample(datadist, args.device_id, is_single, args.host_ip)
    else:
        run_decoder_sample(datadist, args.device_id, is_single, args.host_ip)
    logging.info("Sample end")
