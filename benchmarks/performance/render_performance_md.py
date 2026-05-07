#!/usr/bin/env python3
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
#
# Render A2_benchmark_performance.md and A3_benchmark_performance.md from JSON (schema v2).
"""Render performance markdown tables and per-transport bandwidth charts."""

from __future__ import annotations

import argparse
import json
import sys
from collections import defaultdict
from pathlib import Path
from typing import Any

DEFAULT_JSON = Path(__file__).resolve().parent / "communication_performance.json"

# Order for table columns: write directions first, then read
DIRECTION_ORDER = ["D2rD", "D2rH", "H2rH", "H2rD", "rD2D", "rH2D", "rH2H", "rD2H"]
TRANSPORT_ORDER = ["hccs", "rdma", "fabric_mem"]

DIRECTION_LABEL: dict[str, str] = {
    "D2rD": "D → rD (write D2D)",
    "rD2D": "rD → D (read D2D)",
    "D2rH": "D → rH (write D2H)",
    "rH2D": "rH → D (read D2H)",
    "H2rH": "H → rH (write H2H)",
    "rH2H": "rH → H (read H2H)",
    "H2rD": "H → rD (write H2D)",
    "rD2H": "rD → H (read H2D)",
}

TRANSPORT_LABEL: dict[str, str] = {"hccs": "HCCS", "rdma": "ROCE", "fabric_mem": "FabricMem"}

BLOCK_SORT_ORDER = ["16K", "32K", "64K", "128K", "256K", "512K", "1M", "2M"]


def block_sort_key(b: str) -> int:
    try:
        return BLOCK_SORT_ORDER.index(b)
    except ValueError:
        return 999


def fmt_cell(val: Any) -> str:
    if val is None:
        return "——"
    if isinstance(val, float):
        s = f"{val:.3f}".rstrip("0").rstrip(".")
        return s
    return str(val)


# ---------------------------------------------------------------------------
# Table rendering
# ---------------------------------------------------------------------------

def collect_columns(platform: dict) -> list[tuple[str, str]]:
    """Return ordered list of (direction, transport) column pairs that have data."""
    dir_data: dict[str, set[str]] = defaultdict(set)
    for d in platform.get("directions", []):
        name = d["name"]
        for tport in d.get("transports", {}):
            # Only include if at least one block has non-null data
            has_data = any(v is not None for v in d["transports"][tport].values())
            if has_data:
                dir_data[name].add(tport)

    cols: list[tuple[str, str]] = []
    for dname in DIRECTION_ORDER:
        if dname not in dir_data:
            continue
        for tport in TRANSPORT_ORDER:
            if tport in dir_data[dname]:
                cols.append((dname, tport))
    return cols


def render_table(platform: dict) -> str:
    block_sizes = platform.get("block_sizes", [])
    cols = collect_columns(platform)
    if not cols or not block_sizes:
        return "*(No data)*\n\n"

    # Build lookup: direction -> transport -> block -> value
    lookup: dict[str, dict[str, dict[str, float | None]]] = {}
    for d in platform.get("directions", []):
        lookup[d["name"]] = d.get("transports", {})

    # Header
    lines: list[str] = []
    header = "| **Block** |"
    sep = "| :---: |"
    for dname, tport in cols:
        header += f" **{dname}<br>{TRANSPORT_LABEL.get(tport, tport)}** |"
        sep += " :---: |"
    lines.append(header)
    lines.append(sep)

    # Rows
    for bs in sorted(block_sizes, key=block_sort_key):
        row = f"| {bs} |"
        for dname, tport in cols:
            val = lookup.get(dname, {}).get(tport, {}).get(bs)
            row += f" {fmt_cell(val)} |"
        lines.append(row)

    return "\n".join(lines) + "\n"


# ---------------------------------------------------------------------------
# Direction legend (beginner-friendly)
# ---------------------------------------------------------------------------

