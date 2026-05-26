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
auth=""
while [ "$#" -gt 0 ]; do
  case "$1" in
    -o)
      out="$2"
      shift 2
      ;;
    --header|-H)
      auth="$2"
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

printf '%s\n' "$url" > "$FAKE_CURL_URL_FILE"
printf '%s\n' "$auth" > "$FAKE_CURL_AUTH_FILE"

if [[ "$url" != *"/pipeline/pipeline-123/run-456/build-log/record-789/download-log?region=cn-north-4&log_level=&compress="* ]]; then
  if [[ "$url" == *"pipeline-runs/detail"* ]]; then
    printf '%s' '{"stages":[]}'
    exit 0
  fi
  echo "unexpected url: $url" >&2
  exit 2
fi

if [ "$auth" != "Authorization: token-abc" ]; then
  echo "unexpected auth header: $auth" >&2
  exit 3
fi

cp "$FAKE_LOG_FILE" "$out"
EOF
chmod +x "${FAKE_BIN}/curl"

cat > "${WORK_DIR}/build.log.txt" <<'EOF'
line 1
[  FAILED  ] Suite.Test
line 3
EOF

export FAKE_LOG_FILE="${WORK_DIR}/build.log.txt"
export FAKE_CURL_URL_FILE="${WORK_DIR}/curl_url"
export FAKE_CURL_AUTH_FILE="${WORK_DIR}/curl_auth"
export GITCODE_API_TOKEN="token-abc"
export GP_OWNER="cann"
export GP_REPO="ge"
export PATH="${FAKE_BIN}:${PATH}"

cd "${WORK_DIR}"
output=$(bash "${ROOT_DIR}/scripts/gp-log-full.sh" pipeline-123 run-456 record-789 '{}')

if ! echo "$output" | grep -q 'pipeline_logs/record-789_full.log'; then
  echo "missing output path" >&2
  exit 1
fi

if ! cmp -s "${WORK_DIR}/build.log.txt" "${WORK_DIR}/pipeline_logs/record-789_full.log"; then
  echo "downloaded log content mismatch" >&2
  exit 1
fi

if ! grep -q 'raw.gitcode.com/cann/ge/pipeline/pipeline-123/run-456/build-log/record-789/download-log' "${WORK_DIR}/curl_url"; then
  echo "new download endpoint was not used" >&2
  exit 1
fi
