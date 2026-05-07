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

"""Launch the HIXL KV benchmark processes."""

import argparse
from pathlib import Path
import subprocess
import sys


def parse_args():
    parser = argparse.ArgumentParser(description="Run HIXL KV benchmark")
    parser.add_argument("--num_processes", type=int, default=8)
    parser.add_argument("--devices", default="0,1,2,3,4,5,6,7")
    parser.add_argument("--bench_bin", default="./hixl_kv_bench")
    parser.add_argument("--output_dir", default="kv_benchmark/output")
    parser.add_argument("--segment_size", default="10G")
    parser.add_argument("--pool_memory", choices=["host"], default="host")
    parser.add_argument("--model", default="deepseek-r1")
    parser.add_argument("--model_config", default="kv_benchmark/config/models.json")
    parser.add_argument("--token_lengths", default="16K,32K,64K,128K")
    parser.add_argument("--batch_size", type=int, default=128)
    parser.add_argument("--op_type", choices=["put", "get", "put_get"], default="put_get")
    parser.add_argument("--seed", type=int, default=1234)
    parser.add_argument("--transport", choices=["hccs", "rdma", "fabric_mem"], default="fabric_mem")
    parser.add_argument("--skip_plot", action="store_true", help="Skip automatic plot generation")
    return parser.parse_args()


def generate_plots(args):
    if args.skip_plot:
        return
    csv_path = Path(args.output_dir) / "kv_result_rank0.csv"
    if not csv_path.exists():
        print(f"[WARN] {csv_path} does not exist; skip plot generation", flush=True)
        return
    plot_script = Path(__file__).resolve().parent / "plot_kv_benchmark.py"
    cmd = [sys.executable, str(plot_script), str(csv_path), f"--output_dir={args.output_dir}"]
    print("[INFO]", " ".join(cmd), flush=True)
    ret = subprocess.run(cmd, check=False)
    if ret.returncode != 0:
        print(f"[WARN] plot generation failed for {csv_path}; keep benchmark result", flush=True)


def main():
    args = parse_args()
    devices = [x for x in args.devices.split(",") if x]
    if len(devices) < args.num_processes:
        print("[ERROR] --devices count must be >= --num_processes", file=sys.stderr)
        return 1
    print(
        "[INFO] model=%s model_config=%s token_lengths=%s segment_size=%s transport=%s output_dir=%s"
        % (args.model, args.model_config, args.token_lengths, args.segment_size, args.transport, args.output_dir),
        flush=True,
    )
    procs = []
    for rank in range(args.num_processes):
        cmd = [
            args.bench_bin,
            f"--rank={rank}",
            f"--device_id={devices[rank]}",
            f"--num_processes={args.num_processes}",
            f"--segment_size={args.segment_size}",
            f"--pool_memory={args.pool_memory}",
            f"--model={args.model}",
            f"--model_config={args.model_config}",
            f"--token_lengths={args.token_lengths}",
            f"--batch_size={args.batch_size}",
            f"--op_type={args.op_type}",
            f"--seed={args.seed}",
            f"--transport={args.transport}",
            f"--output_dir={args.output_dir}",
        ]
        print("[INFO]", " ".join(cmd), flush=True)
        procs.append(subprocess.Popen(cmd))
    ret = max(proc.wait() for proc in procs)
    generate_plots(args)
    return ret


if __name__ == "__main__":
    sys.exit(main())
