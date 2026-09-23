#!/usr/bin/env bash
# -----------------------------------------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# -----------------------------------------------------------------------------------------------------------
# gp-wait.sh - 循环轮询流水线状态直到完成
#
# 用途: 自动每隔 60 秒查询一次流水线状态，直到状态变为 success/failed/canceled
# 适用: 触发 CI 后等待完成，避免手动重复调用 gp-list.sh
#
# 入参:
#   $1  PR 编号 (必填)
#
# 环境变量:
#   GITCODE_API_TOKEN  (必填)
#   GP_OWNER           (可选，默认从 git remote 自动检测)
#   GP_REPO            (可选，默认从 git remote 自动检测)
#
# 返回值 (stdout):
#   运行中: 每 60 秒输出一行状态摘要
#   完成时: 输出最终状态并退出 (exit 0 for success, exit 1 for failed/canceled)
#
# 示例:
#   $ ./gp-wait.sh 2479
#   [2026-05-27 10:05:00] status=running sha=794f3dad8c5e elapsed=0m
#   [2026-05-27 10:06:00] status=running sha=794f3dad8c5e elapsed=1m
#   [2026-05-27 10:07:00] status=success sha=794f3dad8c5e elapsed=2m
#   ✅ Pipeline completed: success
#
# 注意: 调用此脚本时必须设置足够长的超时时间（建议 60 分钟 / 3600000 ms）

set -euo pipefail

if [ -z "${1:-}" ]; then
  echo "用法: gp-wait.sh <PR_NUMBER>" >&2
  exit 1
fi

PR_NUMBER="$1"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
START_TIME=$(date +%s)
ITERATION=0

echo "开始轮询 PR #${PR_NUMBER} 流水线状态（每 60 秒检查一次）..."

while true; do
  ITERATION=$((ITERATION + 1))
  CURRENT_TIME=$(date +%s)
  ELAPSED_MINUTES=$(( (CURRENT_TIME - START_TIME) / 60 ))
  TIMESTAMP=$(date '+%Y-%m-%d %H:%M:%S')

  # 调用 gp-list.sh 获取当前状态
  # 必须 `|| true`：gp-list.sh 在 v5 接口无记录时以退出码 3 结束，
  # 本脚本 set -euo pipefail，直接赋值会在这一行静默退出，连"等待重试"分支都进不去。
  RESULT=$(bash "${SCRIPT_DIR}/gp-list.sh" "${PR_NUMBER}" 2>/dev/null || true)

  if [ -z "${RESULT}" ]; then
    if [ "$ITERATION" -eq 1 ]; then
      echo "[${TIMESTAMP}] ⚠️ v5 pipeline 接口对该 PR 返回 0 条，可能是：" >&2
      echo "    - 仓库已迁 v8 Actions → 跑 gp-detect.sh ${PR_NUMBER} 确认，改用 gpv8-wait.sh" >&2
      echo "    - Legacy 流水线由外部系统上报，v5 查不到 → 用 gp-comments.sh ${PR_NUMBER} 读 PR 评论" >&2
      echo "    推荐直接用 gp-gate-wait.sh ${PR_NUMBER}：以门禁 label 为终态，两套系统都覆盖。" >&2
    fi
    echo "[${TIMESTAMP}] 未找到流水线记录，等待 60 秒后重试..."
    sleep 60
    continue
  fi

  # 提取状态
  STATUS=$(echo "${RESULT}" | grep -oP 'status=\K[^ ]+')
  SHA=$(echo "${RESULT}" | grep -oP 'sha=\K[^ ]+')

  if [ "${STATUS}" = "running" ]; then
    echo "[${TIMESTAMP}] status=running sha=${SHA} elapsed=${ELAPSED_MINUTES}m (第 ${ITERATION} 次检查)"
    sleep 60
  else
    # 状态不再是 running，立即退出
    echo "[${TIMESTAMP}] status=${STATUS} sha=${SHA} elapsed=${ELAPSED_MINUTES}m"
    if [ "${STATUS}" = "success" ]; then
      echo "✅ 流水线完成: ${STATUS}"
      GATE=$(bash "${SCRIPT_DIR}/_gp_pr_meta.sh" "${PR_NUMBER}" gate 2>/dev/null || echo "unknown")
      echo "   门禁 label gate=${GATE}（能否合入以此为准；完整门禁面板见 gp-gate-wait.sh ${PR_NUMBER}）"
      exit 0
    else
      echo "❌ 流水线结束: ${STATUS}"
      # 交叉校验门禁 label：本脚本只盯 Legacy 一套，双 CI 并存的仓库里它失败不代表门禁失败
      GATE=$(bash "${SCRIPT_DIR}/_gp_pr_meta.sh" "${PR_NUMBER}" gate 2>/dev/null || echo "unknown")
      if [ "$GATE" != "failed" ]; then
        echo "⚠️ Legacy 流水线 ${STATUS}，但门禁 label gate=${GATE}（不是 failed）。" >&2
        echo "   两套 CI 结论可能不一致，请先复核再决定是否改代码：" >&2
        echo "     bash gp-gate-wait.sh ${PR_NUMBER} <分钟>   # 以门禁 label 为终态轮询（推荐）" >&2
        echo "     bash gp-comments.sh ${PR_NUMBER}           # 从 PR 评论读各系统 job 状态表" >&2
      fi
      exit 1
    fi
  fi
done