def direction_legend(platform: dict) -> str:
    """Build direction legend from actual platform data."""
    dir_info: dict[str, dict] = {}
    for d in platform.get("directions", []):
        dir_info[d["name"]] = d

    lines = [
        "### 方向命名速查\n\n",
        "方向名格式为 `源 → 远程目标`，其中 **D**=Device、**H**=Host、**r**=remote。\n\n",
        "| 方向 | 含义 | Initiator | Target | 操作 |\n",
        "| :--- | :--- | :---: | :---: | :---: |\n",
    ]
    for dname in DIRECTION_ORDER:
        info = dir_info.get(dname)
        if info is None:
            continue
        mem_labels = {"device": "Device", "host": "Host"}
        im = mem_labels.get(info.get("initiator_memory", ""), "")
        tm = mem_labels.get(info.get("target_memory", ""), "")
        op = info.get("op_type", "")
        label = DIRECTION_LABEL.get(dname, dname)
        lines.append(f"| **{dname}** | {label} | {im} | {tm} | {op} |\n")
    lines.append("\n")
    return "".join(lines)


def beginner_intro(platform: dict) -> str:
    pid = platform.get("id", "")
    lines = [f"## {platform.get('title', '')}\n\n"]

    if pid == "atlas_a2":
        lines.append(
            "本页展示 HIXL 在 **昇腾 A2**（Atlas 800I A2 / A200I A2 Box）上的通信性能数据。\n\n"
            "**测试方式**：在同一台机器上运行 `hixl_comm_bench` 的 target 和 initiator，"
            "测量不同 block size（16K～2M）下的有效带宽。\n\n"
            "**约束**：A2 上 HCCS 传输仅支持 Device↔Device（D2D）方向；"
            "其他方向（D2H、H2H、H2D）只能走 ROCE（RDMA）传输。\n\n"
        )
    elif pid == "atlas_a3":
        lines.append(
            "本页展示 HIXL 在 **昇腾 A3**（Atlas A3 训练/推理系列）上的通信性能数据。\n\n"
            "**测试方式**：在同一台机器上运行 `hixl_comm_bench` 的 target 和 initiator，"
            "测量不同 block size（16K～2M）下的有效带宽。\n\n"
            "**约束**：A3 上 HCCS 传输不支持 Host 内存作为远端 Cache；"
            "D2H/H2H 方向的 HCCS 数据因此缺失。ROCE 和 FabricMem 支持全部方向。\n\n"
        )
    lines.append(
        "### 如何读懂表格\n\n"
        "- **Block** 列：每次传输的数据块大小（16K = 16 KiB，1M = 1 MiB）。\n"
        "- **方向+传输** 列：例如 `D2rD HCCS` 表示从本地 Device 写往远程 Device（HCCS 传输），"
        "`rH2D ROCE` 表示从远程 Host 读回本地 Device（ROCE 传输）。\n"
        "- 数值为有效带宽（GB/s），**——** 表示该组合不支持或尚未测试。\n"
        "- 所有数据来自单机 1:1（一个 initiator + 一个 target）测试。\n\n"
    )
    return "".join(lines)


# ---------------------------------------------------------------------------
# Chart generation
# ---------------------------------------------------------------------------

