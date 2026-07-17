#!/bin/sh
set -eu

fail()
{
    echo "[FAIL] $1" >&2
    exit 1
}

# 从脚本所在位置向上寻找项目根目录，避免依赖当前工作目录，也允许 tests 目录被放在更深层。
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PROJECT_ROOT="$SCRIPT_DIR"
while [ "$PROJECT_ROOT" != "/" ]
do
    if [ -f "$PROJECT_ROOT/includes/Response.hpp" ] \
        && [ -f "$PROJECT_ROOT/srcs/Response/Response.cpp" ]
    then
        break
    fi
    PROJECT_ROOT=$(dirname "$PROJECT_ROOT")
done

if [ ! -f "$PROJECT_ROOT/includes/Response.hpp" ] \
    || [ ! -f "$PROJECT_ROOT/srcs/Response/Response.cpp" ]
then
    fail "找不到项目根目录：需要同时存在 includes/Response.hpp 和 srcs/Response/Response.cpp。请完整复制 includes、srcs/Response 和 tests，而不是只复制测试脚本。"
fi
cd "$PROJECT_ROOT"

CXX=${CXX:-c++}
BASE_CXXFLAGS="-Wall -Wextra -Werror -std=c++98 ${EXTRA_CXXFLAGS:-}"
BUILD_DIR="tests/module_tests/.build_response"
mkdir -p "$BUILD_DIR"

# 结构检查：Response 必须是 class，旧 public struct、旧独立枚举头和直接字段访问不得残留。
grep -q 'class Response' includes/Response.hpp || fail "Response.hpp 未定义 class Response"
grep -q 'enum RequestAction' includes/EffectiveRoute.hpp || fail "EffectiveRoute.hpp 未定义 RequestAction"
if [ -e includes/RequestAction.hpp ]; then
    fail "仍残留只包含枚举的 RequestAction.hpp"
fi
if grep -R -n 'struct Response' includes srcs/Response >/dev/null 2>&1; then
    fail "仍残留旧 struct Response"
fi
if grep -R -n -E '^[[:space:]]*(int[[:space:]]+checkRequestVersion|std::string[[:space:]]+getStatusMessage|bool[[:space:]]+isErrorStatusCode)[[:space:]]*\(' includes srcs/Response >/dev/null 2>&1; then
    fail "仍残留旧全局 Response 辅助接口"
fi
if grep -R -n -E '\.(statusCode|statusMessage|headers|body|closingConnection|version)[[:space:]]*=' srcs/Response >/dev/null 2>&1; then
    fail "Response 实现仍直接写旧公开字段"
fi
if grep -R -n -E 'request\.(method|uri|version|headers|body|config|closeConnection)' srcs/Response >/dev/null 2>&1; then
    fail "Response 仍直接访问旧 Request 字段"
fi
if grep -R -n -E '#define[[:space:]]+(GET|POST|DELETE)' includes srcs/Response >/dev/null 2>&1; then
    fail "仍残留 GET/POST/DELETE 宏"
fi
if [ -e includes/ResponseUtils.hpp ] || [ -e srcs/Response/ResponseUtils.cpp ]; then
    fail "冗余 ResponseUtils 文件仍存在"
fi
if grep -q 'struct File' includes/Response.hpp; then
    fail "上传 File 细节仍暴露在 Response.hpp"
fi
if grep -R -n 'size_t[[:space:]]\+length;' srcs/Response/RequestHandler.cpp >/dev/null 2>&1; then
    fail "FileOperation 仍保存冗余 body length"
fi
if grep -R -n 'value == "multipart/form-data"' srcs/Response >/dev/null 2>&1; then
    fail "未实现 multipart parser 时仍把 multipart 标记为支持"
fi

echo "[PASS] 已定位项目根目录：$PROJECT_ROOT"
echo "[PASS] RequestAction 已合并到 EffectiveRoute.hpp，独立头文件已删除"
echo "[PASS] Response 已改为 private class 封装"
echo "[PASS] 旧全局接口、ResponseUtils、旧 Request 字段调用和方法宏已清理"
echo "[PASS] Content-Length/Connection、无 body 状态和 header 大小写由 Response 内部维护"
echo "[PASS] FileOperation 冗余 length 与 multipart 伪支持已清理"

