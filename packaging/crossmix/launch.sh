#!/bin/sh

APP_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
SD_ROOT="/mnt/SDCARD"
APP_NAME="WeRead"
DATA_DIR="$SD_ROOT/Data/$APP_NAME"
LOG_DIR="$DATA_DIR/logs"

mkdir -p "$DATA_DIR" "$LOG_DIR"

export HOME="$DATA_DIR"
export LD_LIBRARY_PATH="$APP_DIR/lib/tg5040:$APP_DIR/lib:$SD_ROOT/System/lib:/usr/trimui/lib:/usr/lib:$LD_LIBRARY_PATH"
export CURL_CA_BUNDLE="$APP_DIR/res/cacert.pem"

exec > "$LOG_DIR/launch.txt" 2>&1

# SD card is vfat — cannot exec directly, copy binary to /tmp
cp "$APP_DIR/bin/tg5040/weread" /tmp/weread
chmod +x /tmp/weread

/tmp/weread \
  --font "/usr/trimui/res/regular.ttf" \
  --data "$DATA_DIR" \
  --cafile "$APP_DIR/res/cacert.pem" \
  --platform tg5040

rm -f /tmp/weread