def render_transport_chart(plt, platform: dict, transport: str, output_dir: Path) -> bool:
    """Generate one PNG per transport with all direction lines."""
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as mpl
    except ImportError:
        return False

    block_sizes = platform.get("block_sizes", [])
    directions = platform.get("directions", [])
    ordered_blocks = sorted(block_sizes, key=block_sort_key)

    # Collect (direction_name, {block: bw}) for this transport
    series: list[tuple[str, dict[str, float]]] = []
    for d in directions:
        tports = d.get("transports", {})
        if transport in tports:
            bw_map = {k: v for k, v in tports[transport].items() if v is not None}
            if bw_map:
                series.append((d["name"], bw_map))

    if not series:
        return False

    fig, ax = mpl.subplots(figsize=(12, 6))
    colors = ["#1f77b4", "#ff7f0e", "#2ca02c", "#d62728",
              "#9467bd", "#8c564b", "#e377c2", "#7f7f7f"]

    for idx, (dname, bw_map) in enumerate(series):
        xs_vals = []
        ys = []
        tick_labels = []
        for i, b in enumerate(ordered_blocks):
            if b in bw_map:
                xs_vals.append(i)
                ys.append(bw_map[b])
                tick_labels.append(b)
        if not ys:
            continue
        color = colors[idx % len(colors)]
        ax.plot(xs_vals, ys, marker="o", linestyle="-", color=color, linewidth=1.2,
                markersize=4, label=dname)

    ax.set_xticks(range(len(ordered_blocks)))
    ax.set_xticklabels(ordered_blocks, rotation=35, ha="right", fontsize=8)
    ax.set_xlabel("Block", fontsize=10)
    ax.set_ylabel("Bandwidth (GB/s)", fontsize=10)
    ax.set_title(f"{platform['id']} — {TRANSPORT_LABEL.get(transport, transport)}", fontsize=12)
    ax.grid(True, linestyle=":", alpha=0.4)
    ax.legend(fontsize=8, loc="upper left")
    fig.tight_layout()

    out_png = output_dir / f"{platform['id']}_{transport}_bandwidth.png"
    out_png.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(out_png, dpi=140)
    mpl.close(fig)
    print(f"[INFO] wrote {out_png}")
    return True


# ---------------------------------------------------------------------------
# Main render
# ---------------------------------------------------------------------------

def render_platform(platform: dict, write_plots: bool,
                    figures_dir: Path) -> str:
    pid = platform["id"]
    lines: list[str] = []

    # Intro
    lines.append(beginner_intro(platform))

    # Direction legend
    lines.append(direction_legend(platform))

    # Performance table
    lines.append("### 性能数据\n\n")
    lines.append(render_table(platform))

    # Charts
    if write_plots:
        transports_seen: set[str] = set()
        for d in platform.get("directions", []):
            for tport in d.get("transports", {}):
                if any(v is not None for v in d["transports"][tport].values()):
                    transports_seen.add(tport)

        for tport in sorted(transports_seen, key=lambda t: TRANSPORT_ORDER.index(t) if t in TRANSPORT_ORDER else 99):
            ok = render_transport_chart(None, platform, tport, figures_dir)
            if ok:
                rel = Path("performance/figures") / f"{pid}_{tport}_bandwidth.png"
                lines.append(f"\n#### {TRANSPORT_LABEL.get(tport, tport)} 带宽折线图\n\n")
                lines.append(f"![]({rel.as_posix()})\n\n")

    return "".join(lines)


def main() -> None:
    ap = argparse.ArgumentParser(description="Render performance markdown from JSON (v2).")
    ap.add_argument("--data", type=Path, default=DEFAULT_JSON)
    ap.add_argument("--no-plots", action="store_true")
    ap.add_argument("--output", type=Path, default=None,
                    help="Output markdown file (default: benchmarks/performance.md)")
    args = ap.parse_args()

    write_plots = not args.no_plots
    if write_plots:
        try:
            import matplotlib  # noqa: F401
            matplotlib.use("Agg")
        except ImportError:
            print("[WARN] matplotlib not installed; skip PNG charts", file=sys.stderr)
            write_plots = False

    data = json.loads(args.data.read_text(encoding="utf-8"))
    if data.get("schema_version") != 2:
        raise SystemExit(f"[ERROR] Expected schema_version=2, got {data.get('schema_version')}")

    benchmarks_dir = Path(__file__).resolve().parents[1]
    figures_dir = benchmarks_dir / "performance" / "figures"

    out_path = args.output or (benchmarks_dir / "performance.md")
    known_ids = {"atlas_a2", "atlas_a3"}
    sections: list[str] = []

    for plat in data.get("platforms", []):
        pid = plat.get("id")
        if pid not in known_ids:
            continue
        sections.append(render_platform(plat, write_plots, figures_dir))

    if sections:
        out_path.parent.mkdir(parents=True, exist_ok=True)
        out_path.write_text("\n---\n\n".join(sections), encoding="utf-8")
        print(f"[INFO] wrote {out_path}")


if __name__ == "__main__":
    main()
