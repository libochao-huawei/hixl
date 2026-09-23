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
# gpv8-log.sh - 下载 v8 Actions 失败 job 的日志（自动递归解压 + 错误摘要）
#
# 用途: Actions 模式（mode=actions）下下载指定 job 的完整日志并提取错误摘要
# 适用: 步骤 5A.3（获取失败 Job 的日志）
#
# 入参:
#   $1  run_id  (必填，来自 gpv8-list.sh 的 run_id)
#   $2  job_id  (必填，来自 gpv8-detail.sh / gpv8-jobs.sh 的 id= 字段，优先用"执行 job"的 id)
#
# 环境变量:
#   GITCODE_API_TOKEN  (必填)
#   GP_OWNER           (可选，默认从 git remote 自动检测；检测失败时可显式设置，从而在任意目录运行)
#   GP_REPO            (可选，同上)
#   GP_LOG_DIR         (可选，日志落盘根目录，默认 ${TMPDIR:-/tmp}/gitcode_pipeline_logs)
#   GP_ANALYZE_LOG_DIR (可选，GP_LOG_DIR 的别名，兼容 SKILL.md 旧写法；GP_LOG_DIR 优先)
#
# 返回值 (stdout):
#   log_dir=<解压后的日志目录绝对路径>
#   ---- error summary ----
#   按信噪比分级输出，命中的才打印：
#   [Actions 失败注解]    （::error:: / ##[error]，Actions 官方失败标注，最多 10 行）
#   [gtest 失败用例]      （grep [  FAILED  ] 去重，最多 20 行）
#   [CTest 汇总]          （tests passed / ***Failed / The following tests FAILED）
#   [业务错误码/消息]     （"code":<数字> / "message":，外部服务返回的结构化错误）
#   [基础设施错误]        （[ERROR] [server] / CP.COMP<数字>，runner 与弹性资源异常）
#   [其他错误行]          （error/fatal 通用匹配，已过滤 GE 运行时 ERROR 噪音，最后 10 行）
#   全部未命中时打印 (未匹配到错误行，请查看 log_dir 下完整日志)
#
# 日志落盘（默认在 /tmp，不写 cwd，避免污染用户仓库）:
#   <LOG_ROOT>/<job_id>/<step名>.log                      （纯文本 step 日志）
#   <LOG_ROOT>/<job_id>/<step名>.log__unpacked/...        （嵌套 zip 解压结果）
#
# 关键实现细节（踩坑总结）:
#   1. download_log 接口返回一个 zip
#   2. zip 内的每个文件对应一个 step（文件名形如 4_ut_acc.log）
#   3. 部分文件本身还是 zip（双层嵌套），必须递归解压直到拿到纯文本
#      （嵌套 zip 解压后落在子目录里，必须用 find 递归扫描，顶层 glob 扫不到）
#   4. 中文步骤名的文件名在服务端编码有乱码（如 0_хИЭхзЛ...log），按序号前缀排序即可对应 step
#
# 示例:
#   $ ./gpv8-log.sh 5a3f1fbf8970408d9300ac332bc04ee5 1c837c66fc4d474b874be96b515104fb
#   log_dir=/path/to/pipeline_logs/1c837c66fc4d474b874be96b515104fb
#   ---- error summary ----
#   [gtest 失败用例]
#   [  FAILED  ] UtestGeApiV2.run_graph_with_stream
#   [CTest 汇总]
#   93% tests passed, 1 tests failed out of 14

set -euo pipefail

