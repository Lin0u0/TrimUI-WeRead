#!/bin/sh
PAK_DIR="$(dirname "$0")"
PAK_NAME="$(basename "$PAK_DIR" .pak)"

export HOME="$SHARED_USERDATA_PATH/$PAK_NAME"
mkdir -p "$HOME"

export PATH="$PAK_DIR/bin/$PLATFORM:$PAK_DIR/bin:$PATH"
export LD_LIBRARY_PATH="$PAK_DIR/lib/$PLATFORM:$LD_LIBRARY_PATH"
export CURL_CA_BUNDLE="$PAK_DIR/res/cacert.pem"

exec > "$LOGS_PATH/$PAK_NAME.txt" 2>&1

chmod +x "$PAK_DIR/bin/$PLATFORM/"*

weread \
  --font "/usr/trimui/res/regular.ttf" \
  --data "$HOME" \
  --cafile "$PAK_DIR/res/cacert.pem" \
  --platform "$PLATFORM"
