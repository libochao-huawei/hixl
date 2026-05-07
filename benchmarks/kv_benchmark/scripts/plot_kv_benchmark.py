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

"""Plot KV benchmark CSV results."""

import argparse
import csv
from collections import defaultdict
from pathlib import Path


def read_rows(csv_path):
    with open(csv_path, newline="", encoding="utf-8") as csv_file:
        return list(csv.DictReader(csv_file))


def plot(plt, rows, metric, output):
    groups = defaultdict(list)
    for row in rows:
        groups[(row.get("model", "unknown"), row.get("transport", "fabric_mem"))].append(
            (float(row["token_length"]), float(row[metric]))
        )
    for (model, transport), points in groups.items():
        points.sort()
        xs, ys = zip(*points)
        plt.plot(xs, ys, marker="o", label=f"{model}/{transport}")
    plt.xlabel("token_length")
    plt.ylabel(metric)
    plt.xscale("log", base=2)
    plt.grid(True)
    plt.legend()
    plt.tight_layout()
    plt.savefig(output)
    plt.close()
    print(f"[INFO] wrote {output}")


def main():
    parser = argparse.ArgumentParser(description="Plot HIXL KV benchmark")
    parser.add_argument("csv")
    parser.add_argument("--output_dir", default=None)
    args = parser.parse_args()
    rows = read_rows(args.csv)
    try:
        import matplotlib.pyplot as plt
    except ImportError:
        print("[WARN] matplotlib is not installed; skip plot generation")
        return
    out_dir = Path(args.output_dir) if args.output_dir else Path(args.csv).parent
    out_dir.mkdir(parents=True, exist_ok=True)
    for metric in ("put_bandwidth_gbps", "get_bandwidth_gbps", "put_p99_us", "get_p99_us"):
        plot(plt, rows, metric, out_dir / f"kv_{metric}.png")


if __name__ == "__main__":
    main()
