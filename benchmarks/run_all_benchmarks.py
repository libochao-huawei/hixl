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

"""Run all HIXL benchmarks (comm + KV) and refresh performance docs.

Detects A2/A3 platform, runs all valid communication and KV cache benchmarks,
merges results into JSON, and re-renders performance Markdown + charts.

Usage:
  python3 benchmarks/run_all_benchmarks.py
  python3 benchmarks/run_all_benchmarks.py --duration 10 --device_ids 0,1,2,3,4,5,6,7
  python3 benchmarks/run_all_benchmarks.py --skip-kv
"""

import argparse
import subprocess
import sys
import time
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parents[1]
BENCHMARKS_DIR = Path(__file__).resolve().parent
PERF_JSON = BENCHMARKS_DIR / "performance" / "communication_performance.json"
RENDER_SCRIPT = BENCHMARKS_DIR / "performance" / "render_performance_md.py"
MERGE_SCRIPT = BENCHMARKS_DIR / "performance" / "merge_comm_csv_into_performance.py"
PLOT_SCRIPT = BENCHMARKS_DIR / "comm_benchmark" / "scripts" / "plot_comm_benchmark.py"
KV_BENCH_SCRIPT = BENCHMARKS_DIR / "kv_benchmark" / "scripts" / "run_kv_benchmark.py"

# Communication test matrix
MEM_TYPES = ["device", "host"]
OP_TYPES = ["write", "read"]
TRANSPORTS_A2 = ["hccs", "rdma"]
TRANSPORTS_A3 = ["hccs", "rdma", "fabric_mem"]
BLOCK_SIZES = [16 * 1024, 32 * 1024, 64 * 1024, 128 * 1024, 256 * 1024,
               512 * 1024, 1 * 1024 * 1024, 2 * 1024 * 1024]

# KV models
KV_MODELS = ["deepseek-r1", "glm5"]


# ------- Platform detection --------

def detect_platform() -> str:
    """Detect Ascend platform from npu-smi. Falls back to user input."""
    try:
        result = subprocess.run(["npu-smi", "info", "-t", "board"],
                                capture_output=True, text=True, timeout=15)
        out = result.stdout
        if "Ascend910B" in out:
            print("[INFO] Detected A2 (Ascend910B) from npu-smi")
            return "atlas_a2"
        if "Ascend910" in out:
            print("[INFO] Detected A3 (Ascend910) from npu-smi")
            return "atlas_a3"
    except FileNotFoundError:
        print("[WARN] npu-smi not found")
    except subprocess.TimeoutExpired:
        print("[WARN] npu-smi timed out")
    while True:
        choice = input("Which platform? Enter 'a2' or 'a3': ").strip().lower()
        if choice == "a2":
            return "atlas_a2"
        if choice == "a3":
            return "atlas_a3"
        print("Please enter 'a2' or 'a3'.")


# ------- Binary auto-detection --------

def find_binary(name: str, *rel_paths: str) -> str:
    """Find a compiled binary from build/ directory or PATH."""
    for rel in rel_paths:
        p = PROJECT_ROOT / "build" / "benchmarks" / rel
        if p.exists():
            return str(p)
    # Fallback to PATH / local
    return f"./{name}"


# ------- Comm benchmark --------

