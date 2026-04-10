#!/usr/bin/env bash

set -euo pipefail

usage() {
  echo "usage: scripts/audit_trimui_package.sh <target> <archive>" >&2
  exit 1
}

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || {
    echo "missing required command: $1" >&2
    exit 1
  }
}

find_readelf() {
  if [[ -n "${READELF:-}" ]]; then
    printf '%s\n' "$READELF"
    return 0
  fi

  for candidate in aarch64-none-linux-gnu-readelf aarch64-linux-gnu-readelf; do
    if command -v "$candidate" >/dev/null 2>&1; then
      command -v "$candidate"
      return 0
    fi
  done

  echo "missing readelf for tg5040 audit" >&2
  exit 1
}

require_file() {
  local root="$1"
  local rel="$2"

  if [[ ! -f "$root/$rel" ]]; then
    echo "missing packaged file: $rel" >&2
    exit 1
  fi
}

require_text() {
  local path="$1"
  local needle="$2"

  if ! grep -Fq -- "$needle" "$path"; then
    echo "missing launcher assertion: $needle" >&2
    exit 1
  fi
}

require_needed() {
  local dynamic_file="$1"
  local lib="$2"

  if ! grep -Fq -- "Shared library: [$lib]" "$dynamic_file"; then
    echo "missing needed dependency: $lib" >&2
    exit 1
  fi
}

extract_archive() {
  local archive="$1"
  local dest="$2"

  case "$archive" in
    *.tar.gz)
      need_cmd tar
      tar -xzf "$archive" -C "$dest"
      ;;
    *.pakz)
      need_cmd unzip
      unzip -q "$archive" -d "$dest"
      ;;
    *)
      echo "unsupported archive format: $archive" >&2
      exit 1
      ;;
  esac
}

main() {
  [[ $# -eq 2 ]] || usage
  local target="$1"
  local archive="$2"
  local tmpdir
  local root
  local launch_path
  local binary_path
  local lib_root
  local dynamic_file
  local readelf_bin

  if [[ ! -f "$archive" ]]; then
    echo "missing archive: $archive" >&2
    exit 1
  fi

  case "$target" in
    nextui|stock|crossmix) ;;
    *) usage ;;
  esac

  need_cmd mktemp
  tmpdir="$(mktemp -d "${TMPDIR:-/tmp}/weread-package-audit.XXXXXX")"
  trap 'rm -rf "${tmpdir:-}"' EXIT

  extract_archive "$archive" "$tmpdir"

  if [[ "$target" == "nextui" ]]; then
    root="$tmpdir/Tools/tg5040/WeRead.pak"
    launch_path="$root/launch.sh"
    binary_path="$root/bin/tg5040/weread"
    lib_root="$root/lib/tg5040"
    require_file "$tmpdir" "Tools/tg5040/WeRead.pak/launch.sh"
    require_file "$tmpdir" "Tools/tg5040/WeRead.pak/bin/tg5040/weread"
    require_file "$tmpdir" "Tools/tg5040/WeRead.pak/res/cacert.pem"
    require_file "$tmpdir" "Tools/tg5040/WeRead.pak/lib/tg5040/libgcc_s.so.1"
    require_file "$tmpdir" "Tools/tg5040/WeRead.pak/pak.json"
    require_file "$tmpdir" "Tools/tg5040/.media/WeRead.png"
    require_text "$launch_path" 'HOME="$SHARED_USERDATA_PATH/$PAK_NAME"'
    require_text "$launch_path" 'PATH="$PAK_DIR/bin/$PLATFORM:$PAK_DIR/bin:$PATH"'
    require_text "$launch_path" 'LD_LIBRARY_PATH="$PAK_DIR/lib/$PLATFORM:$LD_LIBRARY_PATH"'
    require_text "$launch_path" 'CURL_CA_BUNDLE="$PAK_DIR/res/cacert.pem"'
    require_text "$launch_path" '--platform "$PLATFORM"'
    require_text "$launch_path" '--cafile "$PAK_DIR/res/cacert.pem"'
  else
    root="$tmpdir/Apps/WeRead"
    launch_path="$root/launch.sh"
    binary_path="$root/bin/tg5040/weread"
    lib_root="$root/lib/tg5040"
    require_file "$tmpdir" "Apps/WeRead/launch.sh"
    require_file "$tmpdir" "Apps/WeRead/bin/tg5040/weread"
    require_file "$tmpdir" "Apps/WeRead/res/cacert.pem"
    require_file "$tmpdir" "Apps/WeRead/lib/tg5040/libgcc_s.so.1"
    require_file "$tmpdir" "Apps/WeRead/config.json"
    require_text "$launch_path" 'DATA_DIR="$SD_ROOT/Data/$APP_NAME"'
    require_text "$launch_path" 'cp "$APP_DIR/bin/tg5040/weread" /tmp/weread'
    require_text "$launch_path" 'chmod +x /tmp/weread'
    require_text "$launch_path" 'CURL_CA_BUNDLE="$APP_DIR/res/cacert.pem"'
    require_text "$launch_path" '--platform tg5040'
    require_text "$launch_path" '--cafile "$APP_DIR/res/cacert.pem"'

    if [[ "$target" == "crossmix" ]]; then
      require_text "$launch_path" 'SD_ROOT="/mnt/SDCARD"'
      require_text "$launch_path" 'DATA_DIR="$SD_ROOT/Data/$APP_NAME"'
      require_text "$launch_path" 'LD_LIBRARY_PATH="$APP_DIR/lib/tg5040:$APP_DIR/lib:$SD_ROOT/System/lib:/usr/trimui/lib:/usr/lib:$LD_LIBRARY_PATH"'
    fi
  fi

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
    require_file "$lib_root" "$lib"
  done

  readelf_bin="$(find_readelf)"
  dynamic_file="$tmpdir/dynamic.txt"
  "$readelf_bin" -d "$binary_path" >"$dynamic_file"

  for lib in \
    libSDL2-2.0.so.0 \
    libSDL2_ttf-2.0.so.0 \
    libSDL2_image-2.0.so.0 \
    libfreetype.so.6 \
    libbz2.so.1.0 \
    libssl.so.1.1 \
    libcrypto.so.1.1 \
    libz.so.1
  do
    require_needed "$dynamic_file" "$lib"
  done
}

main "$@"
