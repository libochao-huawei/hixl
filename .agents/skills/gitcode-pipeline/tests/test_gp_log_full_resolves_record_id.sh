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

if [[ "$url" == *"pipeline-runs/detail"* ]]; then
  printf '%s' '{"stages":[{"jobs":[{"id":"job-789","steps":[{"id":"step-123"}]}]}]}'
  exit 0
fi

if [[ "$url" == *"steps/gitcode/outputs"* ]]; then
  step_id=$(printf '%s' "$body" | jq -r '.step_run_ids')
  if [ "$step_id" != "step-123" ]; then
    echo "unexpected step id: $step_id" >&2
    exit 2
  fi
  printf '%s' '{"step_outputs":[{"output_result":[{"key":"recordId","value":"record-abc"}]}]}'
  exit 0
fi

if [[ "$url" == *raw.gitcode.com* ]]; then
  printf '%s\n' "$url" > "$FAKE_CURL_URL_FILE"
  if [[ "$url" != *"/build-log/record-abc/download-log"* ]]; then
    echo "unexpected download url: $url" >&2
    exit 3
  fi
  cp "$FAKE_LOG_FILE" "$out"
  exit 0
fi

echo "unexpected url: $url" >&2
exit 4
EOF
chmod +x "${FAKE_BIN}/curl"

printf 'resolved log\n' > "${WORK_DIR}/build.log.txt"

export FAKE_LOG_FILE="${WORK_DIR}/build.log.txt"
export FAKE_CURL_URL_FILE="${WORK_DIR}/curl_url"
export GITCODE_API_TOKEN="token-abc"
export GP_OWNER="cann"
export GP_REPO="ge"
export PATH="${FAKE_BIN}:${PATH}"

cd "${WORK_DIR}"
bash "${ROOT_DIR}/scripts/gp-log-full.sh" pipeline-123 run-456 job-789 '{}'

if ! cmp -s "${WORK_DIR}/build.log.txt" "${WORK_DIR}/pipeline_logs/job-789_full.log"; then
  echo "resolved log content mismatch" >&2
  exit 1
fi

if ! grep -q '/build-log/record-abc/download-log' "${WORK_DIR}/curl_url"; then
  echo "record id was not used for download" >&2
  exit 1
fi
