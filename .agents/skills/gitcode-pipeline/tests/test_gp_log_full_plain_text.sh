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
  printf '%s' '{"step_outputs":[{"output_result":[{"key":"recordId","value":"record-abc"}]}]}'
  exit 0
fi

if [[ "$url" == *raw.gitcode.com* ]]; then
  printf 'plain log\n' > "$out"
  exit 0
fi

echo "unexpected url: $url body: $body" >&2
exit 2
EOF
chmod +x "${FAKE_BIN}/curl"

export GITCODE_API_TOKEN="token-abc"
export GP_OWNER="cann"
export GP_REPO="ge"
export PATH="${FAKE_BIN}:${PATH}"

cd "${WORK_DIR}"
bash "${ROOT_DIR}/scripts/gp-log-full.sh" pipeline-123 run-456 job-789 '{}'

if ! grep -q 'plain log' "${WORK_DIR}/pipeline_logs/job-789_full.log"; then
  echo "plain text log was not saved" >&2
  exit 1
fi
