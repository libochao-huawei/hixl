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
# gpv8-wait.sh - 循环轮询 v8 Actions run 状态直到终态
#
# 用途: Actions 模式（mode=actions）下每 60 秒轮询 run 状态，输出 run + 各 stage 状态，直到终态
# 适用: 步骤 4（循环查询流水线状态）
#
# 入参:
#   $1  PR 编号或 run_id (必填)
#       传 PR 编号: 自动解析该 PR 最新 run 并轮询（运行记录出现前持续等待）
#       传 run_id:  直接轮询该 run（run_id 为 32 位十六进制字符串）
#
# 环境变量:
#   GITCODE_API_TOKEN  (必填)
#   GP_OWNER           (可选，默认从 git remote 自动检测)
#   GP_REPO            (可选，默认从 git remote 自动检测)
#
# 返回值 (stdout):
#   运行中: 每 60 秒输出一行 run 状态 + 各 stage 状态摘要
#   终态:   status=COMPLETED (成功, exit 0)
#           status=FAILED/CANCELED/ABORTED (失败, exit 1)
#
# 状态值对照 (v8 Actions):
#   run 级:  RUNNING -> COMPLETED(成功) | FAILED | CANCELED | ABORTED
#   stage 级: INIT -> RUNNING -> COMPLETED | FAILED
#
# 示例:
#   $ ./gpv8-wait.sh 4991
#   [16:03:05] run=RUNNING | PreBuild=COMPLETED Compile=COMPLETED LLT=RUNNING ...
#   [16:04:06] run=FAILED | PreBuild=COMPLETED Compile=COMPLETED LLT=FAILED ...
#   TERMINAL:FAILED
#
# 注意:
#   1. 调用此脚本必须设置 60 分钟超时（3600000ms）
#   2. 传 PR 编号且刚触发 CI 时，运行记录可能延迟 1~2 分钟才出现，脚本会持续等待（最多 10 分钟）
#   3. run 的 head_sha 是合并预览 SHA，与 PR head.sha 不相等属正常

set -euo pipefail

if [ -z "${1:-}" ]; then
  echo "用法: gpv8-wait.sh <PR_NUMBER|RUN_ID>" >&2
  exit 1
fi

TARGET="$1"
TOKEN="${GITCODE_API_TOKEN:?GITCODE_API_TOKEN 未设置}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [ -z "${GP_OWNER:-}" ] || [ -z "${GP_REPO:-}" ]; then
  repo_url=$(git remote get-url origin 2>/dev/null || true)
  if [[ "$repo_url" == git@* ]]; then
    GP_OWNER=$(echo "$repo_url" | sed 's|.*:\([^/]*\)/\([^/]*\)\.git$|\1|')
    GP_REPO=$(echo "$repo_url" | sed 's|.*:\([^/]*\)/\([^/]*\)\.git$|\2|')
  elif [[ "$repo_url" == *gitcode.com/* ]]; then
    GP_OWNER=$(echo "$repo_url" | sed 's|.*gitcode\.com/\([^/]*\)/\([^/]*\)\.git$|\1|')
    GP_REPO=$(echo "$repo_url" | sed 's|.*gitcode\.com/\([^/]*\)/\([^/]*\)\.git$|\2|')
  else
    echo "无法从 git remote 检测仓库信息，请设置 GP_OWNER 和 GP_REPO" >&2
    exit 1
  fi
fi

API_BASE="https://api.gitcode.com/api/v8/repos/${GP_OWNER}/${GP_REPO}/actions/runs"
AUTH_HEADER="Authorization: Bearer ${TOKEN}"

# 判断入参是 run_id（32 位 hex）还是 PR 编号
IS_RUN_ID=0
if [[ "$TARGET" =~ ^[0-9a-fA-F]{32}$ ]]; then
  IS_RUN_ID=1
  RUN_ID="$TARGET"
fi

START_TIME=$(date +%s)
NO_RECORD_WAITED=0

echo "开始轮询（每 60 秒检查一次）..."

while true; do
  TIMESTAMP=$(date '+%H:%M:%S')

  # PR 编号模式：每次都重新解析最新 run（防止重触发后盯的还是旧 run）
  if [ "$IS_RUN_ID" -eq 0 ]; then
    RESULT=$(bash "${SCRIPT_DIR}/gpv8-list.sh" "$TARGET" || true)
    if [ -z "$RESULT" ]; then
      if [ "$NO_RECORD_WAITED" -ge 600 ]; then
        echo "❌ 等待 ${NO_RECORD_WAITED}s 后仍无 PR ${TARGET} 的运行记录，请确认 CI 已触发"
        exit 1
      fi
      echo "[${TIMESTAMP}] 未找到运行记录（触发后注册可能延迟 1~2 分钟），继续等待..."
      NO_RECORD_WAITED=$((NO_RECORD_WAITED + 60))
      sleep 60
      continue
    fi
    RUN_ID=$(echo "$RESULT" | grep -oP 'run_id=\K[^ ]+')
  fi

  NO_RECORD_WAITED=0
  resp=$(curl -s "${API_BASE}/${RUN_ID}" -H "$AUTH_HEADER" || true)
  STATUS=$(echo "$resp" | jq -r '.status // empty' 2>/dev/null || true)
  STAGES=$(echo "$resp" | jq -r '[.stages[]? | "\(.name)=\(.status)"] | join(" ")' 2>/dev/null || true)
  RUN_NUM=$(echo "$resp" | jq -r '.run_number // "?"' 2>/dev/null || echo "?")

  echo "[${TIMESTAMP}] run#${RUN_NUM} status=${STATUS} | ${STAGES}"

  case "$STATUS" in
    COMPLETED)
      echo "TERMINAL:COMPLETED"
      exit 0
      ;;
    FAILED|CANCELED|ABORTED)
      echo "TERMINAL:${STATUS}"
      # 交叉校验门禁 label：本脚本只盯 v8 一套，双 CI 并存的仓库里 v8 失败不代表门禁失败。
      # 实测 Ascend/torchair PR#3725：v8 run #64/#66/#67 三次 FAILED（sca-pr/malicious-scan
      # 报"当前扫描仓库不在openlibing中"，平台配置缺失），而 Legacy #4123 全绿、
      # 门禁 label 最终置为 ci-pipeline-passed。只看本脚本会去修一个不存在的代码问题。
      if [ "$IS_RUN_ID" -eq 0 ]; then
        GATE=$(bash "${SCRIPT_DIR}/_gp_pr_meta.sh" "$TARGET" gate 2>/dev/null || echo "unknown")
        if [ "$GATE" != "failed" ]; then
          echo "⚠️ v8 run ${STATUS}，但门禁 label gate=${GATE}（不是 failed）。" >&2
          echo "   两套 CI 结论不一致：本脚本只盯 v8 Actions，门禁可能由 Legacy 流水线驱动。" >&2
          echo "   请先复核再决定是否改代码：" >&2
          echo "     bash gp-gate-wait.sh ${TARGET} <分钟>   # 以门禁 label 为终态轮询（推荐）" >&2
          echo "     bash gp-comments.sh ${TARGET}           # 从 PR 评论读 Legacy 各 job 状态表" >&2
        fi
      fi
      exit 1
      ;;
    "" )
      echo "[${TIMESTAMP}] run 详情查询异常（RUN_ID=${RUN_ID}），60 秒后重试..."
      ;;
  esac

  sleep 60
done
