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
# gp-comments.sh - 从 PR 评论里读机器人上报的流水线状态（Legacy 回退路径）
#
# 用途: 部分仓库的 Legacy 流水线由外部系统上报，v5 pipeline 接口查不到任何记录
#       （gp-list.sh 退出码 3），但 AtlasAccount 会在 PR 评论里贴出完整的流水线报告：
#       流水线名#号、commitID、顶层状态、以及每个 job 的状态表。
#       这是这类仓库获取 Legacy 状态的**唯一**途径。
#
#       实测 Ascend/torchair PR#3725：v5 接口在 type=report_pipeline / pipeline / all / 无 type
#       四种参数下全部 total=0，而评论里有 #4120/#4122/#4123 三份完整报告；
#       其中 #4123 全 job ✅ 且顶层 ✅，最终门禁 label 置为 ci-pipeline-passed。
#       只信 gp-list.sh 会得出"这个 PR 没有流水线"的错误结论。
#
# 适用: 步骤 2 的 Legacy 回退；gp-list.sh / gp-analyze-failure.sh 返回 no_record 时
#
# 入参:
#   $1  PR 编号 (必填)
#   $2  --raw  (可选) 额外输出每份报告的去标签原文（默认只输出解析后的摘要）
#
# 环境变量:
#   GITCODE_API_TOKEN  (必填)
#   GP_OWNER           (可选，默认从 git remote 自动检测；检测失败时可显式设置以便在任意目录运行)
#   GP_REPO            (可选，同上)
#
# 返回值 (stdout):
#   每份机器人报告一段：
#     [report] <时间> <流水线名#号> sha=<commitID> status=<已完成|运行失败|运行中|已终止运行>
#     [job]    <job名>=<状态> <job名>=<状态> ...
#   末尾输出门禁概览（来自 _gp_pr_meta.sh）：
#     [gate]   gate=<passed|failed|running|unknown> blocking=<...> advisory=<...> approval=<...>
#
# 退出码: 0=成功（含"没有机器人报告"）  1=用法/环境错  3=接口无该 PR 数据
#
# 示例:
#   $ ./gp-comments.sh 3725
#   [report] 2026-09-21T09:44:22+08:00 PR-pipeline_torchair#4123 sha=c9e343d2 status=已完成
#   [job]    Build_x86=COMPLETED Build_ARM=COMPLETED Antipoison=COMPLETED codecheck_pre-commit=COMPLETED pre-commit=COMPLETED SCA=COMPLETED PR-pipeline_torchair=COMPLETED
#   [gate]   gate=passed blocking= advisory=stat/needs-squash approval=lgtm:2/2,approve:1/1
#
# 注意:
#   1. 报告里的 job 状态表是 HTML 表格，本脚本用「标签换行 + awk 配对」解析：
#      把 <...> 全部换成换行，得到一格一行的单元格序列，凡是形如 "✅ COMPLETED" 的格子，
#      其前一个非空格子就是 job 名。
#   2. 顶层状态取中文词（已完成/运行失败/运行中/已终止运行），它与各 job 状态可能矛盾：
#      实测出现过「全部 job ✅ COMPLETED 但顶层 🟨 CANCELED」——那是被 concurrency.preemption
#      抢占取消的，不是代码问题。判断能否合入请以 [gate] 行的 label 为准。

set -euo pipefail

if [ -z "${1:-}" ]; then
  echo "用法: gp-comments.sh <PR_NUMBER> [--raw]" >&2
  exit 1
fi

PR_NUMBER="$1"
RAW=0
if [ -n "${2:-}" ]; then
  if [ "$2" = "--raw" ]; then
    RAW=1
  else
    echo "错误: 第二个参数只接受 --raw" >&2
    exit 1
  fi
fi

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

V5="https://api.gitcode.com/api/v5/repos/${GP_OWNER}/${GP_REPO}"
AUTH="Authorization: Bearer ${TOKEN}"