def run_combo(bench_bin: str, transport: str, initiator_mem: str,
              target_mem: str, op_type: str, duration: int,
              output_dir: Path, hixl_port: int, tcp_port: int,
              device_id0: int, device_id1: int) -> bool:
    """Run one target+initiator pair. Returns True on success."""
    output_dir.mkdir(parents=True, exist_ok=True)
    start_bs = BLOCK_SIZES[0]
    max_bs = BLOCK_SIZES[-1]

    base_args = [
        f"--transport={transport}",
        f"--initiator_memory={initiator_mem}",
        f"--target_memory={target_mem}",
        f"--op_type={op_type}",
        f"--start_block_size={start_bs}",
        f"--max_block_size={max_bs}",
        f"--start_threads=1",
        f"--max_threads=1",
        f"--duration={duration}",
        f"--output_dir={output_dir}",
        "--metadata=p2p",
    ]

    server_cmd = [bench_bin, "--role=target",
                  f"--device_id={device_id1}",
                  f"--local_engine=127.0.0.1:{hixl_port}",
                  f"--tcp_port={tcp_port}",
                  *base_args]

    client_cmd = [bench_bin, "--role=initiator",
                  f"--device_id={device_id0}",
                  f"--local_engine=127.0.0.1:{hixl_port + 1}",
                  f"--remote_engine=127.0.0.1:{hixl_port}",
                  f"--tcp_port={tcp_port}",
                  *base_args]

    direction = _compute_direction(initiator_mem, target_mem, op_type)
    print(f"\n{'='*60}")
    print(f"[RUN] {direction} transport={transport} initiator={initiator_mem} target={target_mem}")
    print(f"[CMD] server: {' '.join(server_cmd)}")
    print(f"[CMD] client: {' '.join(client_cmd)}")

    server = subprocess.Popen(server_cmd)
    time.sleep(2)

    try:
        client = subprocess.Popen(client_cmd)
        client_rc = client.wait(timeout=300)
        server_rc = server.wait(timeout=10)
    except subprocess.TimeoutExpired:
        print("[FAIL] Timeout — killing processes")
        server.kill()
        server.wait()
        return False

    ok = (server_rc == 0 and client_rc == 0)
    label = "[OK]" if ok else f"[FAIL] server_rc={server_rc} client_rc={client_rc}"
    print(f"{label}   {direction} {transport}")
    return ok


# ------- KV benchmark --------

def run_kv_benchmarks(kv_bin: str, devices: list[int], output_dir: Path,
                      models: list[str], transport: str, duration: int) -> int:
    """Run KV benchmarks via run_kv_benchmark.py for each model. Returns count of successes."""
    ok = 0
    for model in models:
        print(f"\n{'='*60}")
        print(f"[RUN] KV benchmark model={model} transport={transport}")
        cmd = [
            sys.executable, str(KV_BENCH_SCRIPT),
            f"--bench_bin={kv_bin}",
            f"--model={model}",
            f"--transport={transport}",
            f"--num_processes={len(devices)}",
            f"--devices={','.join(str(d) for d in devices)}",
            f"--op_type=put_get",
            f"--output_dir={output_dir}",
        ]
        print(f"[CMD] {' '.join(cmd)}")
        ret = subprocess.run(cmd).returncode
        if ret == 0:
            print(f"[OK]   KV {model}")
            ok += 1
        else:
            print(f"[FAIL] KV {model} returncode={ret}")
        time.sleep(1)
    return ok


# ------- CSV merge --------

def merge_comm_csvs(platform_id: str, csv_dir: Path) -> int:
    """Merge all comm_result_*.csv files into JSON."""
    csvs = sorted(csv_dir.glob("comm_result_*.csv"))
    if not csvs:
        print(f"[WARN] No CSV files found in {csv_dir}")
        return 0

    total = 0
    for csv_path in csvs:
        transport = _detect_field(csv_path, "transport")
        direction = _detect_field(csv_path, "direction")
        if not transport or not direction:
            print(f"[WARN] Cannot determine transport/direction from {csv_path.name}, skipping")
            continue
        cmd = [
            sys.executable, str(MERGE_SCRIPT),
            f"--data={PERF_JSON}",
            f"--csv={csv_path}",
            f"--platform={platform_id}",
            f"--direction={direction}",
            f"--transport={transport}",
        ]
        ret = subprocess.run(cmd, capture_output=True, text=True)
        if ret.returncode == 0:
            print(ret.stdout.strip())
            total += 1
        else:
            print(f"[WARN] merge failed for {csv_path.name}: {ret.stderr.strip()}")
    return total


def _detect_field(csv_path: Path, field: str) -> str | None:
    import csv
    with csv_path.open(newline="", encoding="utf-8") as f:
        for row in csv.DictReader(f):
            v = row.get(field, "").strip()
            if v:
                return v
    return None


# ------- Helpers --------

def _compute_direction(im: str, tm: str, op: str) -> str:
    wr = (op == "write")
    if im == "device" and tm == "device": return "D2rD" if wr else "rD2D"
    if im == "device" and tm == "host":   return "D2rH" if wr else "rH2D"
    if im == "host"   and tm == "host":   return "H2rH" if wr else "rH2H"
    if im == "host"   and tm == "device": return "H2rD" if wr else "rD2H"
    return "unknown"


# ------- Main --------

