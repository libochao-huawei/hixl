#!/usr/bin/env python3
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
#
# Migrate legacy BufferPool-style Markdown tables to communication_performance.json
# by keeping only plain HCCS and plain RDMA columns (non-BufferPool).
"""One-off / maintenance: parse A2/A3 md that still use the legacy dual BufferPool tables.

The generated `A2_benchmark_performance.md` in-tree no longer carries those columns; keep a tarball or git revision
with the legacy file if this script must be rerun."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


def dash(s: str) -> float | None:
    s = s.strip()
    if not s or s in ("——", "—", "--"):
        return None
    try:
        return float(s.replace(" ", ""))
    except ValueError:
        return None


def infer_mode(hdr_line: str) -> str:
    if "H2H" in hdr_line:
        return "h2h"
    if "D2H" in hdr_line:
        return "d2h"
    if "H2D" in hdr_line:
        return "h2d"
    if "D2D" in hdr_line:
        return "d2d"
    return "unknown"


def split_cells(line: str) -> list[str]:
    return [c.strip() for c in line.strip().strip("|").split("|")]


def is_separator_row(line: str) -> bool:
    stripped = line.strip()
    # Markdown alignment row e.g. |:---|---|---|
    return bool(stripped.startswith("|")) and "---" in stripped


def parse_platform(md_path: Path, platform_id: str, title_line: str) -> dict:
    lines = md_path.read_text(encoding="utf-8").splitlines()
    preamble: list[str] = []
    for ln in lines:
        if ln.startswith("### "):
            break
        preamble.append(ln.rstrip())
    constraints = (
        "\n".join(preamble[1:] if preamble and preamble[0].startswith("##") else preamble).strip()
    )

    scene: str | None = None
    op: str | None = None
    groups: list[dict] = []
    i = 0
    while i < len(lines):
        ln = lines[i]
        if ln.startswith("### 单机"):
            scene = "single_machine"
        elif ln.startswith("### 双机"):
            scene = "dual_machine"
        if ln.strip() == "- WRITE:":
            op = "write"
        elif ln.strip() == "- READ:":
            op = "read"

        if ln.startswith("|") and "BufferPool(GB/s)" in ln and "传输内存块大小" in ln:
            mode = infer_mode(ln)
            i += 1
            if i < len(lines) and is_separator_row(lines[i]):
                i += 1

            rows: list[dict] = []
            while i < len(lines) and lines[i].startswith("|"):
                cells = split_cells(lines[i])
                if len(cells) >= 5 and "传输内存块大小" in cells[0]:
                    break
                if len(cells) >= 5:
                    blk, hccs, _hccs_bp, rdma, _rdma_bp = (
                        cells[0],
                        cells[1],
                        cells[2],
                        cells[3],
                        cells[4],
                    )
                    rows.append(
                        {
                            "block": blk.strip(),
                            "hccs_gbps": dash(hccs),
                            "rdma_gbps": dash(rdma),
                            "fabric_mem_gbps": None,
                        },
                    )
                i += 1
            groups.append(
                {"scenario": scene, "op_type": op or "unknown", "transfer_mode": mode, "rows": rows},
            )
            continue
        i += 1

    return {
        "id": platform_id,
        "title": title_line.replace("#", "").strip(),
        "constraints_md": constraints,
        "table_groups": groups,
    }


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--a2", type=Path, default=None)
    ap.add_argument("--a3", type=Path, default=None)
    ap.add_argument("--out", type=Path, default=None)
    args = ap.parse_args()

    bench = Path(__file__).resolve().parents[1]
    a2_md = args.a2 or bench / "A2_benchmark_performance.md"
    a3_md = args.a3 or bench / "A3_benchmark_performance.md"
    out_json = args.out or (Path(__file__).resolve().parent / "communication_performance.json")

    a2_plat = parse_platform(
        a2_md,
        "atlas_a2",
        a2_md.read_text(encoding="utf-8").split("\n", 1)[0],
    )
    a3_plat = parse_platform(
        a3_md,
        "atlas_a3",
        a3_md.read_text(encoding="utf-8").split("\n", 1)[0],
    )
    a2_plat["show_fabric_mem_column"] = False
    a3_plat["show_fabric_mem_column"] = True

    doc = {
        "_meta": {},
        "schema_version": 1,
        "unit": "GB/s",
        "description": (
            "HCCS and RDMA numbers use non-BufferPool columns from legacy Markdown. Optional fabric_mem_gbps is null "
            "until filled from measurements."
        ),
        "bench_mapping": {
            "tool": "hixl_comm_bench",
            "hccs": "--transport=hccs --memory_type=device --transfer_mode=... --op_type=write|read",
            "rdma": "--transport=rdma (--transport sets HCCL_INTRA_ROCE_ENABLE)",
            "fabric_mem": "--transport=fabric_mem (optional numeric column)",
        },
        "platforms": [a2_plat, a3_plat],
    }

    out_json.write_text(json.dumps(doc, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(out_json)


if __name__ == "__main__":
    main()
