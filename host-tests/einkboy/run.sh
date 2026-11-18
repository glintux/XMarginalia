#!/bin/bash
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
BUILD_DIR="$(mktemp -d)"
trap 'rm -rf "$BUILD_DIR"' EXIT

CXX_BIN="${CXX:-c++}"
if [ -n "${CC:-}" ]; then
  CC_BIN="$CC"
elif [[ "$CXX_BIN" == *g++* ]]; then
  CC_BIN="${CXX_BIN/g++/gcc}"
elif [[ "$CXX_BIN" == *clang++* ]]; then
  CC_BIN="${CXX_BIN/clang++/clang}"
else
  CC_BIN="cc"
fi

"$CC_BIN" -std=c11 -O2 -DSIMULATOR \
  -I"$ROOT/lib/PeanutGB/src" \
  -c "$ROOT/src/apps_local/einkboy/peanut_gb_impl.c" \
  -o "$BUILD_DIR/peanut_gb_impl.o"

"$CC_BIN" -std=c11 -O2 -DSIMULATOR -DWALNUT_CGB_KEEP_BUILD_OPT \
  -I"$ROOT/lib/WalnutCGB/src" \
  -c "$ROOT/src/apps_local/einkboy/walnut_cgb_impl.c" \
  -o "$BUILD_DIR/walnut_cgb_impl.o"

"$CXX_BIN" -std=c++17 -O2 -Wall -Wextra -Werror -Wno-comment -DSIMULATOR \
  -I"$ROOT/src/apps_local/einkboy" \
  -I"$ROOT/lib/PeanutGB/src" \
  -c "$ROOT/src/apps_local/einkboy/EinkBoyCore.cpp" \
  -o "$BUILD_DIR/EinkBoyCore.o"

"$CXX_BIN" -std=c++17 -O2 -Wall -Wextra -Werror -Wno-comment -DSIMULATOR \
  -I"$ROOT/src/apps_local/einkboy" \
  -I"$ROOT/lib/PeanutGB/src" \
  "$BUILD_DIR/peanut_gb_impl.o" \
  "$BUILD_DIR/walnut_cgb_impl.o" \
  "$BUILD_DIR/EinkBoyCore.o" \
  "$HERE/test_einkboy.cpp" \
  -o "$BUILD_DIR/test_einkboy"

"$BUILD_DIR/test_einkboy"
