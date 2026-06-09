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
from pull_sample_common import (
    CACHE_NUM_TENSORS,
    CACHE_SHAPE,
    init_llm_datadist,
    init_process_group,
    link,
)

from llm_datadist import CacheDesc, CacheKey, DataType, LLMRole, Placement

logging.basicConfig(format="%(asctime)s %(message)s", level=logging.INFO)


def run_decoder_sample(datadist, device_id: int, is_single: bool, host_ip: str):
    cache_manager = datadist.cache_manager
    cache_desc = CacheDesc(
        num_tensors=CACHE_NUM_TENSORS,
        shape=CACHE_SHAPE,
        data_type=DataType.DT_FLOAT16,
        placement=Placement.DEVICE,
    )
    tensors = []
    addrs = []
    for _ in range(CACHE_NUM_TENSORS):
        tensor = torch.ones(*CACHE_SHAPE, dtype=torch.float16).npu()
        addrs.append(tensor.data_ptr())
        tensors.append(tensor)
    cache = cache_manager.register_blocks_cache(cache_desc, addrs)
    logging.info("[register_blocks_cache] success")

    comm_id = link(datadist, device_id, is_single, host_ip)

    dist.barrier()  # cache ready
    cache_key = CacheKey(prompt_cluster_id=1, req_id=0, model_id=0)
    cache_manager.pull_blocks(cache_key, cache, src_blocks=[], dst_blocks=[0])
    logging.info("[pull_blocks] success")

    dist.barrier()  # pull_blocks end
    datadist.unlink(comm_id)
    dist.barrier()  # wait peer unlink end
    datadist.finalize()


def run_prompt_sample(datadist, device_id: int, is_single: bool, host_ip: str):
    comm_id = link(datadist, device_id, is_single, host_ip)
    # 通过cache_manager分配kv cache
    cache_manager = datadist.cache_manager
    cache_desc = CacheDesc(
        num_tensors=CACHE_NUM_TENSORS,
        shape=CACHE_SHAPE,
        data_type=DataType.DT_FLOAT16,
        placement=Placement.DEVICE,
    )
    cache_key = CacheKey(prompt_cluster_id=1, req_id=0, model_id=0)
    cache = cache_manager.allocate_cache(cache_desc, [cache_key])
    logging.info("[allocate_cache] success")

    dist.barrier()  # cache ready
    dist.barrier()  # decoder pull_blocks end
    datadist.unlink(comm_id)
    dist.barrier()  # wait peer unlink end

    # 如果pull_cache失败，或者decoder没有调用pull_cache，此处需要调用remove_cache_key，确保cache能够得到释放
    # 如果pull_cache成功，这里只是个空操作
    cache_manager.remove_cache_key(cache_key)
    logging.info("[remove_cache_key] success")
    cache_manager.deallocate_cache(cache)
    logging.info("[deallocate_cache] success")
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
    datadist = init_llm_datadist(role, args.cluster_id, args.device_id)
    if role == LLMRole.PROMPT:
        run_prompt_sample(datadist, args.device_id, is_single, args.host_ip)
    else:
        run_decoder_sample(datadist, args.device_id, is_single, args.host_ip)
    logging.info("Sample end")
