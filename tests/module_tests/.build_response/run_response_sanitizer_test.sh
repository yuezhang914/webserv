#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PROJECT_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)
cd "$PROJECT_ROOT"

CXX=${CXX:-c++}
export EXTRA_CXXFLAGS="${EXTRA_CXXFLAGS:-} -fsanitize=address,undefined -fno-omit-frame-pointer"
export ASAN_OPTIONS="detect_leaks=1:halt_on_error=1"
export UBSAN_OPTIONS="halt_on_error=1"

"$SCRIPT_DIR/run_response_test.sh"
