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
# gp-list.sh - 查询 PR 流水线列表
#
# 用途: 查询指定 PR 的所有流水线状态，提取关键字段，大幅减少返回体积
# 适用: 轮询流水线状态、判断是否需要触发新流水线
#
# 入参:
#   $1  PR 编号 (必填)
#
# 环境变量:
#   GITCODE_API_TOKEN  (必填)
#   GP_OWNER           (可选，默认从 git remote 自动检测)
#   GP_REPO            (可选，默认从 git remote 自动检测)
#
# 返回值 (stdout, 纯文本，仅返回最新一条流水线):
#   id=<数字id> status=<状态> sha=<commit前12位> ref=<分支> created=<创建时间> pipeline_id=<长字符串> pipeline_run_id=<长字符串> pipeline_detail=<完整JSON>
#
# 退出码:
#   0=有记录  1=用法/环境错  3=v5 接口对该 PR 返回 0 条（stderr 输出 no_record 诊断与三条排查路径）
#
# 【重要】退出码 3 ≠ 该 PR 没有流水线。仓库已迁 v8 Actions、或 Legacy 流水线由外部系统上报
#   （v5 接口查不到但 AtlasAccount 一直在 PR 评论里贴完整 job 状态表）时都会返回 3。
#   调用方必须处理该分支，禁止拿空字符串继续往下走。
#
# 示例:
#   $ ./gp-list.sh 2479
#   id=526718 status=success sha=659baaa82e1a ref=fix/add-missing-semicolon-in-file-constant created=2026-04-30T16:08:16 pipeline_id=c85338dd... pipeline_run_id=159d8739... pipeline_detail={"hook_id":"42205",...}
#
#   $ ./gp-list.sh 3725        # Ascend/torchair，Legacy 由外部系统上报
#   no_record: v5 pipeline 接口对 PR 3725 返回 0 条（Ascend/torchair）。
#     可能1: 仓库已迁到 v8 Actions ...
#   (退出码 3)

set -euo pipefail

if [ -z "${1:-}" ]; then
  echo "用法: gp-list.sh <PR_NUMBER>" >&2
  exit 1
fi

PR_NUMBER="$1"
TOKEN="${GITCODE_API_TOKEN:?GITCODE_API_TOKEN 未设置}"

if [ -z "${GP_OWNER:-}" ] || [ -z "${GP_REPO:-}" ]; then
  repo_url=$(git remote get-url origin 2>/dev/null)
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

RESP=$(curl -s "https://api.gitcode.com/api/v5/repos/${GP_OWNER}/${GP_REPO}/merge_requests/${PR_NUMBER}/pipeline?access_token=${TOKEN}&type=report_pipeline" || true)

COUNT=$(printf '%s' "$RESP" | jq -r '(.content | length) // 0' 2>/dev/null || echo 0)

if [ "${COUNT:-0}" -eq 0 ]; then
  # 【重要】无输出 ≠ 该 PR 没有流水线。必须显式报错，否则调用方（含 gp-analyze-failure.sh）
  # 会拿着空字符串继续往下走并静默死掉，让人误判成"这个 PR 还没触发 CI"。
  cat >&2 <<EOF
no_record: v5 pipeline 接口对 PR ${PR_NUMBER} 返回 0 条（${GP_OWNER}/${GP_REPO}）。
  可能1: 仓库已迁到 v8 Actions，v5 接口本来就没有记录 → 跑 gp-detect.sh ${PR_NUMBER} 确认，
         mode=actions 时改用 gpv8-list.sh / gpv8-detail.sh / gpv8-log.sh。
  可能2: Legacy 流水线由外部系统上报，v5 接口查不到但流水线确实在跑
         （实测 Ascend/torchair PR#3725：v5 四种 type 参数全部 total=0，
           而 AtlasAccount 一直在评论 PR-pipeline_torchair#4120/#4122/#4123 的完整 job 状态表）
         → 用 gp-comments.sh ${PR_NUMBER} 从 PR 评论里读，或用 _gp_pr_meta.sh ${PR_NUMBER} 看门禁 label。
  可能3: CI 确实还没触发 → gp-trigger.sh ${PR_NUMBER}（注意先确认没有正在跑的 run，见该脚本的抢占保护）。
EOF
  exit 3
fi

printf '%s' "$RESP" | jq -r '.content[0] | "id=\(.id) status=\(.status) sha=\(.sha[0:12]) ref=\(.ref) created=\(.created_at) pipeline_id=\(.pipeline_id) pipeline_run_id=\(.pipeline_run_id) pipeline_detail=\(.pipeline_detail)"'
