#!/bin/sh
set -eu

# 本脚本可以从项目根目录或任意目录启动；先定位自身，再回到包含 srcs/ 和 includes/ 的项目根目录。
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PROJECT_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)
cd "$PROJECT_ROOT"

# 使用项目要求的 C++98、全部 warning 和 Werror；同时兼容头文件集中在 includes/ 的当前目录结构。
CXX=${CXX:-c++}
CXXFLAGS="-Wall -Wextra -Werror -std=c++98 -Iincludes -Isrcs/Request -Isrcs/Config -I."
BUILD_DIR="tests/module_tests/.build"
mkdir -p "$BUILD_DIR"

# RequestParser 会使用 ServerConfig、LocationConfig 和 ConfigRouteUtils。
# 当前 Config 的 server/location 生命周期实现与 Config 工具函数位于多个 .cpp，测试一起编译它们，避免链接阶段缺少 Config::split/parseSize 等符号。
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
