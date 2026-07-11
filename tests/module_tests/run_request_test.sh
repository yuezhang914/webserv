#!/bin/sh
set -eu

# 本脚本可以从项目根目录或任意目录启动；先定位自身，再回到包含 srcs/ 和 includes/ 的项目根目录。
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PROJECT_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)
cd "$PROJECT_ROOT"

# 同时加入 includes 与各模块目录，兼容 Config 头文件放在 includes/ 或 srcs/Config/ 的两种项目结构。
CXX=${CXX:-c++}
CXXFLAGS="-Wall -Wextra -Werror -std=c++98 -Iincludes -Isrcs/Request -Isrcs/Config -I."
BUILD_DIR="tests/module_tests/.build"
mkdir -p "$BUILD_DIR"

# RequestParser 只依赖 Request、ServerConfig、LocationConfig 和 ConfigRouteUtils，不编译任何 Server/ClientIO 文件。
$CXX $CXXFLAGS \
    tests/module_tests/request_module_test.cpp \
    srcs/Request/Request.cpp \
    srcs/Request/RequestParser.cpp \
    srcs/Config/Config.cpp \
    srcs/Config/ConfigParser.cpp \
    srcs/Config/ConfigRouteUtils.cpp \
    srcs/Config/ServerConfig.cpp \
    srcs/Config/LocationConfig.cpp \
    -o "$BUILD_DIR/request_module_test"

"$BUILD_DIR/request_module_test"
