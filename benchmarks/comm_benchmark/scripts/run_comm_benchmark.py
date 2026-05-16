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

"""Launch HIXL communication benchmark processes.

Examples (single machine — default):
  python3 run_comm_benchmark.py --type=D2rD --transport=hccs
  python3 run_comm_benchmark.py --type=D2rH --transport=rdma --device_ids=0,1
  python3 run_comm_benchmark.py --pattern=one_to_many --num_targets=4 --type=D2rD

Examples (dual machine):
  # On the target machine (starts target, prints initiator command for peer):
  python3 run_comm_benchmark.py --type=D2rD --transport=hccs --role=target

  # On the initiator machine (copy the printed command, or construct manually):
  python3 run_comm_benchmark.py --type=D2rD --transport=hccs --role=initiator --target-host=10.0.0.1
"""

import argparse
from pathlib import Path
import socket
import subprocess
import sys
import time


# --type → (initiator_memory, target_memory, op_type)
TYPE_MAP = {
    "D2rD": ("device", "device", "write"),
    "rD2D": ("device", "device", "read"),
    "D2rH": ("device", "host",   "write"),
    "rH2D": ("device", "host",   "read"),
    "H2rH": ("host",   "host",   "write"),
    "rH2H": ("host",   "host",   "read"),
    "H2rD": ("host",   "device", "write"),
    "rD2H": ("host",   "device", "read"),
}

# Default block size range
DEFAULT_START_BLOCK = 16384      # 16K
DEFAULT_MAX_BLOCK = 2097152      # 2M


def get_local_ip() -> str:
    """Auto-detect local non-loopback IP by connecting a UDP socket."""
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.settimeout(2)
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except Exception:
        try:
            return socket.gethostbyname(socket.gethostname())
        except Exception:
            return "127.0.0.1"


def find_bench_bin() -> str:
    """Auto-detect hixl_comm_bench binary from build directory."""
    candidates = [
        Path(__file__).resolve().parents[3] / "build" / "benchmarks" / "comm_benchmark" / "hixl_comm_bench",
        Path("build/benchmarks/comm_benchmark/hixl_comm_bench"),
        Path("./hixl_comm_bench"),
    ]
    for p in candidates:
        if p.exists():
            return str(p)
    return "./hixl_comm_bench"


def endpoint(host, port):
    return f"{host}:{port}"


def parse_args():
    parser = argparse.ArgumentParser(description="Run HIXL communication benchmark")
    parser.add_argument("--pattern", choices=["pairwise", "one_to_many", "many_to_one"], default="pairwise")
    parser.add_argument("--num_targets", type=int, default=1)
    parser.add_argument("--num_initiators", type=int, default=1)
    parser.add_argument("--bench_bin", default=None, help="Path to hixl_comm_bench (auto-detected)")
    parser.add_argument("--host", default="127.0.0.1",
                        help="Local host IP used in single-machine mode. "
                             "In dual-machine --role=target mode, used as the advertised IP "
                             "(auto-detected if not set).")
    parser.add_argument("--role", choices=["target", "initiator"], default=None,
                        help="Dual-machine mode: only run this role locally. "
                             "Omit for single-machine mode (start both).")
    parser.add_argument("--target-host", default=None,
                        help="Target host IP. Required when --role=initiator in dual-machine mode. "
                             "Ignored for --role=target.")
    parser.add_argument("--base_hixl_port", type=int, default=16000)
    parser.add_argument("--base_tcp_port", type=int, default=20000)
    parser.add_argument("--device_ids", default="0,1", help="Device IDs for target and initiator(s)")
    parser.add_argument("--type", choices=list(TYPE_MAP.keys()), default="D2rD",
                        help="Transfer direction, e.g. D2rD=Device→remote Device (write)")
    parser.add_argument("--transport", choices=["hccs", "rdma", "fabric_mem"], default="hccs")
    parser.add_argument("--start_block_size", type=int, default=DEFAULT_START_BLOCK,
                        help=f"First block size in bytes (default {DEFAULT_START_BLOCK})")
    parser.add_argument("--max_block_size", type=int, default=DEFAULT_MAX_BLOCK,
                        help=f"Max block size in bytes, doubles each step (default {DEFAULT_MAX_BLOCK})")
    parser.add_argument("--batch_size", type=int, default=1)
    parser.add_argument("--threads", type=int, default=1)
    parser.add_argument("--duration", type=int, default=5)
    parser.add_argument("--output_dir", default="comm_benchmark/output")
    parser.add_argument("--skip_plot", action="store_true")
    return parser.parse_args()


