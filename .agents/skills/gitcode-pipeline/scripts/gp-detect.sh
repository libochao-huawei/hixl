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
# gp-detect.sh - 检测仓库使用的 CI 系统模式（Actions v8 / Legacy Pipeline v5）
#
# 用途: GitCode 存在两套 CI 系统，不同仓库使用不同系统，不能按组织猜测
#       （实测 cann/ge 与 Ascend/pytorch 均已接入 Actions，部分仓库仍为 Legacy）。
#       本脚本通过特征探测自动识别，决定后续使用 gpv8-*.sh 还是 gp-*.sh 脚本族。
# 适用: 盯 CI 流程的步骤 0（每次盯 CI 前必须先执行）
#
# 入参:
#   $1  PR 编号 (可选。提供时按该 PR 的实际运行记录探测，结果最权威；
#       不提供时按仓库 workflow 声明探测)
#
# 环境变量:
#   GITCODE_API_TOKEN  (必填)
#   GP_OWNER           (可选，默认从 git remote 自动检测)
#   GP_REPO            (可选，默认从 git remote 自动检测)
#
# 返回值 (stdout):
#   mode=actions|legacy|none
#   evidence=<探测依据>（可能多行）
# 退出码: 0=actions, 1=legacy, 2=none/无法判断
#
# 示例:
#   $ ./gp-detect.sh 4991
#   mode=actions
#   evidence=v8 actions/runs 存在 PR 4991 的运行记录 (run_number=2360 workflow=PR-pipeline_ge status=FAILED)

set -euo pipefail

PR_NUMBER="${1:-}"
TOKEN="${GITCODE_API_TOKEN:?GITCODE_API_TOKEN 未设置}"

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
    exit 2
  fi
fi

AUTH_HEADER="Authorization: Bearer ${TOKEN}"
V8_BASE="https://api.gitcode.com/api/v8/repos/${GP_OWNER}/${GP_REPO}"
V5_BASE="https://api.gitcode.com/api/v5/repos/${GP_OWNER}/${GP_REPO}"

# 信号1a: v8 runs 中是否存在该 PR 的运行记录（最多扫描 3 页，命中即停）
v8_run_count=0
v8_latest=""
if [ -n "$PR_NUMBER" ]; then
  for page in 1 2 3; do
    resp=$(curl -s "${V8_BASE}/actions/runs?page=${page}&per_page=100" -H "$AUTH_HEADER" || true)
    page_hits=$(echo "$resp" | jq -r --arg pr "$PR_NUMBER" \
      '[.workflow_runs[]? | select(.pull_request_id == $pr)] | length' 2>/dev/null || echo 0)
    if [ "${page_hits:-0}" -gt 0 ] 2>/dev/null; then
      v8_run_count="$page_hits"
      v8_latest=$(echo "$resp" | jq -r --arg pr "$PR_NUMBER" \
        '[.workflow_runs[]? | select(.pull_request_id == $pr)] | sort_by(.start_time) | reverse | .[0] | "run_number=\(.run_number) workflow=\(.workflow_name) status=\(.status)"' 2>/dev/null || true)
      break
    fi
    total=$(echo "$resp" | jq -r '.total_count // 0' 2>/dev/null || echo 0)
    if [ "${total:-0}" -le $((page * 100)) ]; then
      break
    fi
  done
fi

# 信号1b: v5 pipeline 中是否存在该 PR 的记录
v5_total=0
if [ -n "$PR_NUMBER" ]; then
  v5_total=$(curl -s "${V5_BASE}/merge_requests/${PR_NUMBER}/pipeline?access_token=${TOKEN}&type=report_pipeline" \
    | jq -r '(.total // (.content | length) // 0)' 2>/dev/null || echo 0)
fi

# 信号2: 仓库是否声明 v8 workflows（.gitcode/workflows/*.yml）
wf_count=$(curl -s "${V8_BASE}/actions/workflows" -H "$AUTH_HEADER" \
  | jq -r '(.total_count // (.workflows | length) // 0)' 2>/dev/null || echo 0)

# 决策
if [ "$v8_run_count" -gt 0 ] && [ "${v5_total:-0}" -gt 0 ]; then
  echo "mode=actions"
  echo "evidence=两套系统均有 PR ${PR_NUMBER} 的记录，按约定优先使用 v8 Actions (${v8_latest})"
  exit 0
elif [ "$v8_run_count" -gt 0 ]; then
  echo "mode=actions"
  echo "evidence=v8 actions/runs 存在 PR ${PR_NUMBER} 的运行记录 (${v8_latest})"
  exit 0
elif [ "${v5_total:-0}" -gt 0 ]; then
  echo "mode=legacy"
  echo "evidence=v5 pipeline 接口存在 PR ${PR_NUMBER} 的记录 (total=${v5_total})"
  exit 1
elif [ -n "$PR_NUMBER" ]; then
  # PR 无任何运行记录（CI 未触发），回落到仓库 workflow 声明
  if [ "${wf_count:-0}" -gt 0 ]; then
    echo "mode=actions"
    echo "evidence=PR ${PR_NUMBER} 暂无运行记录，但仓库声明了 ${wf_count} 个 .gitcode/workflows，判定为 Actions"
    exit 0
  else
    echo "mode=legacy"
    echo "evidence=PR ${PR_NUMBER} 暂无运行记录，且仓库无 .gitcode/workflows，判定为 Legacy"
    exit 1
  fi
else
  if [ "${wf_count:-0}" -gt 0 ]; then
    echo "mode=actions"
    echo "evidence=仓库声明了 ${wf_count} 个 .gitcode/workflows"
    exit 0
  else
    echo "mode=none"
    echo "evidence=仓库无 .gitcode/workflows 声明（未接入 v8 Actions）"
    exit 2
  fi
fi
