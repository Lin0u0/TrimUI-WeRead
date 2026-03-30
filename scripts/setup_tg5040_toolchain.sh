#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST_ROOT="${1:-$ROOT_DIR/build/tg5040-toolchain}"
TOOLCHAIN_VERSION="${TOOLCHAIN_VERSION:-10.3-2021.07}"
TOOLCHAIN_BASENAME="gcc-arm-${TOOLCHAIN_VERSION}-x86_64-aarch64-none-linux-gnu"
TOOLCHAIN_URL="${TOOLCHAIN_URL:-https://developer.arm.com/-/media/Files/downloads/gnu-a/${TOOLCHAIN_VERSION}/binrel/${TOOLCHAIN_BASENAME}.tar.xz}"
DOWNLOAD_DIR="$DEST_ROOT/downloads"
ARCHIVE_PATH="$DOWNLOAD_DIR/${TOOLCHAIN_BASENAME}.tar.xz"
INSTALL_ROOT="$DEST_ROOT/${TOOLCHAIN_BASENAME}"

mkdir -p "$DOWNLOAD_DIR"

if [[ ! -d "$INSTALL_ROOT/bin" ]]; then
  if [[ ! -f "$ARCHIVE_PATH" ]]; then
    echo "[tg5040-toolchain] downloading ${TOOLCHAIN_BASENAME}.tar.xz"
    curl -L "$TOOLCHAIN_URL" -o "$ARCHIVE_PATH"
  fi

  echo "[tg5040-toolchain] extracting to $DEST_ROOT"
  rm -rf "$INSTALL_ROOT"
  tar -xJf "$ARCHIVE_PATH" -C "$DEST_ROOT"
fi

echo "[tg5040-toolchain] ready: $INSTALL_ROOT/bin/aarch64-none-linux-gnu-gcc"
