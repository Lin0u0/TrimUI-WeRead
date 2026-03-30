#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PREFIX_ROOT="${1:-$ROOT_DIR/third_party/tg5040}"
SDK_ROOT="${TG5040_SDK_ROOT:-$ROOT_DIR/build/tg5040-sdk}"
SDK_USR="$SDK_ROOT/sdk_usr/usr"
BUILD_ROOT="$ROOT_DIR/build/tg5040-deps"
SRC_ROOT="$BUILD_ROOT/src"
WORK_ROOT="$BUILD_ROOT/work"
STAMP_ROOT="$BUILD_ROOT/stamps"

CC="${CC:-${CROSS_PREFIX:-aarch64-linux-gnu-}gcc}"
TARGET="${TARGET:-$($CC -dumpmachine 2>/dev/null || echo aarch64-linux-gnu)}"
CROSS_PREFIX="${CROSS_PREFIX:-${TARGET}-}"
AR="${AR:-${CROSS_PREFIX}ar}"
RANLIB="${RANLIB:-${CROSS_PREFIX}ranlib}"
SYSROOT="${SYSROOT:-$($CC -print-sysroot 2>/dev/null || true)}"
JOBS="${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}"

CURL_VERSION="${CURL_VERSION:-8.12.1}"
CURL_URL="${CURL_URL:-https://curl.se/download/curl-${CURL_VERSION}.tar.xz}"
CURL_SHA256="${CURL_SHA256:-0341f1ed97a26c811abaebd37d62b833956792b7607ea3f15d001613c76de202}"
CURL_PREFIX="$PREFIX_ROOT/curl"

log() {
  printf '\n[%s] %s\n' "$(date '+%H:%M:%S')" "$*"
}

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || {
    echo "missing required command: $1" >&2
    exit 1
  }
}

fetch() {
  local archive="$SRC_ROOT/curl-${CURL_VERSION}.tar.xz"

  mkdir -p "$SRC_ROOT"
  if [[ ! -f "$archive" ]]; then
    log "downloading curl ${CURL_VERSION}"
    curl -L "$CURL_URL" -o "$archive"
  fi

  printf '%s  %s\n' "$CURL_SHA256" "$archive" | shasum -a 256 -c -
}

main() {
  need_cmd curl
  need_cmd shasum
  need_cmd tar
  need_cmd "$CC"

  [[ -d "$SDK_USR/include" ]] || {
    echo "missing TrimUI SDK at $SDK_USR" >&2
    echo "run: make tg5040-sdk" >&2
    exit 1
  }

  if [[ -f "$CURL_PREFIX/lib/libcurl.a" && -f "$CURL_PREFIX/include/curl/curl.h" ]]; then
    log "using cached curl prefix: $CURL_PREFIX"
    exit 0
  fi

  fetch

  mkdir -p "$WORK_ROOT" "$STAMP_ROOT" "$PREFIX_ROOT"

  log "building curl ${CURL_VERSION} against SDK: $SDK_USR"
  rm -rf "$WORK_ROOT/curl-${CURL_VERSION}" "$CURL_PREFIX"
  mkdir -p "$WORK_ROOT/curl-${CURL_VERSION}" "$CURL_PREFIX"
  tar -xJf "$SRC_ROOT/curl-${CURL_VERSION}.tar.xz" -C "$WORK_ROOT/curl-${CURL_VERSION}" --strip-components=1

  (
    cd "$WORK_ROOT/curl-${CURL_VERSION}"
    PKG_CONFIG_LIBDIR="$SDK_USR/lib/pkgconfig" \
    PKG_CONFIG_SYSROOT_DIR="$SDK_ROOT/sdk_usr" \
    CC="$CC" \
    AR="$AR" \
    RANLIB="$RANLIB" \
    ./configure \
      --host="$TARGET" \
      --prefix="$CURL_PREFIX" \
      --with-openssl="$SDK_USR" \
      --with-zlib="$SDK_USR" \
      --disable-shared \
      --enable-static \
      --without-libpsl \
      --without-brotli \
      --without-zstd \
      --disable-ldap \
      --disable-ldaps \
      --disable-manual
    make -j"$JOBS"
    make install
  )

  log "finished"
  log "curl prefix: $CURL_PREFIX"
  log "sysroot: $SYSROOT"
}

main "$@"
