#!/usr/bin/env bash

set -euo pipefail

base_dir="${TMPDIR:-/tmp}/hixl-troubleshoot-deps"

mkdir -p "${base_dir}"

clone_if_missing() {
    local repo="$1"
    local dst="${base_dir}/${repo}"

    if [ -d "${dst}/.git" ]; then
        echo "${repo} already exists at ${dst}"
        return 0
    fi

    git clone "https://github.com/cann/${repo}.git" "${dst}"
}

clone_if_missing runtime
clone_if_missing hcomm
clone_if_missing driver

echo "Dependencies are available under ${base_dir}"
