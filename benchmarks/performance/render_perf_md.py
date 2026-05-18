#!/usr/bin/env python3
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
"""Render perf.md directly from hixl_comm_bench CSV outputs.

Reads all comm_result_*.csv files, computes average bandwidth per
(direction, transport, block_size), and writes a single-platform perf.md.
Also prints the performance table to stdout.
"""

from __future__ import annotations

import argparse
import csv
import statistics
import sys
from collections import defaultdict
from pathlib import Path
from typing import Any

DIRECTION_ORDER = ["D2rD", "D2rH", "H2rH", "H2rD", "rD2D", "rH2D", "rH2H", "rD2H"]
TRANSPORT_ORDER = ["hccs", "rdma", "fabric_mem"]

# HCCS path supports device-to-device only (matches comm benchmark restriction).
HCCS_SUPPORTED_DIRECTIONS = frozenset({"D2rD", "rD2D"})

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

BLOCK_SORT_ORDER = ["16K", "32K", "64K", "128K", "256K", "512K", "1M", "2M", "4M", "8M"]


def block_sort_key(b: str) -> int:
    try:
        return BLOCK_SORT_ORDER.index(b)
    except ValueError:
        return 999


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


def fmt_cell(val: Any) -> str:
    if val is None:
        return "——"
    if isinstance(val, float):
        s = f"{val:.3f}".rstrip("0").rstrip(".")
        return s
    return str(val)


def hccs_cell_not_supported(direction: str, transport: str) -> bool:
    return transport == "hccs" and direction not in HCCS_SUPPORTED_DIRECTIONS


def format_bandwidth_cell(
    direction: str,
    transport: str,
    block_label: str,
    lookup: dict[str, dict[str, dict[str, float]]],
) -> str:
    if hccs_cell_not_supported(direction, transport):
        return "不支持"
    val = lookup.get(direction, {}).get(transport, {}).get(block_label)
    return fmt_cell(val)


# ---------------------------------------------------------------------------
# CSV loading
# ---------------------------------------------------------------------------


def load_csvs(csv_dir: Path) -> list[dict]:
    """Load all comm_result_*.csv and return list of merged rows."""
    rows: list[dict] = []
    for csv_path in sorted(csv_dir.glob("comm_result_*.csv")):
        with csv_path.open(newline="", encoding="utf-8") as f:
            rows.extend(list(csv.DictReader(f)))
    return rows


def build_lookup(rows: list[dict]) -> dict[str, dict[str, dict[str, float]]]:
    """Build direction -> transport -> block_label -> avg_bandwidth_gbps."""
    groups: dict[str, dict[str, dict[str, list[float]]]] = defaultdict(
        lambda: defaultdict(lambda: defaultdict(list))
    )
    for r in rows:
        direction = r.get("direction", "").strip()
        transport = r.get("transport", "").strip()
        try:
            bw = float(r["bandwidth_gbps"])
            blk = int(r["block_size"])
        except (KeyError, ValueError):
            continue
        if not direction or not transport:
            continue
        label = block_label_from_bytes(blk)
        groups[direction][transport][label].append(bw)

    result: dict[str, dict[str, dict[str, float]]] = {}
    for dname, tports in groups.items():
        result[dname] = {}
        for tport, blocks in tports.items():
            result[dname][tport] = {
                b: statistics.mean(vals) for b, vals in blocks.items()
            }
    return result


# ---------------------------------------------------------------------------
# Table rendering
# ---------------------------------------------------------------------------


def collect_columns_with_data(
    lookup: dict[str, dict[str, dict[str, float]]]
) -> list[tuple[str, str]]:
    """Return ordered list of (direction, transport) pairs that have numeric data."""
    cols: list[tuple[str, str]] = []
    for dname in DIRECTION_ORDER:
        if dname not in lookup:
            continue
        for tport in TRANSPORT_ORDER:
            if tport in lookup[dname] and lookup[dname][tport]:
                cols.append((dname, tport))
    return cols


def collect_columns_full() -> list[tuple[str, str]]:
    """Full direction × transport matrix for perf tables (includes unsupported HCCS columns)."""
    cols: list[tuple[str, str]] = []
    for dname in DIRECTION_ORDER:
        for tport in TRANSPORT_ORDER:
            cols.append((dname, tport))
    return cols


