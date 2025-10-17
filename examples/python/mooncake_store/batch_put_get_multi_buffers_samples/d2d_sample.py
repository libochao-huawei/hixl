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

import torch
import torch_npu
import sys
import os
import time
from mooncake.store import MooncakeDistributedStore, ReplicateConfig
import torch.distributed as dist

SEGMENT_SIZE = 1024 * 1024 * 1024
LOCAL_BUFFER = 20 * 1024 * 1024
ALIGNMENT = 2 * 1024 * 1024

dev_id = sys.argv[1]
print("device:", dev_id)
torch.npu.set_device(int(dev_id))
rank = int(dev_id) % 2

# 初始化Mooncake Store
store = MooncakeDistributedStore()
port = 12345 + int(dev_id)
store.setup(
    "127.0.0.1:"+str(port),
    "http://localhost:8080/metadata",
    SEGMENT_SIZE,
    LOCAL_BUFFER,
    "ascend",
    "",
    "localhost:50051"
)

os.environ["MASTER_ADDR"] = '127.0.0.1'
os.environ["MASTER_PORT"] = '29500'
dist.init_process_group("gloo", rank=int(int(dev_id) % 2), world_size=2)
dist.barrier(group=dist.group.WORLD)

tensor = torch.ones(33, 61, 144 * 1024, dtype=torch.int8).npu()
data_ptr = tensor.data_ptr()
addr = (data_ptr + ALIGNMENT - 1) // ALIGNMENT * ALIGNMENT
print(f"dataptr:{data_ptr}, addr:{addr}")
store.register_buffer(addr, 61 * 32 * 144 * 1024)

target_tensor = torch.zeros(33, 61, 144 * 1024, dtype=torch.int8).npu()
target_data_ptr = target_tensor.data_ptr()
remote_addr = (target_data_ptr + ALIGNMENT - 1) // ALIGNMENT * ALIGNMENT
print(f"dataptr:{target_data_ptr}, addr:{remote_addr}")
store.register_buffer(remote_addr, 61 * 32 * 144 * 1024)

all_local_addrs = []
all_remote_addrs = []
all_sizes = []
keys = []
for block_i in range(32):
    local_addrs = []
    remote_addrs = []
    sizes = []
    for _ in range(61):
        local_addrs.append(addr)
        remote_addrs.append(remote_addr)
        sizes.append(128 * 1024)
        addr += 128 * 1024
        remote_addr += 128 * 1024

        local_addrs.append(addr)
        remote_addrs.append(remote_addr)
        sizes.append(16 * 1024)
        addr += 16 * 1024
        remote_addr += 16 * 1024

    all_local_addrs.append(local_addrs)
    all_remote_addrs.append(remote_addrs)
    all_sizes.append(sizes)
    keys.append("hello_" + str(dev_id) + "_" + str(block_i))

config = ReplicateConfig()
config.prefer_alloc_in_same_node = True
store.batch_put_from_multi_buffers(keys, all_local_addrs, all_sizes, config)
store.batch_get_into_multi_buffers(keys, all_remote_addrs, all_sizes, True)

store.unregister_buffer(tensor.data_ptr())
store.unregister_buffer(target_tensor.data_ptr())
store.close()
