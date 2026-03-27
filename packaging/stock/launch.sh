#!/bin/sh

APP_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
SD_ROOT=$(CDPATH= cd -- "$APP_DIR/../.." && pwd)
APP_NAME="WeRead"
DATA_DIR="$SD_ROOT/Data/$APP_NAME"
LOG_DIR="$DATA_DIR/logs"

mkdir -p "$DATA_DIR" "$LOG_DIR"

export HOME="$DATA_DIR"
export PATH="$APP_DIR/bin/tg5040:$APP_DIR/bin:$PATH"
export LD_LIBRARY_PATH="$APP_DIR/lib/tg5040:$APP_DIR/lib:/usr/trimui/lib:/usr/lib:$LD_LIBRARY_PATH"

exec > "$LOG_DIR/launch.txt" 2>&1

chmod +x "$APP_DIR/bin/tg5040/"* 2>/dev/null

exec "$APP_DIR/bin/tg5040/weread" \
  --font "$APP_DIR/res/fonts/SourceHanSerifSC-Regular.otf" \
  --data "$DATA_DIR" \
  --platform tg5040
