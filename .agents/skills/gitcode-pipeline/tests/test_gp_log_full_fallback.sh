#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
WORK_DIR="$(mktemp -d)"
FAKE_BIN="${WORK_DIR}/bin"

cleanup() {
  rm -rf "${WORK_DIR}"
}
trap cleanup EXIT

mkdir -p "${FAKE_BIN}"

cat > "${FAKE_BIN}/curl" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

out=""
url=""
body=""
while [ "$#" -gt 0 ]; do
  case "$1" in
    -o)
      out="$2"
      shift 2
      ;;
    --data-raw)
      body="$2"
      shift 2
      ;;
    http*)
      url="$1"
      shift
      ;;
    *)
      shift
      ;;
  esac
done

if [[ "$url" == *raw.gitcode.com* ]]; then
  printf '{"error_msg":"[record_id] 参数错误","error_code":"DEV.CB.032000"}' > "$out"
  exit 0
fi

if [[ "$url" == *"pipeline-runs/detail"* ]]; then
  printf '%s' '{"stages":[]}'
  exit 0
fi

if [[ "$url" != *"/pipelines/pipeline-123/pipeline-runs/run-456/jobs/job-789/logs"* ]]; then
  echo "unexpected fallback url: $url" >&2
  exit 2
fi

start=$(printf '%s' "$body" | jq -r '.start_offset')
if [ "$start" = "0" ]; then
  printf '%s' '{"log":"first\n","has_more":true,"start_offset":"6","end_offset":"10"}'
else
  printf '%s' '{"log":"second\n","has_more":false,"start_offset":"10","end_offset":"17"}'
fi
EOF
chmod +x "${FAKE_BIN}/curl"

export GITCODE_API_TOKEN="token-abc"
export GP_OWNER="cann"
export GP_REPO="ge"
export PATH="${FAKE_BIN}:${PATH}"

cd "${WORK_DIR}"
if bash "${ROOT_DIR}/scripts/gp-log-full.sh" pipeline-123 run-456 job-789 '{}' 2>"${WORK_DIR}/stderr"; then
  echo "expected download-log error" >&2
  exit 1
fi

if ! grep -q 'download-log 接口返回错误' "${WORK_DIR}/stderr"; then
  echo "missing download-log error" >&2
  exit 1
fi

if grep -q '/jobs/job-789/logs' "${WORK_DIR}/stderr"; then
  echo "should not use paged log API" >&2
  exit 1
fi
