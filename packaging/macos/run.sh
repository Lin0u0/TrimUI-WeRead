#!/bin/sh

APP_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)

exec "$APP_DIR/bin/weread" \
  --font "$APP_DIR/assets/fonts/SourceHanSerifSC-Regular.otf"
