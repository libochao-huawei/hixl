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

"""Run all HIXL benchmarks (comm + KV) and generate perf.md.

Detects A2/A3 platform, runs all valid communication and KV cache benchmarks,
generates perf.md directly from CSV outputs, and prints results to terminal.

Usage:
  python3 benchmarks/run_all_benchmarks.py
  python3 benchmarks/run_all_benchmarks.py --duration 10 --device_ids 0,1,2,3,4,5,6,7
  python3 benchmarks/run_all_benchmarks.py --skip-kv
  python3 benchmarks/run_all_benchmarks.py --target-host=192.168.1.100
"""

import argparse
import subprocess
import sys
import time
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parents[1]
BENCHMARKS_DIR = Path(__file__).resolve().parent
RENDER_SCRIPT = BENCHMARKS_DIR / "performance" / "render_perf_md.py"
KV_BENCH_SCRIPT = BENCHMARKS_DIR / "kv_benchmark" / "scripts" / "run_kv_benchmark.py"

MEM_TYPES = ["device", "host"]
OP_TYPES = ["write", "read"]
TRANSPORTS_A2 = ["hccs", "rdma"]
TRANSPORTS_A3 = ["hccs", "rdma", "fabric_mem"]
BLOCK_SIZES = [16 * 1024, 32 * 1024, 64 * 1024, 128 * 1024, 256 * 1024,
               512 * 1024, 1 * 1024 * 1024, 2 * 1024 * 1024]

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
    for rel in rel_paths:
        p = PROJECT_ROOT / "build" / "benchmarks" / rel
        if p.exists():
            return str(p)
    return f"./{name}"


# ------- Comm benchmark --------

def _make_cmd(bench_bin: str, role: str, transport: str, initiator_mem: str,
              target_mem: str, op_type: str, duration: int, output_dir: Path,
              hixl_port: int, tcp_port: int, device_id: int,
              target_host: str = "127.0.0.1") -> list[str]:
    """Build command line for target or initiator."""
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

    if role == "target":
        return [bench_bin, "--role=target",
                f"--device_id={device_id}",
                f"--local_engine=0.0.0.0:{hixl_port}",
                f"--tcp_port={tcp_port}",
                *base_args]
    else:
        remote = target_host if target_host else "127.0.0.1"
        return [bench_bin, "--role=initiator",
                f"--device_id={device_id}",
                f"--local_engine=127.0.0.1:{hixl_port + 1}",
                f"--remote_engine={remote}:{hixl_port}",
                f"--tcp_port={tcp_port}",
                *base_args]


def run_combo(bench_bin: str, transport: str, initiator_mem: str,
              target_mem: str, op_type: str, duration: int,
              output_dir: Path, hixl_port: int, tcp_port: int,
              device_id0: int, device_id1: int,
              target_host: str = "127.0.0.1") -> bool:
    """Run one target+initiator pair. Returns True on success."""
    output_dir.mkdir(parents=True, exist_ok=True)

    server_cmd = _make_cmd(bench_bin, "target", transport, initiator_mem,
                           target_mem, op_type, duration, output_dir,
                           hixl_port, tcp_port, device_id1)

    client_cmd = _make_cmd(bench_bin, "initiator", transport, initiator_mem,
                           target_mem, op_type, duration, output_dir,
                           hixl_port, tcp_port, device_id0, target_host)

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


# ------- Helpers --------

def _compute_direction(im: str, tm: str, op: str) -> str:
    wr = (op == "write")
    if im == "device" and tm == "device": return "D2rD" if wr else "rD2D"
    if im == "device" and tm == "host":   return "D2rH" if wr else "rH2D"
    if im == "host"   and tm == "host":   return "H2rH" if wr else "rH2H"
    if im == "host"   and tm == "device": return "H2rD" if wr else "rD2H"
    return "unknown"


def _hccs_supports_combo(initiator_mem: str, target_mem: str) -> bool:
    return initiator_mem == "device" and target_mem == "device"


# ------- Main --------

def main() -> None:
    parser = argparse.ArgumentParser(description="Run all HIXL benchmarks")
    parser.add_argument("--duration", type=int, default=5)
    parser.add_argument("--device_ids", type=str, default="0,1,2,3,4,5,6,7",
                        help="Device IDs (default: 0-7). First 2 used for comm, all used for KV.")
    parser.add_argument("--base_hixl_port", type=int, default=17000)
    parser.add_argument("--base_tcp_port", type=int, default=21000)
    parser.add_argument("--target_host", type=str, default="127.0.0.1",
                        help="Remote target host IP for dual-machine mode (default: 127.0.0.1)")
    parser.add_argument("--skip_comm", action="store_true", help="Skip communication benchmarks")
    parser.add_argument("--skip_kv", action="store_true", help="Skip KV benchmarks")
    args = parser.parse_args()

    devs = [int(x.strip()) for x in args.device_ids.split(",")]
    if len(devs) < 2:
        print("[ERROR] Need at least 2 device IDs")
        sys.exit(1)

    platform_id = detect_platform()
    transports = TRANSPORTS_A3 if platform_id == "atlas_a3" else TRANSPORTS_A2
    print(f"[INFO] Platform: {platform_id}, transports: {transports}")
    if args.target_host != "127.0.0.1":
        print(f"[INFO] Dual-machine mode: target on {args.target_host}")

    # ---- Communication benchmarks ----
    comm_ok = 0
    comm_total = 0
    if not args.skip_comm:
        comm_bin = find_binary("hixl_comm_bench", "comm_benchmark/hixl_comm_bench")
        if not Path(comm_bin).exists():
            print(f"[ERROR] comm bench binary not found: {comm_bin}")
            print("[ERROR] Build with: bash build.sh --examples")
            sys.exit(1)

        comm_output = BENCHMARKS_DIR / "comm_benchmark" / "output"
        for old_csv in comm_output.glob("comm_result_*.csv"):
            old_csv.unlink()

        port_offset = 0
        for transport in transports:
            for im in MEM_TYPES:
                for tm in MEM_TYPES:
                    for op in OP_TYPES:
                        direction = _compute_direction(im, tm, op)
                        if transport == "hccs" and not _hccs_supports_combo(im, tm):
                            print(
                                f"[SKIP] {direction} transport=hccs not supported "
                                f"(HCCS is D2D-only; use rdma/fabric_mem)"
                            )
                            continue
                        comm_total += 1
                        hixl_port = args.base_hixl_port + port_offset * 2
                        tcp_port = args.base_tcp_port + port_offset
                        port_offset += 1
                        if run_combo(comm_bin, transport, im, tm, op, args.duration,
                                     comm_output, hixl_port, tcp_port, devs[0], devs[1],
                                     args.target_host):
                            comm_ok += 1
                        time.sleep(1)

        print(f"\n[INFO] Comm: {comm_ok}/{comm_total} runs succeeded")

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
        print("[WARN] No successful benchmarks, skipping perf.md generation")
        return

    # ---- Render perf.md from CSVs ----
    comm_csv_dir = BENCHMARKS_DIR / "comm_benchmark" / "output"
    if comm_ok > 0 and list(comm_csv_dir.glob("comm_result_*.csv")):
        print("\n[INFO] Generating perf.md from CSVs...")
        render_cmd = [
            sys.executable, str(RENDER_SCRIPT),
            f"--csv-dir={comm_csv_dir}",
            f"--platform={platform_id}",
        ]
        ret = subprocess.run(render_cmd, check=False)
        if ret.returncode != 0:
            print("[WARN] render_perf_md.py failed")

    print("\n[DONE]")


if __name__ == "__main__":
    main()
