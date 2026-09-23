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
# gpv8-jobs.sh - 查询 v8 Actions run 的 jobs + steps 明细
#
# 用途: Actions 模式（mode=actions）下获取 run 的全部 job 及其 step 执行状态
# 适用: 需要完整步骤列表（含已完成的）或 gpv8-detail.sh 信息不足时
#
# 入参:
#   $1  run_id (必填，来自 gpv8-list.sh 输出的 run_id 字段)
#   $2  --all (可选，输出全部 job；默认只输出非 COMPLETED 的 job)
#
# 环境变量:
#   GITCODE_API_TOKEN  (必填)
#   GP_OWNER           (可选，默认从 git remote 自动检测)
#   GP_REPO            (可选，默认从 git remote 自动检测)
#
# 返回值 (stdout):
#   JOB: <name> id=<id> status=<status>
#     step[<seq>] <name>: <status>
#
# 示例:
#   $ ./gpv8-jobs.sh 5a3f1fbf8970408d9300ac332bc04ee5
#   JOB: UT_Test_ge_common id=53d06e26074d446e8b34c0ec117146f1 status=FAILED
#   JOB: ut id=1c837c66fc4d474b874be96b515104fb status=FAILED
#     step[0] 初始化步骤: COMPLETED
#     step[4] ut_acc: FAILED

set -euo pipefail

if [ -z "${1:-}" ]; then
  echo "用法: gpv8-jobs.sh <RUN_ID> [--all]" >&2
  exit 1
fi

RUN_ID="$1"
SHOW_ALL="${2:-}"
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

API="https://api.gitcode.com/api/v8/repos/${GP_OWNER}/${GP_REPO}/actions/runs/${RUN_ID}/jobs"
AUTH_HEADER="Authorization: Bearer ${TOKEN}"

if [ "$SHOW_ALL" = "--all" ]; then
  FILTER='.jobs[]?'
else
  FILTER='.jobs[]? | select(.status != "COMPLETED" and .status != "SUCCESS" and .status != "INIT")'
fi

curl -s "$API" -H "$AUTH_HEADER" | jq -r "
  $FILTER |
  \"JOB: \(.name) id=\(.id) status=\(.status)\",
  (.steps[]? | \"  step[\(.sequence)] \(.name): \(.status)\")" 2>/dev/null
