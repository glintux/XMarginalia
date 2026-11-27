#!/bin/sh
# Builds and runs the daybook core tests. DaybookCore is freestanding C++17.
#
#   host-tests/daybook/run.sh
set -e
cd "$(dirname "$0")"
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-daybook-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
SRC=../../src/apps_local/daybook
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O2 $SRC/DaybookCore.cpp test_daybook.cpp \
  -o "$BUILD_DIR/test_daybook"
"$BUILD_DIR/test_daybook"
