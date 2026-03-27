#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST_ROOT="${1:-$ROOT_DIR/build/tg5040-sdk}"
DOWNLOAD_DIR="$DEST_ROOT/downloads"
SDK_ARCHIVE="$DOWNLOAD_DIR/SDK_usr_tg5040_a133p.tgz"
SDK_URL="${SDK_URL:-https://github.com/trimui/toolchain_sdk_smartpro/releases/download/20231018/SDK_usr_tg5040_a133p.tgz}"

mkdir -p "$DOWNLOAD_DIR"

if [[ ! -f "$SDK_ARCHIVE" ]]; then
  echo "[tg5040-sdk] downloading SDK_usr_tg5040_a133p.tgz"
  curl -L "$SDK_URL" -o "$SDK_ARCHIVE"
fi

echo "[tg5040-sdk] extracting to $DEST_ROOT/sdk_usr"
rm -rf "$DEST_ROOT/sdk_usr"
mkdir -p "$DEST_ROOT/sdk_usr"
tar -xzf "$SDK_ARCHIVE" -C "$DEST_ROOT/sdk_usr"

echo "[tg5040-sdk] ready: $DEST_ROOT/sdk_usr/usr"
