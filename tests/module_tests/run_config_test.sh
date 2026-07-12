#!/usr/bin/env bash
set -u

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
BINARY="$SCRIPT_DIR/.config_module_test"
RESULT_FILE="$SCRIPT_DIR/config_test_results.txt"

CXX="${CXX:-c++}"
CXXFLAGS="-Wall -Wextra -Werror -std=c++98"

SOURCES=(
    "$SCRIPT_DIR/config_module_test.cpp"
    "$ROOT_DIR/srcs/Config/Config.cpp"
    "$ROOT_DIR/srcs/Config/ConfigParser.cpp"
    "$ROOT_DIR/srcs/Config/ConfigRouteUtils.cpp"
    "$ROOT_DIR/srcs/Config/LocationConfig.cpp"
    "$ROOT_DIR/srcs/Config/ServerConfig.cpp"
)

INCLUDES=(
    -I"$ROOT_DIR/includes"
    -I"$ROOT_DIR/srcs"
    -I"$ROOT_DIR/srcs/Config"
    -I"$ROOT_DIR/srcs/Utils"
)

cleanup() {
    rm -f "$BINARY"
}
trap cleanup EXIT

printf '%s\n' "[1/2] Compiling Config strict module tests..."
if ! "$CXX" $CXXFLAGS "${INCLUDES[@]}" "${SOURCES[@]}" -o "$BINARY"; then
    printf '%s\n' "[ERROR] Compilation failed."
    exit 2
fi

printf '%s\n' "[2/2] Running tests..."
set +e
"$BINARY" | tee "$RESULT_FILE"
STATUS=${PIPESTATUS[0]}
set -e

printf '\nResults saved to: %s\n' "$RESULT_FILE"
exit "$STATUS"