def render_table(lookup: dict[str, dict[str, dict[str, float]]]) -> str:
    cols = collect_columns_full()
    all_blocks: set[str] = set()
    for _dname, _tport in collect_columns_with_data(lookup):
        all_blocks.update(lookup.get(_dname, {}).get(_tport, {}).keys())
    ordered_blocks = sorted(all_blocks, key=block_sort_key)
    if not ordered_blocks:
        return "*(No bandwidth data in CSVs — run comm benchmarks first)*\n\n"

    lines: list[str] = []
    header = "| **Block** |"
    sep = "| :---: |"
    for dname, tport in cols:
        header += f" **{dname}<br>{TRANSPORT_LABEL.get(tport, tport)}** |"
        sep += " :---: |"
    lines.append(header)
    lines.append(sep)

    for bs in ordered_blocks:
        row = f"| {bs} |"
        for dname, tport in cols:
            row += f" {format_bandwidth_cell(dname, tport, bs, lookup)} |"
        lines.append(row)

    return "\n".join(lines) + "\n"


# ---------------------------------------------------------------------------
# Direction legend
# ---------------------------------------------------------------------------


def direction_legend() -> str:
    lines = [
        "### 方向命名速查\n\n",
        "方向名格式为 `源 → 远程目标`，其中 **D**=Device、**H**=Host、**r**=remote。\n\n",
        "| 方向 | 含义 | 操作 |\n",
        "| :--- | :--- | :---: |\n",
        "| **D2rD** | Device 写往远程 Device | write |\n",
        "| **rD2D** | 从远程 Device 读回 Device | read |\n",
        "| **D2rH** | Device 写往远程 Host | write |\n",
        "| **rH2D** | 从远程 Host 读回 Device | read |\n",
        "| **H2rH** | Host 写往远程 Host | write |\n",
        "| **rH2H** | 从远程 Host 读回 Host | read |\n",
        "| **H2rD** | Host 写往远程 Device | write |\n",
        "| **rD2H** | 从远程 Device 读回 Host | read |\n",
        "\n",
    ]
    return "".join(lines)


# ---------------------------------------------------------------------------
# Intro
# ---------------------------------------------------------------------------


def platform_intro(platform: str) -> str:
    pid = platform
    lines = [f"## {platform_title(platform)}\n\n"]

    if pid == "atlas_a2":
        lines.append(
            "本页展示 HIXL 在 **昇腾 A2**（Atlas 800I A2 / A200I A2 Box）上的通信性能数据。\n\n"
            "**测试方式**：在同一台机器上运行 `hixl_comm_bench` 的 target 和 initiator，"
            "测量不同 block size（16K～2M）下的有效带宽。\n\n"
            "**约束**：benchmark 将 **HCCS** 限制为 **仅 D2D**（`D2rD` / `rD2D`）；"
            "涉及 Host 的方向请使用 **RDMA** 或 **FabricMem**。\n\n"
        )
    elif pid == "atlas_a3":
        lines.append(
            "本页展示 HIXL 在 **昇腾 A3**（Atlas A3 训练/推理系列）上的通信性能数据。\n\n"
            "**测试方式**：在同一台机器上运行 `hixl_comm_bench` 的 target 和 initiator，"
            "测量不同 block size（16K～2M）下的有效带宽。\n\n"
            "**约束**：benchmark 将 **HCCS** 限制为 **仅 D2D**（`D2rD` / `rD2D`）；"
            "其余方向请使用 **RDMA** 或 **FabricMem**。\n\n"
        )
    lines.append(
        "### 如何读懂表格\n\n"
        "- **Block** 列：每次传输的数据块大小（16K = 16 KiB，1M = 1 MiB）。\n"
        "- **方向+传输** 列：例如 `D2rD HCCS` 表示从本地 Device 写往远程 Device（HCCS 传输），"
        "`rH2D ROCE` 表示从远程 Host 读回本地 Device（ROCE 传输）。\n"
        "- 数值为有效带宽（GB/s）；**不支持** 表示该传输路径不支持该方向（如 HCCS 仅 D2D）；"
        "**——** 表示尚未采集到数据。\n"
        "- 所有数据来自单机 1:1（一个 initiator + 一个 target）测试。\n\n"
    )
    return "".join(lines)


def platform_title(platform: str) -> str:
    titles = {
        "atlas_a2": "昇腾 A2 (Ascend910B) 通信性能",
        "atlas_a3": "昇腾 A3 (Ascend910) 通信性能",
    }
    return titles.get(platform, f"{platform} 通信性能")


# ---------------------------------------------------------------------------
# Charts
# ---------------------------------------------------------------------------


