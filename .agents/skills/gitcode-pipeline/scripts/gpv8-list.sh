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
# gpv8-list.sh - 查询 PR 的 v8 Actions 运行记录（最新一条）
#
# 用途: Actions 模式（mode=actions）下查询指定 PR 的流水线运行列表，提取最新一条的关键字段
# 适用: 步骤 2（查询流水线状态）
#
# 入参:
#   $1  PR 编号 (必填)
#
# 环境变量:
#   GITCODE_API_TOKEN  (必填)
#   GP_OWNER           (可选，默认从 git remote 自动检测)
#   GP_REPO            (可选，默认从 git remote 自动检测)
#
# 返回值 (stdout, 仅最新一条):
#   run_id=<32hex> run_number=<N> status=<RUNNING|COMPLETED|FAILED|CANCELED|ABORTED> sha=<12位> ref=<分支> workflow=<名称> event=<触发事件> created=<ISO8601 UTC>
#   无记录时返回空
#
# 示例:
#   $ ./gpv8-list.sh 4991
#   run_id=5a3f1fbf8970408d9300ac332bc04ee5 run_number=2360 status=FAILED sha=24e7f43ef48e ref=test-add-empty-line-cmakelists workflow=PR-pipeline_ge event=Note created=2026-09-16T07:41:24Z
#
# 注意: run 的 head_sha 是合并预览 SHA，与 PR head.sha 不相等属正常现象。
#       判断 run 归属用 pull_request_id（字符串比较），判断新旧用 start_time 最大值。

set -euo pipefail

if [ -z "${1:-}" ]; then
  echo "用法: gpv8-list.sh <PR_NUMBER>" >&2
  exit 1
fi

PR_NUMBER="$1"
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
    exit 1
  fi
fi

API="https://api.gitcode.com/api/v8/repos/${GP_OWNER}/${GP_REPO}/actions/runs"
AUTH_HEADER="Authorization: Bearer ${TOKEN}"

# 最多扫描 3 页，命中即停
for page in 1 2 3; do
  resp=$(curl -s "${API}?page=${page}&per_page=100" -H "$AUTH_HEADER" || true)
  hits=$(echo "$resp" | jq -r --arg pr "$PR_NUMBER" \
    '[.workflow_runs[]? | select(.pull_request_id == $pr)] | length' 2>/dev/null || echo 0)
  if [ "${hits:-0}" -gt 0 ] 2>/dev/null; then
    echo "$resp" | jq -r --arg pr "$PR_NUMBER" \
      '[.workflow_runs[]? | select(.pull_request_id == $pr)] | sort_by(.start_time) | reverse | .[0] |
       "run_id=\(.workflow_run_id) run_number=\(.run_number) status=\(.status) sha=\(.head_sha[0:12]) ref=\(.head_branch) workflow=\(.workflow_name) event=\(.event) created=\((.start_time/1000) | strftime("%Y-%m-%dT%H:%M:%SZ"))"' 2>/dev/null
    exit 0
  fi
  total=$(echo "$resp" | jq -r '.total_count // 0' 2>/dev/null || echo 0)
  if [ "${total:-0}" -le $((page * 100)) ]; then
    break
  fi
done
# 无记录
exit 0