def base_options(args):
    im, tm, op = TYPE_MAP[args.type]
    return [
        f"--metadata=p2p",
        f"--benchmark_group=default",
        f"--output_dir={args.output_dir}",
        f"--initiator_memory={im}",
        f"--target_memory={tm}",
        f"--op_type={op}",
        f"--start_block_size={args.start_block_size}",
        f"--max_block_size={args.max_block_size}",
        f"--start_batch_size={args.batch_size}",
        f"--max_batch_size={args.batch_size}",
        f"--start_threads={args.threads}",
        f"--max_threads={args.threads}",
        f"--duration={args.duration}",
        f"--transport={args.transport}",
    ]


def start_process(cmd):
    print("[INFO]", " ".join(cmd), flush=True)
    return subprocess.Popen(cmd)


def list_result_csvs(output_dir):
    return set(Path(output_dir).glob("comm_result_*.csv"))


def generate_plots(args, csvs):
    if args.skip_plot:
        return
    if not csvs:
        print(f"[WARN] no comm CSV result found in {args.output_dir}", flush=True)
        return
    plot_script = Path(__file__).resolve().parent / "plot_comm_benchmark.py"
    for csv_path in sorted(csvs):
        out_dir = Path(args.output_dir)
        if len(csvs) > 1:
            out_dir = out_dir / csv_path.stem
        cmd = [sys.executable, str(plot_script), "--csv", str(csv_path), f"--output_dir={out_dir}"]
        print("[INFO]", " ".join(map(str, cmd)), flush=True)
        subprocess.run(cmd, check=False)


# ------- Dual-machine: target side -------

def _launch_target(args, bench_bin: str, device_id: int):
    """Start target locally, print the initiator command for the peer machine."""
    local_ip = get_local_ip()
    if args.host != "127.0.0.1":
        local_ip = args.host

    hixl_port = args.base_hixl_port
    tcp_port = args.base_tcp_port

    # Target listens on all interfaces so the remote initiator can reach it
    target_cmd = [
        bench_bin, "--role=target",
        f"--device_id={device_id}",
        f"--local_engine={endpoint('0.0.0.0', hixl_port)}",
        f"--tcp_port={tcp_port}",
        f"--tcp_client_count=1",
        *base_options(args),
    ]
    print(f"\n[DUAL] Starting target on device {device_id}, listening on 0.0.0.0:{hixl_port} ...", flush=True)
    proc = start_process(target_cmd)
    time.sleep(2)

    if proc.poll() is not None:
        print(f"[ERROR] Target exited early with code {proc.returncode}", flush=True)
        return proc.returncode

    # Build the initiator command for the peer
    initiator_script = Path(__file__).resolve()
    im, tm, op = TYPE_MAP[args.type]
    direction = f"{im[0].upper()}2r{tm[0].upper()}" if op == "write" else f"r{tm[0].upper()}2{im[0].upper()}"

    initiator_cmd = [
        sys.executable, str(initiator_script),
        f"--type={args.type}",
        f"--transport={args.transport}",
        f"--role=initiator",
        f"--target-host={local_ip}",
        f"--base_hixl_port={hixl_port}",
        f"--base_tcp_port={tcp_port}",
        f"--device_ids={device_id}",
        f"--duration={args.duration}",
        f"--output_dir={args.output_dir}",
    ]

    sep = "=" * 60
    print(f"\n{sep}", flush=True)
    print(f"  Target ready.  Local  IP: {local_ip}", flush=True)
    print(f"  HIXL port: {hixl_port},  TCP coord port: {tcp_port}", flush=True)
    print(f"  Direction: {direction},  device: {device_id}", flush=True)
    print(f"  Run this on the INITIATOR machine:", flush=True)
    print(f"{sep}", flush=True)
    print(f"  {' '.join(initiator_cmd)}", flush=True)
    print(f"{sep}\n", flush=True)

    print("[DUAL] Waiting for remote initiator to connect and finish...", flush=True)
    ret = proc.wait()
    after_csvs = list_result_csvs(args.output_dir)
    generate_plots(args, after_csvs - set())
    return ret


