#!/bin/sh
set -eu

# 本脚本可以从任意目录启动；先回到包含 includes/、srcs/ 和 tests/ 的项目根目录。
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PROJECT_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)
cd "$PROJECT_ROOT"

# 结构检查：RequestParser 只能有一个 class 静态入口，旧自由函数和转发层不得恢复。
if grep -R "parseRequestBuffer\|parse_buffer_internal" \
    includes/RequestParser.hpp srcs/Request/RequestParser.cpp \
    tests/module_tests/request_module_test.cpp >/dev/null 2>&1; then
    echo "[FAIL] 发现旧解析入口或无意义转发层"
    exit 1
fi
if [ "$(grep -c 'static int parseBuffer' includes/RequestParser.hpp)" -ne 1 ]; then
    echo "[FAIL] RequestParser.hpp 必须只声明一个公开 parseBuffer"
    exit 1
fi
if [ "$(grep -c '^int RequestParser::parseBuffer' srcs/Request/RequestParser.cpp)" -ne 1 ]; then
    echo "[FAIL] RequestParser.cpp 必须只实现一个 parseBuffer"
    exit 1
fi

# 冗余检查：解析状态统一放在 enum；Request 不保存连接生命周期；内部 lowercase 不暴露为全局函数。
if grep -R "ERROR_MAX_BODY_LENGTH" \
    includes/Request.hpp includes/RequestParser.hpp \
    srcs/Request/Request.cpp srcs/Request/RequestParser.cpp \
    tests/module_tests/request_module_test.cpp >/dev/null 2>&1; then
    echo "[FAIL] 仍存在旧的 ERROR_MAX_BODY_LENGTH 宏状态"
    exit 1
fi
if grep -R "closeConnection\|shouldCloseConnection\|setCloseConnection" \
    includes/Request.hpp srcs/Request/Request.cpp \
    tests/module_tests/request_module_test.cpp >/dev/null 2>&1; then
    echo "[FAIL] Request 仍保存不属于解析结果的连接生命周期状态"
    exit 1
fi
if grep -R "std::string to_lower\|to_lower(" \
    includes/Request.hpp srcs/Request/Request.cpp \
    srcs/Request/RequestParser.cpp tests/module_tests/request_module_test.cpp \
    >/dev/null 2>&1; then
    echo "[FAIL] Request 模块仍暴露全局 lowercase 工具"
    exit 1
fi
if grep -R "has_required_host" \
    includes/RequestParser.hpp srcs/Request/RequestParser.cpp >/dev/null 2>&1; then
    echo "[FAIL] Host 必填检查仍存在额外转调用函数"
    exit 1
fi

# ConfigRouteUtils 是 Request body-limit 的直接依赖：不得保留冗余 bool 输出、const_cast 或再次截 query。
if grep -R "use_location\|const_cast\|stripQueryString" \
    includes/ConfigRouteUtils.hpp srcs/Config/ConfigRouteUtils.cpp >/dev/null 2>&1; then
    echo "[FAIL] Request 路由辅助函数仍有冗余状态、const_cast 或重复 query 截断"
    exit 1
fi

# 头文件只保留 includes/ 一份；同名旧副本会让不同源文件读取不同定义。
if [ -e srcs/Request/Request.hpp ] || [ -e srcs/Request/RequestParser.hpp ] \
    || [ -e srcs/Config/ConfigRouteUtils.hpp ]; then
    echo "[FAIL] srcs/ 中仍存在重复头文件，请只保留 includes/ 版本"
    exit 1
fi

echo "[PASS] RequestParser 只有一个 class 静态入口"
echo "[PASS] Request 状态、成员和内部工具已清除旧宏与冗余接口"
echo "[PASS] body-limit 路由辅助函数已移除重复 query 处理与 const_cast"
echo "[PASS] Request 相关头文件只保留 includes/ 一份"

# 使用 Subject 要求的 C++98、全部 warning 和 Werror；EXTRA_CXXFLAGS 用于 sanitizer 复测。
CXX=${CXX:-c++}
BASE_FLAGS="-Wall -Wextra -Werror -std=c++98 -Iincludes -I."
EXTRA_CXXFLAGS=${EXTRA_CXXFLAGS:-}
BUILD_DIR="tests/module_tests/.build"
mkdir -p "$BUILD_DIR"

# 完整项目中优先编译真实 Config；独立下载包中使用只含必要字段的 test_support 替身。
if [ -f srcs/Config/ServerConfig.cpp ] && [ -f srcs/Config/LocationConfig.cpp ]; then
    CXXFLAGS="$BASE_FLAGS -Isrcs/Config"
    CONFIG_SOURCES="srcs/Config/Config.cpp srcs/Config/ConfigParser.cpp srcs/Config/ConfigRouteUtils.cpp srcs/Config/ServerConfig.cpp srcs/Config/LocationConfig.cpp"
    echo "[INFO] 使用完整项目中的真实 Config 类型运行 Request 测试"
else
    CXXFLAGS="$BASE_FLAGS -Itests/module_tests/test_support"
    CONFIG_SOURCES="srcs/Config/ConfigRouteUtils.cpp"
    echo "[INFO] 使用下载包 test_support 运行独立 Request 测试"
fi

$CXX $CXXFLAGS $EXTRA_CXXFLAGS \
    tests/module_tests/request_module_test.cpp \
    srcs/Request/Request.cpp \
    srcs/Request/RequestParser.cpp \
    $CONFIG_SOURCES \
    -o "$BUILD_DIR/request_module_test"

"$BUILD_DIR/request_module_test"