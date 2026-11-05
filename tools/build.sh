#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mode="${1:-mock}"
case "$mode" in
  mock) mock=1 ;;
  live) mock=0 ;;
  *) echo 'Usage: sh tools/build.sh [mock|live]' >&2; exit 2 ;;
esac
cli="${ARDUINO_CLI:-/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli}"
if [ ! -f firmware/VtaBoard/config.h ]; then
  echo 'Copy firmware/VtaBoard/config.h.example to firmware/VtaBoard/config.h first.' >&2
  exit 1
fi
"$cli" compile \
  --fqbn 'esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=8M,PSRAM=opi,PartitionScheme=default_8MB' \
  --build-property "compiler.cpp.extra_flags=-DUSE_MOCK_DATA=$mock" \
  --build-path "$PWD/.build/$mode" firmware/VtaBoard
