#!/usr/bin/env bash
# ----------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
RUNNER="${ROOT_DIR}/benchmarks/comm_benchmark/scripts/run_comm_benchmark.py"
BENCH_BIN="${ROOT_DIR}/build/benchmarks/comm_benchmark/hixl_comm_bench"

RUNS="${RUNS:-20}"
SIZE="${SIZE:-1073741824}"
DEVICES="${DEVICES:-0,1}"
SOC_VARIANT="${SOC_VARIANT:-a5}"
OUT_ROOT="${OUT_ROOT:-${ROOT_DIR}/comm_benchmark/output/uboe_rd2h_host_flag_probe}"

if [[ ! -x "${BENCH_BIN}" ]]; then
  echo "[ERROR] benchmark binary not found: ${BENCH_BIN}"
  echo "[ERROR] please build first: bash build.sh --examples"
  exit 1
fi

mkdir -p "${OUT_ROOT}"

pass_count=0
fail_count=0
host_flag_done_count=0
host_flag_not_visible_count=0

for run_id in $(seq 1 "${RUNS}"); do
  run_dir="${OUT_ROOT}/run_${run_id}"
  log_file="${run_dir}/run.log"
  mkdir -p "${run_dir}"

  echo "[INFO] run ${run_id}/${RUNS}, size=${SIZE}, devices=${DEVICES}, output=${run_dir}"
  set +e
  python3 "${RUNNER}" \
    --type=rD2H \
    --transport=uboe \
    --soc_variant="${SOC_VARIANT}" \
    --device_ids="${DEVICES}" \
    --total_size="${SIZE}" \
    --buffer_size="${SIZE}" \
    --start_block_size="${SIZE}" \
    --max_block_size="${SIZE}" \
    --loops=1 \
    --output_dir="${run_dir}" \
    >"${log_file}" 2>&1
  rc=$?
  set -e

  if [[ ${rc} -eq 0 ]]; then
    pass_count=$((pass_count + 1))
  else
    fail_count=$((fail_count + 1))
  fi

  counts="$(python3 - "${log_file}" <<'PY'
import sys
path = sys.argv[1]
text = open(path, "r", errors="ignore").read()
done = text.count("D2H host flag done after stream sync")
not_visible = text.count("stream sync success but D2H host flag is not visible")
print(f"{done} {not_visible}")
PY
)"
  run_done="${counts%% *}"
  run_not_visible="${counts##* }"
  host_flag_done_count=$((host_flag_done_count + run_done))
  host_flag_not_visible_count=$((host_flag_not_visible_count + run_not_visible))

  echo "[INFO] run ${run_id} rc=${rc}, host_flag_done=${run_done}, host_flag_not_visible=${run_not_visible}"
done

echo "[RESULT] pass=${pass_count}, fail=${fail_count}, total=${RUNS}"
echo "[RESULT] host_flag_done=${host_flag_done_count}, host_flag_not_visible=${host_flag_not_visible_count}"
