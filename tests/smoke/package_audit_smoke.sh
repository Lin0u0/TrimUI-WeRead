#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TMPDIR_ROOT="${TMPDIR:-/tmp}"
WORK_DIR="$(mktemp -d "$TMPDIR_ROOT/weread-package-audit-smoke.XXXXXX")"
AUDIT_SCRIPT="$ROOT_DIR/scripts/audit_trimui_package.sh"

cleanup() {
  rm -rf "$WORK_DIR"
}

trap cleanup EXIT

make_tree() {
  local tree_root="$1"
  mkdir -p "$tree_root/Tools/tg5040/WeRead.pak/bin/tg5040"
  mkdir -p "$tree_root/Tools/tg5040/WeRead.pak/lib/tg5040"
  mkdir -p "$tree_root/Tools/tg5040/WeRead.pak/res"
  mkdir -p "$tree_root/Tools/tg5040/.media"

  cat >"$tree_root/Tools/tg5040/WeRead.pak/launch.sh" <<'EOF'
#!/bin/sh
HOME="$SHARED_USERDATA_PATH/$PAK_NAME"
PATH="$PAK_DIR/bin/$PLATFORM:$PAK_DIR/bin:$PATH"
LD_LIBRARY_PATH="$PAK_DIR/lib/$PLATFORM:$LD_LIBRARY_PATH"
CURL_CA_BUNDLE="$PAK_DIR/res/cacert.pem"
weread --cafile "$PAK_DIR/res/cacert.pem" --platform "$PLATFORM"
EOF
  : >"$tree_root/Tools/tg5040/WeRead.pak/bin/tg5040/weread"
  : >"$tree_root/Tools/tg5040/WeRead.pak/res/cacert.pem"
  : >"$tree_root/Tools/tg5040/WeRead.pak/pak.json"
  : >"$tree_root/Tools/tg5040/.media/WeRead.png"

  for lib in \
    libSDL2.so \
    libSDL2-2.0.so.0 \
    libSDL2_ttf.so \
    libSDL2_ttf-2.0.so.0 \
    libSDL2_image.so \
    libSDL2_image-2.0.so.0 \
    libfreetype.so \
    libfreetype.so.6 \
    libbz2.so \
    libbz2.so.1.0 \
    libssl.so.1.1 \
    libcrypto.so.1.1 \
    libz.so \
    libz.so.1 \
    libgcc_s.so.1
  do
    : >"$tree_root/Tools/tg5040/WeRead.pak/lib/tg5040/$lib"
  done
}

make_archive() {
  local tree_root="$1"
  local archive_path="$2"
  tar -C "$tree_root" -czf "$archive_path" Tools
}

READELF_STUB="$WORK_DIR/readelf-stub.sh"
cat >"$READELF_STUB" <<'EOF'
#!/usr/bin/env bash
cat <<'OUT'
 0x0000000000000001 (NEEDED)             Shared library: [libSDL2-2.0.so.0]
 0x0000000000000001 (NEEDED)             Shared library: [libSDL2_ttf-2.0.so.0]
 0x0000000000000001 (NEEDED)             Shared library: [libSDL2_image-2.0.so.0]
 0x0000000000000001 (NEEDED)             Shared library: [libfreetype.so.6]
 0x0000000000000001 (NEEDED)             Shared library: [libbz2.so.1.0]
 0x0000000000000001 (NEEDED)             Shared library: [libssl.so.1.1]
 0x0000000000000001 (NEEDED)             Shared library: [libcrypto.so.1.1]
 0x0000000000000001 (NEEDED)             Shared library: [libz.so.1]
OUT
EOF
chmod +x "$READELF_STUB"

PASS_TREE="$WORK_DIR/pass-tree"
PASS_ARCHIVE="$WORK_DIR/WeRead-nextui.tar.gz"
make_tree "$PASS_TREE"
make_archive "$PASS_TREE" "$PASS_ARCHIVE"

READELF="$READELF_STUB" "$AUDIT_SCRIPT" nextui "$PASS_ARCHIVE"

rm -f "$PASS_TREE/Tools/tg5040/WeRead.pak/res/cacert.pem"
FAIL_ARCHIVE="$WORK_DIR/WeRead-nextui-missing-cacert.tar.gz"
make_archive "$PASS_TREE" "$FAIL_ARCHIVE"

FAIL_LOG="$WORK_DIR/fail.log"
if READELF="$READELF_STUB" "$AUDIT_SCRIPT" nextui "$FAIL_ARCHIVE" >"$FAIL_LOG" 2>&1; then
  echo "package audit smoke failed: expected missing-file case to fail" >&2
  exit 1
fi

grep -Fq 'missing packaged file: Tools/tg5040/WeRead.pak/res/cacert.pem' "$FAIL_LOG" || {
  echo "package audit smoke failed: missing expected error text" >&2
  cat "$FAIL_LOG" >&2
  exit 1
}

printf '%s\n' "[test-package-audit-smoke] package audit helper pass/fail checks passed"
