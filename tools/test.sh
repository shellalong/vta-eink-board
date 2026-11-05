#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
JSON_SRC="${ARDUINOJSON_SRC:-$HOME/Documents/Arduino/libraries/ArduinoJson/src}"
mkdir -p .build
clang++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I firmware/VtaBoard -I "$JSON_SRC" tests/parser_test.cpp -o .build/parser_test
.build/parser_test
clang++ -std=c++17 -fsanitize=address,undefined \
  -I tests/host -I firmware/VtaBoard -I "$JSON_SRC" \
  tests/render_test.cpp firmware/VtaBoard/EPD_GUI.cpp -o .build/render_test
.build/render_test