def render_charts(
    lookup: dict[str, dict[str, dict[str, float]]],
    platform: str,
    output_dir: Path,
) -> list[str]:
    """Generate per-transport bandwidth PNGs. Returns relative paths to embed."""
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as mpl
    except ImportError:
        print("[WARN] matplotlib not installed; skip PNG charts", file=sys.stderr)
        return []

    block_sizes = set()
    for tports in lookup.values():
        for blocks in tports.values():
            block_sizes.update(blocks.keys())
    ordered_blocks = sorted(block_sizes, key=block_sort_key)

    transports_seen: set[str] = set()
    for tports in lookup.values():
        for tport, blocks in tports.items():
            if blocks:
                transports_seen.add(tport)

    colors = [
        "#1f77b4", "#ff7f0e", "#2ca02c", "#d62728",
        "#9467bd", "#8c564b", "#e377c2", "#7f7f7f",
    ]
    embeds: list[str] = []

    for tport in sorted(
        transports_seen,
        key=lambda t: TRANSPORT_ORDER.index(t) if t in TRANSPORT_ORDER else 99,
    ):
        fig, ax = mpl.subplots(figsize=(12, 6))
        color_idx = 0

        for dname in DIRECTION_ORDER:
            bw_map = lookup.get(dname, {}).get(tport, {})
            if not bw_map:
                continue
            xs, ys, labels = [], [], []
            for i, b in enumerate(ordered_blocks):
                if b in bw_map:
                    xs.append(i)
                    ys.append(bw_map[b])
                    labels.append(b)
            if not ys:
                continue
            color = colors[color_idx % len(colors)]
            color_idx += 1
            ax.plot(
                xs, ys, marker="o", linestyle="-", color=color,
                linewidth=1.2, markersize=4, label=dname,
            )

        ax.set_xticks(range(len(ordered_blocks)))
        ax.set_xticklabels(ordered_blocks, rotation=35, ha="right", fontsize=8)
        ax.set_xlabel("Block", fontsize=10)
        ax.set_ylabel("Bandwidth (GB/s)", fontsize=10)
        ax.set_title(
            f"{platform} — {TRANSPORT_LABEL.get(tport, tport)}", fontsize=12
        )
        ax.grid(True, linestyle=":", alpha=0.4)
        ax.legend(fontsize=8, loc="upper left")
        fig.tight_layout()

        out_png = output_dir / f"{platform}_{tport}_bandwidth.png"
        out_png.parent.mkdir(parents=True, exist_ok=True)
        fig.savefig(out_png, dpi=140)
        mpl.close(fig)
        print(f"[INFO] wrote {out_png}")

        rel = Path("performance/figures") / out_png.name
        embeds.append(
            f"\n#### {TRANSPORT_LABEL.get(tport, tport)} 带宽折线图\n\n"
            f"![]({rel.as_posix()})\n\n"
        )

    return embeds


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------


def main() -> None:
    ap = argparse.ArgumentParser(
        description="Render perf.md from hixl_comm_bench CSV outputs"
    )
    ap.add_argument("--csv-dir", type=Path, required=True,
                    help="Directory containing comm_result_*.csv files")
    ap.add_argument("--platform", type=str, required=True,
                    help="Platform ID: atlas_a2 or atlas_a3")
    ap.add_argument("--output", type=Path, default=None,
                    help="Output perf.md path (default: benchmarks/perf.md)")
    ap.add_argument("--no-plots", action="store_true",
                    help="Skip PNG chart generation")
    args = ap.parse_args()

    rows = load_csvs(args.csv_dir)
    if not rows:
        print(f"[WARN] No CSV rows found in {args.csv_dir}")
        return

    lookup = build_lookup(rows)
    data_cols = collect_columns_with_data(lookup)
    if not data_cols:
        print("[WARN] No direction+transport combos with numeric bandwidth data found")

    # --- perf.md ---
    intro = platform_intro(args.platform)
    legend = direction_legend()
    table = render_table(lookup)

    sections = [intro, legend, "### 性能数据\n\n", table]

    # Charts
    benchmarks_dir = Path(__file__).resolve().parents[1]
    figures_dir = benchmarks_dir / "performance" / "figures"
    if not args.no_plots:
        chart_embeds = render_charts(lookup, args.platform, figures_dir)
        sections.extend(chart_embeds)

    md_content = "".join(sections)

    out_path = args.output or (benchmarks_dir / "perf.md")
    out_path.write_text(md_content, encoding="utf-8")
    print(f"[INFO] wrote {out_path}")

    # --- Terminal output ---
    print("\n" + "=" * 70)
    print(f"  {platform_title(args.platform)}")
    print("=" * 70)
    print()
    print(table)


if __name__ == "__main__":
    main()