resp=$(curl -s "${V5}/pulls/${PR_NUMBER}/comments?per_page=100&page=1" -H "$AUTH" || true)
if [ -z "$resp" ] || ! printf '%s' "$resp" | jq -e 'type == "array"' >/dev/null 2>&1; then
  echo "no_record: 未取到 PR ${PR_NUMBER} 的评论（检查 PR 号与 GP_OWNER/GP_REPO，或该 PR 无评论）" >&2
  exit 3
fi

# 机器人上报的流水线报告：正文含 "流水线" 且含 commitID
count=$(printf '%s' "$resp" | jq -r '[.[] | select((.body // "") | test("commitID"))] | length' 2>/dev/null || echo 0)

if [ "${count:-0}" -eq 0 ]; then
  echo "(PR ${PR_NUMBER} 的评论里没有机器人流水线报告。可能 CI 未触发，或由 v8 Actions 承载 → 跑 gp-detect.sh ${PR_NUMBER})"
else
  # 逐条取出 时间/作者/正文。
  # 用 jq -c 输出「一条评论一行 JSON」再逐字段 jq -r 解析，不要用 \x1f + cut 拼分隔符：
  # cut -d 是逐行处理的，正文里不含分隔符的行会被原样打印（除非加 -s），
  # 结果整个 HTML 正文会混进 created / author 字段。
  printf '%s' "$resp" | jq -c '
    .[] | select((.body // "") | test("commitID"))
    | {t: .created_at, a: (.user.login // "?"), b: (.body // "")}
  ' 2>/dev/null |
    while IFS= read -r line; do
      [ -n "$line" ] || continue
      created=$(printf '%s' "$line" | jq -r '.t')
      author=$(printf '%s' "$line" | jq -r '.a')
      body=$(printf '%s' "$line" | jq -r '.b')

      # 去 HTML 标签 → 单元格一行一个
      cells=$(printf '%s' "$body" | sed 's/<[^>]*>/\n/g' | sed 's/^[[:space:]]*//; s/[[:space:]]*$//' | sed '/^$/d')

      pl_name=$(printf '%s\n' "$cells" | grep -oE '[A-Za-z0-9_.-]+#[0-9]+' | head -1 || true)
      pl_sha=$(printf '%s\n' "$cells" | grep -oE 'commitID：[0-9a-fA-F]+' | head -1 | sed 's/^commitID：//' || true)
      pl_state=$(printf '%s\n' "$cells" | grep -oE '(运行失败|已完成|运行中|已终止运行)' | head -1 || true)

      echo "[report] ${created} ${pl_name:-?} sha=${pl_sha:-?} status=${pl_state:-?} (by ${author})"

      # job=状态 配对：形如 "✅ COMPLETED" 的格子，其前一个非空格子是 job 名
      jobs=$(printf '%s\n' "$cells" | awk '
        {
          line = $0
          if (line ~ /(COMPLETED|FAILED|CANCELED|INIT|RUNNING|IGNORED)$/) {
            n = split(line, a, " ")
            if (prev != "") printf "%s=%s ", prev, a[n]
          }
          prev = line
        }')
      echo "[job]    ${jobs:-（未解析到 job 状态表，加 --raw 看原文）}"

      if [ "$RAW" -eq 1 ]; then
        echo "[raw]    $(printf '%s\n' "$cells" | tr '\n' ' ' | tr -s ' ')"
      fi
    done
fi

echo ""
# 门禁概览：能否合入以 label 为准，机器人报告的顶层状态可能是被抢占取消的
if meta=$(bash "${SCRIPT_DIR}/_gp_pr_meta.sh" "$PR_NUMBER" 2>/dev/null); then
  gate=$(printf '%s\n' "$meta" | sed -n 's/^gate=//p')
  blocking=$(printf '%s\n' "$meta" | sed -n 's/^blocking=//p')
  advisory=$(printf '%s\n' "$meta" | sed -n 's/^advisory=//p')
  approval=$(printf '%s\n' "$meta" | sed -n 's/^approval=//p')
  echo "[gate]   gate=${gate} blocking=${blocking:-无} advisory=${advisory:-无} approval=${approval}"
else
  echo "[gate]   （_gp_pr_meta.sh 取门禁信息失败，可单独执行排查）"
fi