rm -rf tests/tmp_response_www
mkdir -p tests/tmp_response_www/upload \
         tests/tmp_response_www/readonly \
         tests/tmp_response_www/list \
         tests/tmp_response_www/alias_target \
         tests/tmp_response_www/delete_dir \
         tests/tmp_response_www/cgi \
         tests/tmp_response_www/text-index \
         tests/tmp_response_www/joined_upload

# 优先使用项目当前真实 Request/Config；若下载包被单独运行，则使用包内同接口测试支持文件。
USE_REAL=1
for file in \
    includes/Request.hpp \
    includes/RequestParser.hpp \
    includes/ConfigRouteUtils.hpp \
    includes/ServerConfig.hpp \
    includes/LocationConfig.hpp \
    srcs/Request/Request.cpp \
    srcs/Request/RequestParser.cpp \
    srcs/Request/RequestParserRequestLine.cpp \
    srcs/Request/RequestParserUri.cpp \
    srcs/Request/RequestParserHeaders.cpp \
    srcs/Request/RequestParserBody.cpp \
    srcs/Request/RequestParserChunked.cpp \
    srcs/Config/ConfigRouteUtils.cpp
do
    if [ ! -f "$file" ]; then
        USE_REAL=0
    fi
done

if [ "$USE_REAL" -eq 1 ]; then
    echo "[INFO] 使用项目中的真实 Request、RequestParser 和完整 Config 模块"
    INCLUDE_FLAGS="-Iincludes"
    REQUEST_SOURCES="srcs/Request/Request.cpp \
srcs/Request/RequestParser.cpp \
srcs/Request/RequestParserRequestLine.cpp \
srcs/Request/RequestParserUri.cpp \
srcs/Request/RequestParserHeaders.cpp \
srcs/Request/RequestParserBody.cpp \
srcs/Request/RequestParserChunked.cpp"
    # Config 的构造、析构和拷贝构造通常定义在其他 .cpp 中，不能只链接 ConfigRouteUtils.cpp。
    # 这里编译整个 srcs/Config 模块，确保真实 ServerConfig/LocationConfig 实现参与链接。
    CONFIG_SOURCES=$(find srcs/Config -type f -name '*.cpp' -print | sort | tr '\n' ' ')
    [ -n "$CONFIG_SOURCES" ] || fail "srcs/Config 中没有可编译的 .cpp 文件"
else
    echo "[INFO] 真实 Request/Config 不完整，使用下载包内测试支持文件"
    for file in \
        tests/test_support/Request.hpp \
        tests/test_support/RequestParser.hpp \
        tests/test_support/ConfigRouteUtils.hpp \
        tests/test_support/ServerConfig.hpp \
        tests/test_support/LocationConfig.hpp \
        tests/test_support/srcs/Request/Request.cpp \
        tests/test_support/srcs/Request/RequestParser.cpp \
        tests/test_support/srcs/Request/RequestParserRequestLine.cpp \
        tests/test_support/srcs/Request/RequestParserUri.cpp \
        tests/test_support/srcs/Request/RequestParserHeaders.cpp \
        tests/test_support/srcs/Request/RequestParserBody.cpp \
        tests/test_support/srcs/Request/RequestParserChunked.cpp \
        tests/test_support/srcs/Config/ConfigRouteUtils.cpp
    do
        [ -f "$file" ] || fail "缺少真实依赖且测试支持文件不存在：$file"
    done
    INCLUDE_FLAGS="-Itests/test_support -Iincludes"
    REQUEST_SOURCES="tests/test_support/srcs/Request/Request.cpp \
tests/test_support/srcs/Request/RequestParser.cpp \
tests/test_support/srcs/Request/RequestParserRequestLine.cpp \
tests/test_support/srcs/Request/RequestParserUri.cpp \
tests/test_support/srcs/Request/RequestParserHeaders.cpp \
tests/test_support/srcs/Request/RequestParserBody.cpp \
tests/test_support/srcs/Request/RequestParserChunked.cpp"
    CONFIG_SOURCES="tests/test_support/srcs/Config/ConfigRouteUtils.cpp"
fi

"$CXX" $BASE_CXXFLAGS $INCLUDE_FLAGS \
    tests/module_tests/response_module_test.cpp \
    $REQUEST_SOURCES \
    $CONFIG_SOURCES \
    srcs/Response/Response.cpp \
    srcs/Response/EffectiveRoute.cpp \
    srcs/Response/RequestHandler.cpp \
    -o "$BUILD_DIR/response_module_test"

"$BUILD_DIR/response_module_test"