# ------- Dual-machine: initiator side -------

def _launch_initiator(args, bench_bin: str, device_id: int):
    """Start initiator locally, connecting to a remote target."""
    if not args.target_host:
        print("[ERROR] --target-host is required when --role=initiator", flush=True)
        return -1

    hixl_port = args.base_hixl_port
    tcp_port = args.base_tcp_port

    im, tm, op = TYPE_MAP[args.type]
    direction = f"{im[0].upper()}2r{tm[0].upper()}" if op == "write" else f"r{tm[0].upper()}2{im[0].upper()}"
    print(f"\n[DUAL] Connecting to target at {args.target_host}:{hixl_port}  "
          f"(direction={direction}, device={device_id})", flush=True)

    initiator_cmd = [
        bench_bin, "--role=initiator",
        f"--device_id={device_id}",
        f"--local_engine={endpoint('127.0.0.1', hixl_port + 1)}",
        f"--remote_engine={endpoint(args.target_host, hixl_port)}",
        f"--tcp_port={tcp_port}",
        *base_options(args),
    ]

    before_csvs = list_result_csvs(args.output_dir)
    proc = start_process(initiator_cmd)
    ret = proc.wait()
    after_csvs = list_result_csvs(args.output_dir)
    generate_plots(args, after_csvs - before_csvs)
    return ret


# ------- Single-machine mode -------

def _launch_single(args, bench_bin: str, devices: list[int]):
    """Single-machine mode: start both target and initiator locally."""
    before_csvs = list_result_csvs(args.output_dir)
    target_count = args.num_targets if args.pattern != "many_to_one" else 1
    initiator_count = args.num_initiators if args.pattern != "one_to_many" else 1
    target_endpoints = [endpoint(args.host, args.base_hixl_port + i) for i in range(target_count)]
    tcp_ports = [args.base_tcp_port + i for i in range(target_count)]
    procs = []

    for idx, target in enumerate(target_endpoints):
        procs.append(
            start_process(
                [bench_bin, "--role=target",
                 f"--device_id={devices[idx % len(devices)]}",
                 f"--local_engine={target}",
                 f"--tcp_port={tcp_ports[idx]}",
                 f"--tcp_client_count={initiator_count}",
                 *base_options(args)])
        )
    time.sleep(2)

    for idx in range(initiator_count):
        if args.pattern == "one_to_many":
            remotes = ",".join(target_endpoints)
            ports = ",".join(str(p) for p in tcp_ports)
        else:
            remotes = target_endpoints[0]
            ports = str(tcp_ports[0])
        procs.append(
            start_process(
                [bench_bin, "--role=initiator",
                 f"--device_id={devices[(target_count + idx) % len(devices)]}",
                 f"--local_engine={endpoint(args.host, args.base_hixl_port + target_count + idx)}",
                 f"--remote_engine={remotes}",
                 f"--tcp_port={ports}",
                 *base_options(args)])
        )

    ret = max(proc.wait() for proc in procs)
    after_csvs = list_result_csvs(args.output_dir)
    generate_plots(args, after_csvs - before_csvs)
    return ret


# ------- Main entry -------

def launch(args):
    bench_bin = args.bench_bin or find_bench_bin()
    if not Path(bench_bin).exists():
        print(f"[ERROR] bench binary not found: {bench_bin}", flush=True)
        print("[ERROR] Build with: bash build.sh --examples", flush=True)
        return -1

    devices = [int(x) for x in args.device_ids.split(",") if x]

    if args.role == "target":
        return _launch_target(args, bench_bin, devices[0])
    elif args.role == "initiator":
        return _launch_initiator(args, bench_bin, devices[0])
    else:
        return _launch_single(args, bench_bin, devices)


if __name__ == "__main__":
    sys.exit(launch(parse_args()))
