#!/bin/sh

APP_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
APP_NAME="WeRead"
DATA_DIR="$HOME/Library/Application Support/$APP_NAME"
FONT_FILE="/System/Library/Fonts/PingFang.ttc"

mkdir -p "$DATA_DIR"

exec "$APP_DIR/bin/weread" \
  ui \
  --data "$DATA_DIR" \
  --font "$FONT_FILE"
