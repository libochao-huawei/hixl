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
# gpv8-detail.sh - 查询 v8 Actions run 详情（stage/job/step）
#
# 用途: Actions 模式（mode=actions）下获取 run 的整体状态、各阶段状态和失败 job 列表
# 适用: 步骤 5A.1（失败处理 — 分析失败 Job）
#
# 入参:
#   $1  run_id (必填，来自 gpv8-list.sh 输出的 run_id 字段)
#
# 环境变量:
#   GITCODE_API_TOKEN  (必填)
#   GP_OWNER           (可选，默认从 git remote 自动检测)
#   GP_REPO            (可选，默认从 git remote 自动检测)
#
# 返回值 (stdout):
#   workflow_name=<名称> status=<状态> run_number=<N> sha=<12位> branch=<分支>
#   [stage] <name>: <status>                                    # 所有阶段
#   [job]   <name>: <status> id=<job_id> (failed_steps: ...)     # 仅 FAILED/CANCELED 的 job
#
# 示例:
#   $ ./gpv8-detail.sh 5a3f1fbf8970408d9300ac332bc04ee5
#   workflow_name=PR-pipeline_ge status=FAILED run_number=2360 sha=24e7f43ef48e branch=test-add-empty-line-cmakelists
#   [stage] PreBuild: COMPLETED
#   [stage] Compile: COMPLETED
#   [stage] LLT: FAILED
#   [job]   UT_Test_ge_common: FAILED id=53d06e26074d446e8b34c0ec117146f1
#   [job]   ut: FAILED id=1c837c66fc4d474b874be96b515104fb (failed_steps: ut_acc)
#
# 注意: Actions 模式下同一任务常有一个"展示 job"（如 UT_Test_ge_common，无 steps）
#       和一个"执行 job"（如 ut，带 steps）。失败 step 和日志都挂在执行 job 上，
#       下载日志（gpv8-log.sh）必须用执行 job 的 id。

set -euo pipefail

if [ -z "${1:-}" ]; then
  echo "用法: gpv8-detail.sh <RUN_ID>" >&2
  exit 1
fi

RUN_ID="$1"
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

API="https://api.gitcode.com/api/v8/repos/${GP_OWNER}/${GP_REPO}/actions/runs/${RUN_ID}"
AUTH_HEADER="Authorization: Bearer ${TOKEN}"

curl -s "$API" -H "$AUTH_HEADER" | jq -r '
  if .workflow_run_id then
    "workflow_name=\(.workflow_name) status=\(.status) run_number=\(.run_number) sha=\(.head_sha[0:12]) branch=\(.head_branch)",
    (.stages[]? | "[stage] \(.name): \(.status)"),
    (.stages[].jobs[]? | select(.status == "FAILED" or .status == "CANCELED") |
      "[job]   \(.name): \(.status) id=\(.id)" +
      (if ([.steps[]? | select(.status == "FAILED" or .status == "CANCELED")] | length) > 0
       then " (failed_steps: " + ([.steps[]? | select(.status == "FAILED" or .status == "CANCELED") | .name] | join(", ")) + ")"
       else "" end))
  else
    empty
  end' 2>/dev/null