if [ $# -lt 2 ]; then
  echo "用法: gpv8-log.sh <RUN_ID> <JOB_ID>" >&2
  exit 1
fi

RUN_ID="$1"
JOB_ID="$2"
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

API="https://api.gitcode.com/api/v8/repos/${GP_OWNER}/${GP_REPO}/actions/runs/${RUN_ID}/jobs/${JOB_ID}/download_log"
AUTH_HEADER="Authorization: Bearer ${TOKEN}"

# 落盘根目录：默认 /tmp，绝不写 cwd。
# 原因：owner/repo 自动探测依赖 `git remote get-url origin`，即通常必须在仓库里执行；
# 若落盘用相对路径，就会在用户仓库里留下未跟踪的 pipeline_logs/，一个 git add -A 即被提交。
LOG_ROOT="${GP_LOG_DIR:-${GP_ANALYZE_LOG_DIR:-${TMPDIR:-/tmp}/gitcode_pipeline_logs}}"
OUT_DIR="${LOG_ROOT}/${JOB_ID}"
mkdir -p "${OUT_DIR}"

TMP_ZIP=$(mktemp /tmp/gpv8_log_XXXXXX.zip)
curl -sL "$API" -H "$AUTH_HEADER" -o "$TMP_ZIP"

if ! file "$TMP_ZIP" | grep -q "Zip archive"; then
  echo "下载的文件不是 zip，可能 job_id 无效或接口返回错误：" >&2
  head -c 300 "$TMP_ZIP" >&2
  exit 1
fi

# 递归解压函数：对目录下（含子目录）所有 zip 类文件递归解压，直到全部是纯文本
# 注意：必须用 find 递归扫描（嵌套 zip 解压后落在子目录里，顶层 glob 扫不到）
extract_recursive() {
  local dir="$1"
  local pass f sub remaining
  for pass in 1 2 3 4 5; do
    remaining=0
    while IFS= read -r f; do
      [ -f "$f" ] || continue
      if file -b "$f" | grep -q "Zip archive"; then
        sub="${f}__unpacked"
        mkdir -p "$sub"
        if unzip -o -q "$f" -d "$sub" 2>/dev/null; then
          rm -f "$f"
          remaining=1
        else
          rm -rf "$sub"
        fi
      fi
    done < <(find "$dir" -type f)
    [ "$remaining" -eq 0 ] && break
  done
}

unzip -o -q "$TMP_ZIP" -d "${OUT_DIR}"
rm -f "$TMP_ZIP"
extract_recursive "${OUT_DIR}"

echo "log_dir=$(cd "${OUT_DIR}" && pwd)"
echo "---- error summary ----"

# 分级提取错误信息，信噪比从高到低：
#   Actions 失败注解 > gtest 失败用例 > CTest 汇总 > 业务错误码 > 基础设施 > 通用 error/fatal
#
# 【重要】每处 grep 管道都必须以 `|| true` 收尾。本脚本 set -euo pipefail，grep 无匹配
# 返回 1 → pipefail 让整条管道返回 1 → 赋值语句返回 1 → set -e 直接退出脚本。
# 症状是 stdout 只打印到 "---- error summary ----" 就没了、退出码 1，看起来像"下载失败"，
# 实际是"日志里没有 gtest 的 [  FAILED  ]"——编译错误、SCA/配置错误、基础设施错误全中，
# 而这些恰恰是本脚本的主要用途。
pick() { grep -rhE "$1" "${OUT_DIR}" 2>/dev/null | head -n "$2" || true; }

actions_err=$(pick '::error::|##\[error\]' 10)
gtest_failed=$(grep -rh "\[  FAILED  \]" "${OUT_DIR}" 2>/dev/null \
  | sed 's/^.*\[  FAILED  \]/[  FAILED  ]/' | sort -u | head -20 || true)
ctest_summary=$(pick '[0-9]+% tests passed|The following tests FAILED|\*\*\*Failed' 10)
biz_error=$(pick '"code":[0-9]+|"message":' 10)
infra_error=$(pick '\[ERROR\] \[server\]|CP\.COMP[0-9]+' 5)

# 通用兜底噪音最大，排到最后，并剔除上面各级已经打印过的行，避免同一行重复出现
SHOWN_FILE=$(mktemp)
printf '%s\n' "${actions_err}" "${gtest_failed}" "${ctest_summary}" "${biz_error}" "${infra_error}" \
  | sed '/^$/d' | sort -u >"${SHOWN_FILE}"
generic_errors=$(grep -rhiE "error|fatal" "${OUT_DIR}" 2>/dev/null \
  | grep -viE "error_test|errorcode|error_code|error_msg|errorlist|Werror|ERROR\] GE\(" \
  | grep -Fxv -f "${SHOWN_FILE}" \
  | tail -10 || true)
rm -f "${SHOWN_FILE}"

# 末尾 return 0：否则 [ -n ] 为假时 && 链返回 1，又会被 set -e 打死
emit() {
  if [ -n "$2" ]; then
    echo "[$1]"
    echo "$2"
  fi
  return 0
}

emit "Actions 失败注解" "${actions_err}"
emit "gtest 失败用例" "${gtest_failed}"
emit "CTest 汇总" "${ctest_summary}"
emit "业务错误码/消息" "${biz_error}"
emit "基础设施错误" "${infra_error}"
emit "其他错误行（最后 10 行，已过滤 GE 运行时 ERROR 日志）" "${generic_errors}"

if [ -z "${actions_err}" ] && [ -z "${gtest_failed}" ] && [ -z "${ctest_summary}" ] \
  && [ -z "${biz_error}" ] && [ -z "${infra_error}" ] && [ -z "${generic_errors}" ]; then
  echo "(未匹配到错误行，请查看 log_dir 下完整日志)"
fi
