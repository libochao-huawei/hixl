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

printf '%s' '{"step_outputs":[{"output_result":[{"key":"pipeline_id","value":"pipe-123"},{"key":"pipeline_run_id","value":"run-456"},{"key":"recordId","value":"record-abc"}]}]}'
EOF
chmod +x "${FAKE_BIN}/curl"

export GITCODE_API_TOKEN="token-abc"
export GP_OWNER="cann"
export GP_REPO="ge"
export PATH="${FAKE_BIN}:${PATH}"

output=$(bash "${ROOT_DIR}/scripts/gp-sub-output.sh" parent-pipe parent-run '{}' step-123)

if ! echo "$output" | grep -q 'sub_record_id=record-abc'; then
  echo "missing record id in output: $output" >&2
  exit 1
fi

if ! echo "$output" | grep -q 'sub_pipeline_id=pipe-123'; then
  echo "missing pipeline id in output: $output" >&2
  exit 1
fi

if ! echo "$output" | grep -q 'sub_pipeline_run_id=run-456'; then
  echo "missing pipeline run id in output: $output" >&2
  exit 1
fi
