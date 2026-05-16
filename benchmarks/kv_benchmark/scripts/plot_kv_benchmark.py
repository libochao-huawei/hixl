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


def format_count(value):
    value = int(value)
    if value >= 1024 and value % 1024 == 0:
        return f"{value // 1024}K"
    return str(value)


def aggregate_points(rows, metric, x_field):
    groups = defaultdict(list)
    for row in rows:
        x_value = row[x_field]
        key = (
            row.get("model", "unknown"),
            row.get("transport", "fabric_mem"),
            float(x_value),
        )
        value = float(row[metric])
        if value <= 0:
            continue
        groups[key].append(value)

    series = defaultdict(list)
    for (model, transport, x_value), values in groups.items():
        if not values:
            continue
        series[(model, transport)].append((x_value, sum(values) / len(values)))
    return series


def plot(plt, rows, metric, output, x_field):
    series = aggregate_points(rows, metric, x_field)
    if not series:
        print(f"[WARN] no data for {metric}; skip")
        return
    all_xs = set()
    for (model, transport), points in series.items():
        points.sort()
        xs, ys = zip(*points)
        all_xs.update(xs)
        plt.plot(xs, ys, marker="o", label=f"{model}/{transport}")
    plt.xlabel(x_field)
    plt.ylabel(metric)
    if all_xs:
        ticks = sorted(all_xs)
        plt.xticks(ticks, [format_count(x) for x in ticks])
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
    if not rows:
        print(f"[WARN] {args.csv} has no rows; skip plot generation")
        return
    try:
        import matplotlib.pyplot as plt
    except ImportError:
        print("[WARN] matplotlib is not installed; skip plot generation")
        return
    out_dir = Path(args.output_dir) if args.output_dir else Path(args.csv).parent
    out_dir.mkdir(parents=True, exist_ok=True)
    metrics = (
        "put_bandwidth_gbps",
        "get_bandwidth_gbps",
        "put_avg_us",
        "get_avg_us",
        "put_p99_us",
        "get_p99_us",
    )
    x_field = "key_count" if "key_count" in rows[0] else "token_length"
    for metric in metrics:
        if metric in rows[0]:
            plot(plt, rows, metric, out_dir / f"kv_{metric}.png", x_field)


if __name__ == "__main__":
    main()
