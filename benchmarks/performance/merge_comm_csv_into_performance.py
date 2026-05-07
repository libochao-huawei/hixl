#!/usr/bin/env python3
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
#
# Merge bandwidth numbers from hixl_comm_bench CSV output into communication_performance.json.
"""Apply comm_result_*.csv throughput into the matching direction+transport entry."""

from __future__ import annotations

import argparse
import csv
import json
import statistics
from pathlib import Path


def block_label_from_bytes(bs: int | str) -> str:
    if isinstance(bs, str):
        bs = int(bs.strip())
    if bs % (1024 * 1024) == 0:
        n = bs // (1024 * 1024)
        return f"{n}M"
    if bs % 1024 == 0:
        n = bs // 1024
        return f"{n}K"
    return str(bs)


def avg_bandwidth_by_block(csv_path: Path, transport: str) -> dict[str, float]:
    """Map block label -> mean bandwidth for the given transport."""
    by_block: dict[str, list[float]] = {}
    with csv_path.open(newline="", encoding="utf-8") as f:
        for row in csv.DictReader(f):
            if row.get("transport") != transport:
                continue
            try:
                bw = float(row["bandwidth_gbps"])
                blk = int(row["block_size"])
            except (KeyError, ValueError):
                continue
            label = block_label_from_bytes(blk)
            by_block.setdefault(label, []).append(bw)
    return {k: statistics.mean(v) for k, v in by_block.items()}


def find_direction(platform: dict, direction_name: str) -> dict | None:
    for d in platform.get("directions", []):
        if d.get("name") == direction_name:
            return d
    return None


def main() -> None:
    ap = argparse.ArgumentParser(description="Merge comm CSV into performance JSON")
    ap.add_argument("--data", type=Path, required=True, help="communication_performance.json")
    ap.add_argument("--csv", type=Path, required=True, help="comm_result_*.csv")
    ap.add_argument("--platform", type=str, required=True, help="e.g. atlas_a2")
    ap.add_argument("--direction", type=str, required=True, help="e.g. D2rD, rH2D")
    ap.add_argument("--transport", choices=("hccs", "rdma", "fabric_mem"), required=True)
    ap.add_argument("--out", type=Path, default=None, help="default: overwrite --data")
    args = ap.parse_args()

    bw_map = avg_bandwidth_by_block(args.csv, args.transport)
    if not bw_map:
        raise SystemExit(f"[ERROR] No matching rows for transport={args.transport} in {args.csv}")

    doc = json.loads(args.data.read_text(encoding="utf-8"))
    out_path = args.out or args.data

    found = False
    patched = 0
    for plat in doc.get("platforms", []):
        if plat.get("id") != args.platform:
            continue
        d = find_direction(plat, args.direction)
        if d is None:
            raise SystemExit(f"[ERROR] Direction '{args.direction}' not found in platform '{args.platform}'")
        transports = d.setdefault("transports", {})
        tport_map = transports.setdefault(args.transport, {})
        for block, bw in bw_map.items():
            tport_map[block] = round(bw, 6)
            patched += 1
        found = True
        break

    if not found:
        raise SystemExit(f"[ERROR] Platform '{args.platform}' not found")

    out_path.write_text(json.dumps(doc, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"[INFO] updated {patched} blocks in {args.platform}/{args.direction}/{args.transport} -> {out_path}")


if __name__ == "__main__":
    main()