def main() -> None:
    parser = argparse.ArgumentParser(description="Run all HIXL benchmarks")
    parser.add_argument("--duration", type=int, default=5)
    parser.add_argument("--device_ids", type=str, default="0,1,2,3,4,5,6,7",
                        help="Device IDs (default: 0-7). First 2 used for comm, all used for KV.")
    parser.add_argument("--base_hixl_port", type=int, default=17000)
    parser.add_argument("--base_tcp_port", type=int, default=21000)
    parser.add_argument("--skip_comm", action="store_true", help="Skip communication benchmarks")
    parser.add_argument("--skip_kv", action="store_true", help="Skip KV benchmarks")
    parser.add_argument("--skip_merge", action="store_true", help="Skip CSV→JSON merge step")
    parser.add_argument("--skip_render", action="store_true", help="Skip MD+chart rendering")
    args = parser.parse_args()

    devs = [int(x.strip()) for x in args.device_ids.split(",")]
    if len(devs) < 2:
        print("[ERROR] Need at least 2 device IDs")
        sys.exit(1)

    platform_id = detect_platform()
    transports = TRANSPORTS_A3 if platform_id == "atlas_a3" else TRANSPORTS_A2
    print(f"[INFO] Platform: {platform_id}, transports: {transports}")

    # ---- Communication benchmarks ----
    comm_ok = 0
    if not args.skip_comm:
        comm_bin = find_binary("hixl_comm_bench", "comm_benchmark/hixl_comm_bench")
        if not Path(comm_bin).exists():
            print(f"[ERROR] comm bench binary not found: {comm_bin}")
            print("[ERROR] Build with: bash build.sh --examples")
            sys.exit(1)

        comm_output = BENCHMARKS_DIR / "comm_benchmark" / "output"
        for old_csv in comm_output.glob("comm_result_*.csv"):
            old_csv.unlink()

        total = 0
        port_offset = 0
        for transport in transports:
            for im in MEM_TYPES:
                for tm in MEM_TYPES:
                    for op in OP_TYPES:
                        total += 1
                        hixl_port = args.base_hixl_port + port_offset * 2
                        tcp_port = args.base_tcp_port + port_offset
                        port_offset += 1
                        if run_combo(comm_bin, transport, im, tm, op, args.duration,
                                     comm_output, hixl_port, tcp_port, devs[0], devs[1]):
                            comm_ok += 1
                        time.sleep(1)

        print(f"\n[INFO] Comm: {comm_ok}/{total} runs succeeded")

    # ---- KV benchmarks ----
    kv_ok = 0
    if not args.skip_kv:
        kv_bin = find_binary("hixl_kv_bench", "kv_benchmark/hixl_kv_bench")
        if not Path(kv_bin).exists():
            print(f"[WARN] KV bench binary not found: {kv_bin}, skipping KV benchmarks")
        else:
            kv_output = BENCHMARKS_DIR / "kv_benchmark" / "output"
            kv_ok = run_kv_benchmarks(kv_bin, devs, kv_output, KV_MODELS,
                                      "fabric_mem", args.duration)
            print(f"\n[INFO] KV: {kv_ok}/{len(KV_MODELS)} runs succeeded")

    total_ok = comm_ok + kv_ok
    if total_ok == 0:
        print("[WARN] No successful benchmarks, skipping merge/render")
        return

    # ---- Merge CSV → JSON ----
    if not args.skip_merge and comm_ok > 0:
        print("\n[INFO] Merging CSVs into JSON...")
        merged = merge_comm_csvs(platform_id, BENCHMARKS_DIR / "comm_benchmark" / "output")
        print(f"[INFO] Merged {merged} comm CSVs")

    # ---- Render MD + charts ----
    if not args.skip_render:
        print("\n[INFO] Rendering performance MD + charts...")
        ret = subprocess.run([sys.executable, str(RENDER_SCRIPT)], check=False)
        if ret.returncode != 0:
            print("[WARN] render_performance_md.py failed")

        # Batch charts from JSON
        figures_dir = BENCHMARKS_DIR / "performance" / "figures"
        figures_dir.mkdir(parents=True, exist_ok=True)
        plot_ret = subprocess.run([
            sys.executable, str(PLOT_SCRIPT),
            f"--json={PERF_JSON}",
            f"--platform={platform_id}",
            f"--output_dir={figures_dir}",
        ], check=False)
        if plot_ret.returncode != 0:
            print("[WARN] batch plot generation failed")

    print("\n[DONE]")
    print(f"  JSON: {PERF_JSON}")
    print(f"  MD:   {BENCHMARKS_DIR / 'performance.md'}")


if __name__ == "__main__":
    main()
