#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TMPDIR_ROOT="${TMPDIR:-/tmp}"
WORK_DIR="$(mktemp -d "$TMPDIR_ROOT/weread-nextui-launch-smoke.XXXXXX")"
WORK_DIR="$(CDPATH= cd -- "$WORK_DIR" && pwd -P)"

cleanup() {
  rm -rf "$WORK_DIR"
}

trap cleanup EXIT

PAK_DIR="$WORK_DIR/Tools/tg5040/WeRead.pak"
SHARED_DIR="$WORK_DIR/shared"
LOG_DIR="$WORK_DIR/logs"
CAPTURE_FILE="$WORK_DIR/capture.txt"

mkdir -p "$PAK_DIR/bin/tg5040" "$PAK_DIR/lib/tg5040" "$PAK_DIR/res" "$SHARED_DIR" "$LOG_DIR"
cp "$ROOT_DIR/packaging/nextui/launch.sh" "$PAK_DIR/launch.sh"
chmod +x "$PAK_DIR/launch.sh"
: >"$PAK_DIR/res/cacert.pem"

cat >"$PAK_DIR/bin/tg5040/weread" <<'EOF'
#!/bin/sh
{
  printf 'HOME=%s\n' "$HOME"
  printf 'CURL_CA_BUNDLE=%s\n' "$CURL_CA_BUNDLE"
  printf 'ARGS=%s\n' "$*"
} >"$CAPTURE_FILE"
EOF
chmod +x "$PAK_DIR/bin/tg5040/weread"

assert_contains() {
  local file="$1"
  local needle="$2"

  if ! grep -Fq -- "$needle" "$file"; then
    printf 'nextui launch smoke failed: expected %s in %s\n' "$needle" "$file" >&2
    cat "$file" >&2
    exit 1
  fi
}

run_case() {
  local mode="$1"

  rm -f "$CAPTURE_FILE" "$LOG_DIR/WeRead.txt" "$LOG_DIR/..txt"
  rm -rf "$SHARED_DIR/WeRead"

  case "$mode" in
    absolute)
      SHARED_USERDATA_PATH="$SHARED_DIR" \
        LOGS_PATH="$LOG_DIR" \
        PLATFORM=tg5040 \
        CAPTURE_FILE="$CAPTURE_FILE" \
        "$PAK_DIR/launch.sh"
      ;;
    relative)
      (
        cd "$PAK_DIR"
        SHARED_USERDATA_PATH="$SHARED_DIR" \
          LOGS_PATH="$LOG_DIR" \
          PLATFORM=tg5040 \
          CAPTURE_FILE="$CAPTURE_FILE" \
          ./launch.sh
      )
      ;;
    *)
      printf 'unknown nextui launch smoke mode: %s\n' "$mode" >&2
      exit 1
      ;;
  esac

  test -d "$SHARED_DIR/WeRead" || {
    printf 'nextui launch smoke failed: missing shared WeRead directory for %s\n' "$mode" >&2
    exit 1
  }
  test -f "$LOG_DIR/WeRead.txt" || {
    printf 'nextui launch smoke failed: missing WeRead log for %s\n' "$mode" >&2
    exit 1
  }
  test ! -f "$LOG_DIR/..txt" || {
    printf 'nextui launch smoke failed: relative launch produced ..txt for %s\n' "$mode" >&2
    exit 1
  }

  assert_contains "$CAPTURE_FILE" "HOME=$SHARED_DIR/WeRead"
  assert_contains "$CAPTURE_FILE" "CURL_CA_BUNDLE=$PAK_DIR/res/cacert.pem"
  assert_contains "$CAPTURE_FILE" "--data $SHARED_DIR/WeRead"
  assert_contains "$CAPTURE_FILE" "--cafile $PAK_DIR/res/cacert.pem"
  assert_contains "$CAPTURE_FILE" "--platform tg5040"
}

run_case absolute
run_case relative

printf '%s\n' "[test-nextui-launch-smoke] absolute and relative launcher paths share WeRead data root"
