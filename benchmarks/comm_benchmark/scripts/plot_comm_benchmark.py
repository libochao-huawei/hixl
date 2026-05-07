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

"""Plot HIXL communication benchmark results.

Two modes:
  Single run:  plot one CSV  -> one chart with one line (bandwidth vs block_size)
  Batch run:   read JSON     -> one chart per transport with all direction lines
"""

import argparse
import csv
import json
import statistics
from collections import defaultdict
from pathlib import Path


BLOCK_ORDER = ["16K", "32K", "64K", "128K", "256K", "512K", "1M", "2M", "4M", "8M"]


def block_sort_key(label: str) -> int:
    try:
        return BLOCK_ORDER.index(label)
    except ValueError:
        return 999


# ---------------------------------------------------------------------------
# Single-run mode
# ---------------------------------------------------------------------------

def plot_single(csv_path: Path, output_dir: Path) -> None:
    try:
        import matplotlib.pyplot as plt
    except ImportError:
        print("[WARN] matplotlib is not installed; skip plot generation")
        return

    rows = _load_csv(csv_path)
    if not rows:
        print(f"[WARN] no data in {csv_path}")
        return

    direction = rows[0].get("direction", "unknown")
    transport = rows[0].get("transport", "unknown")
    title = f"{direction} ({transport})"

    by_block = _avg_by_block(rows, "bandwidth_gbps")
    _line_chart(plt, by_block, title, "Block", "Bandwidth (GB/s)",
                output_dir / f"comm_bandwidth_{direction}_{transport}.png")

    by_block_p99 = _avg_by_block(rows, "p99_us")
    if by_block_p99:
        _line_chart(plt, by_block_p99, f"{title} — P99", "Block", "P99 (us)",
                    output_dir / f"comm_p99_{direction}_{transport}.png")


# ---------------------------------------------------------------------------
# Batch mode (from JSON)
# ---------------------------------------------------------------------------

def plot_batch(json_path: Path, platform: str, output_dir: Path) -> None:
    try:
        import matplotlib.pyplot as plt
    except ImportError:
        print("[WARN] matplotlib is not installed; skip plot generation")
        return

    data = json.loads(json_path.read_text(encoding="utf-8"))
    plat = _find_platform(data, platform)
    if plat is None:
        print(f"[ERROR] platform '{platform}' not found in JSON")
        return

    block_sizes = plat.get("block_sizes", [])
    directions = plat.get("directions", [])

    # Group by transport
    by_transport: dict[str, dict[str, dict[str, float]]] = defaultdict(dict)
    for d in directions:
        name = d["name"]
        for tport, bw_map in d.get("transports", {}).items():
            by_transport[tport][name] = bw_map

    for tport, dir_map in sorted(by_transport.items()):
        _batch_transport_chart(plt, tport, dir_map, block_sizes, platform, output_dir)


def _batch_transport_chart(plt, transport: str, dir_map: dict, block_sizes: list,
                           platform: str, output_dir: Path) -> None:
    # dir_map: {direction_name: {block: bandwidth or None}}
    fig, ax = plt.subplots(figsize=(12, 6))
    ordered_blocks = sorted(block_sizes, key=block_sort_key)

    colors = ["#1f77b4", "#ff7f0e", "#2ca02c", "#d62728",
              "#9467bd", "#8c564b", "#e377c2", "#7f7f7f"]
    color_idx = 0

    for direction in sorted(dir_map.keys()):
        bw_map = dir_map[direction]
        xs = []
        ys = []
        labels = []
        for b in ordered_blocks:
            val = bw_map.get(b)
            if val is not None:
                xs.append(ordered_blocks.index(b))
                ys.append(val)
                labels.append(b)
        if not ys:
            continue
        color = colors[color_idx % len(colors)]
        color_idx += 1
        ax.plot(xs, ys, marker="o", linestyle="-", color=color, linewidth=1.2,
                markersize=4, label=direction)

    ax.set_xticks(range(len(ordered_blocks)))
    ax.set_xticklabels(ordered_blocks, rotation=35, ha="right", fontsize=8)
    ax.set_xlabel("Block", fontsize=10)
    ax.set_ylabel("Bandwidth (GB/s)", fontsize=10)
    ax.set_title(f"{platform} — {transport.upper()}", fontsize=12)
    ax.grid(True, linestyle=":", alpha=0.4)
    ax.legend(fontsize=8, loc="upper left")
    fig.tight_layout()
    out = output_dir / f"{platform}_{transport}_bandwidth.png"
    fig.savefig(out, dpi=140)
    plt.close(fig)
    print(f"[INFO] wrote {out}")


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _load_csv(path: Path) -> list[dict]:
    with path.open(newline="", encoding="utf-8") as f:
        return list(csv.DictReader(f))


def _avg_by_block(rows: list[dict], field: str) -> dict[str, float]:
    groups: dict[str, list[float]] = defaultdict(list)
    for r in rows:
        try:
            val = float(r[field])
            blk = r.get("block_size", "")
        except (KeyError, ValueError):
            continue
        groups[blk].append(val)
    return {k: statistics.mean(v) for k, v in groups.items()}


def _line_chart(plt, by_block: dict, title: str, xlabel: str, ylabel: str, output: Path) -> None:
    items = sorted(by_block.items(), key=lambda kv: block_sort_key(kv[0]))
    if not items:
        return
    labels, ys = zip(*items)
    xs = list(range(len(labels)))
    plt.figure(figsize=(9, 5))
    plt.plot(xs, ys, marker="o", linestyle="-", linewidth=1.5, markersize=5)
    plt.xticks(xs, labels, rotation=35, ha="right")
    plt.xlabel(xlabel)
    plt.ylabel(ylabel)
    plt.title(title)
    plt.grid(True, linestyle=":", alpha=0.4)
    plt.tight_layout()
    plt.savefig(output, dpi=140)
    plt.close()
    print(f"[INFO] wrote {output}")


def _find_platform(data: dict, platform: str) -> dict | None:
    for p in data.get("platforms", []):
        if p.get("id") == platform:
            return p
    return None


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def main() -> None:
    parser = argparse.ArgumentParser(description="Plot HIXL communication benchmark")
    parser.add_argument("--csv", type=Path, default=None, help="Single CSV from hixl_comm_bench")
    parser.add_argument("--json", type=Path, default=None, help="communication_performance.json for batch mode")
    parser.add_argument("--platform", type=str, default=None, help="Platform ID (atlas_a2 / atlas_a3) for batch mode")
    parser.add_argument("--output_dir", type=Path, default=None, help="Output directory for PNGs")
    args = parser.parse_args()

    if args.csv:
        out_dir = args.output_dir or args.csv.parent
        out_dir.mkdir(parents=True, exist_ok=True)
        plot_single(args.csv, out_dir)
    elif args.json and args.platform:
        out_dir = args.output_dir or args.json.parent
        out_dir.mkdir(parents=True, exist_ok=True)
        plot_batch(args.json, args.platform, out_dir)
    else:
        parser.error("use --csv for single mode, or --json --platform for batch mode")


if __name__ == "__main__":
    main()
