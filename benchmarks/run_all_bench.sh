#!/bin/bash
# ----------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------
#
# Run all HIXL benchmarks and generate perf.md.
#
# Usage:
#   bash benchmarks/run_all_bench.sh                    # single machine
#   bash benchmarks/run_all_bench.sh --target-host=10.0.0.1  # dual machine
#   bash benchmarks/run_all_bench.sh --skip-kv --duration 10
#

set -e

BASEPATH=$(cd "$(dirname "$0")/.."; pwd)
cd "$BASEPATH"

# --- usage ---
usage() {
  echo "Usage:"
  echo "  bash benchmarks/run_all_bench.sh [OPTIONS]"
  echo ""
  echo "Options:"
  echo "  --duration <SEC>        Test duration per combo (default: 5)"
  echo "  --device-ids <LIST>     Device IDs, comma-separated (default: 0,1,2,3,4,5,6,7)"
  echo "  --target-host <IP>      Remote target host for dual-machine mode (default: 127.0.0.1)"
  echo "  --skip-comm             Skip communication benchmarks"
  echo "  --skip-kv               Skip KV cache benchmarks"
  echo "  --output <PATH>         perf.md output path (default: benchmarks/perf.md)"
  echo "  -h, --help              Print this message"
  echo ""
  echo "Examples:"
  echo "  bash benchmarks/run_all_bench.sh"
  echo "  bash benchmarks/run_all_bench.sh --duration 10 --device-ids 0,1"
  echo "  bash benchmarks/run_all_bench.sh --target-host=192.168.1.100"
}

# --- check CANN environment ---
check_env() {
  if ! command -v npu-smi &>/dev/null; then
    echo "[WARN] npu-smi not found. Trying to source CANN environment..."
    if [ -f /usr/local/Ascend/cann/set_env.sh ]; then
      source /usr/local/Ascend/cann/set_env.sh
    else
      echo "[ERROR] CANN environment not available. Please source set_env.sh first."
      exit 1
    fi
  fi
}

# --- main ---
DURATION=5
DEVICE_IDS="0,1,2,3,4,5,6,7"
TARGET_HOST="127.0.0.1"
SKIP_COMM=""
SKIP_KV=""
OUTPUT=""

while [ $# -gt 0 ]; do
  case "$1" in
    --duration)
      DURATION="$2"; shift 2 ;;
    --duration=*)
      DURATION="${1#*=}"; shift ;;
    --device-ids)
      DEVICE_IDS="$2"; shift 2 ;;
    --device-ids=*)
      DEVICE_IDS="${1#*=}"; shift ;;
    --target-host)
      TARGET_HOST="$2"; shift 2 ;;
    --target-host=*)
      TARGET_HOST="${1#*=}"; shift ;;
    --skip-comm)
      SKIP_COMM="--skip_comm"; shift ;;
    --skip-kv)
      SKIP_KV="--skip_kv"; shift ;;
    --output)
      OUTPUT="$2"; shift 2 ;;
    --output=*)
      OUTPUT="${1#*=}"; shift ;;
    -h|--help)
      usage; exit 0 ;;
    *)
      echo "Unknown option: $1"
      usage; exit 1 ;;
  esac
done

check_env

echo "============================================"
echo "  HIXL Benchmark Runner"
echo "============================================"
echo "  Duration:    ${DURATION}s"
echo "  Devices:     ${DEVICE_IDS}"
echo "  Target host: ${TARGET_HOST}"
echo "============================================"
echo ""

# Build benchmarks if needed
BENCH_BIN="${BASEPATH}/build/benchmarks/comm_benchmark/hixl_comm_bench"
if [ ! -f "$BENCH_BIN" ]; then
  echo "[INFO] Benchmarks not built yet, building..."
  bash "${BASEPATH}/build.sh" --examples
fi

# Build Python args
PY_ARGS="--duration ${DURATION} --device_ids ${DEVICE_IDS}"
if [ -n "$SKIP_COMM" ]; then
  PY_ARGS="${PY_ARGS} ${SKIP_COMM}"
fi
if [ -n "$SKIP_KV" ]; then
  PY_ARGS="${PY_ARGS} ${SKIP_KV}"
fi
if [ "$TARGET_HOST" != "127.0.0.1" ]; then
  PY_ARGS="${PY_ARGS} --target_host ${TARGET_HOST}"
fi

# Run benchmarks
python3 "${BASEPATH}/benchmarks/run_all_benchmarks.py" ${PY_ARGS}

# Print output location
if [ -n "$OUTPUT" ]; then
  echo "[INFO] perf.md written to: ${OUTPUT}"
else
  echo "[INFO] perf.md written to: ${BASEPATH}/benchmarks/perf.md"
fi
