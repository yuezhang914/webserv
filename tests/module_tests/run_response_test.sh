#!/bin/sh
# 文件：tests/module_tests/run_response_test.sh
# 用途：编译真实 Config、RequestParser 与拆分后的全部 Response .cpp，并运行不依赖 CGI 进程实现的 Response 模块回归测试。
# 测试边界：不启动 pipe/fork/poll/execve；CGI 只验证内部脚本路径交接和 stdout 到 Response 的转换。
set -eu

# 函数：fail
# 用途：统一打印 Response-only 测试的前置检查或编译失败原因，并立即结束脚本。
# 参数来源：$1 由文件检查、接口检查或依赖检查分支传入。
# 变量说明：本函数没有局部变量，直接读取第一个位置参数。
# 实现逻辑：把带有 [FAIL] 的错误消息写到标准错误，再以状态码 1 退出，避免继续测试不完整源码。
fail()
{
    echo "[FAIL] $1" >&2
    exit 1
}

# 函数：find_project_root
# 用途：从当前测试脚本目录逐层向上寻找真实 Webserv 项目根目录。
# 参数来源：无参数；起点使用脚本自身所在目录 SCRIPT_DIR。
# 变量说明：candidate 保存当前检查的目录路径。
# 实现逻辑：逐层检查 includes/Response.hpp 与 srcs/Response/Response.cpp；找到后输出路径，走到根目录仍未找到则返回失败。
find_project_root()
{
    candidate="$SCRIPT_DIR"
    while [ "$candidate" != "/" ]
    do
        if [ -f "$candidate/includes/Response.hpp" ] \
            && [ -f "$candidate/srcs/Response/Response.cpp" ]
        then
            printf '%s\n' "$candidate"
            return 0
        fi
        candidate=$(dirname "$candidate")
    done
    return 1
}

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PROJECT_ROOT=$(find_project_root) \
    || fail "找不到项目根目录：需要 includes/Response.hpp 和 srcs/Response/Response.cpp"
cd "$PROJECT_ROOT"
printf '%s\n' "[PASS] 已定位项目根目录：$PROJECT_ROOT"

CXX=${CXX:-c++}
BASE_CXXFLAGS="-Wall -Wextra -Werror -std=c++98 ${EXTRA_CXXFLAGS:-}"
BUILD_DIR="tests/module_tests/.build_response_only"
mkdir -p "$BUILD_DIR"

# 拆分后的 Response 源文件使用显式列表：既保证每个实现被编译，也让缺失文件在链接前就能给出清楚错误。
RESPONSE_HEADERS="includes/Response.hpp \
includes/ResponseInternal.hpp \
includes/EffectiveRoute.hpp \
includes/RequestHandler.hpp \
includes/RequestHandlerInternal.hpp"
RESPONSE_SOURCES="srcs/Response/Response.cpp \
srcs/Response/ResponseBuilder.cpp \
srcs/Response/ResponseHeaders.cpp \
srcs/Response/ResponseStatus.cpp \
srcs/Response/ResponseConnection.cpp \
srcs/Response/ResponseError.cpp \
srcs/Response/ResponseCgi.cpp \
srcs/Response/EffectiveRoute.cpp \
srcs/Response/EffectivePath.cpp \
srcs/Response/RequestHandler.cpp \
srcs/Response/RequestDirectory.cpp \
srcs/Response/RequestUpload.cpp \
srcs/Response/RequestDelete.cpp"
REQUEST_SOURCES="srcs/Request/Request.cpp \
srcs/Request/RequestParser.cpp \
srcs/Request/RequestParserRequestLine.cpp \
srcs/Request/RequestParserUri.cpp \
srcs/Request/RequestParserHeaders.cpp \
srcs/Request/RequestParserBody.cpp \
srcs/Request/RequestParserChunked.cpp"
CONFIG_SOURCES="srcs/Config/Config.cpp \
srcs/Config/ConfigParser.cpp \
srcs/Config/ConfigRouteUtils.cpp \
srcs/Config/LocationConfig.cpp \
srcs/Config/ServerConfig.cpp"
PROJECT_HEADERS="includes/Request.hpp \
includes/RequestParser.hpp \
includes/ConfigRouteUtils.hpp \
includes/ServerConfig.hpp \
includes/LocationConfig.hpp"

# 函数：check_required_files
# 用途：在编译前确认测试依赖的正式源码、拆分后新增文件和公共头文件全部存在。
# 参数来源：使用上方 RESPONSE_HEADERS、RESPONSE_SOURCES、REQUEST_SOURCES、CONFIG_SOURCES 与 PROJECT_HEADERS。
# 变量说明：file 依次保存当前检查路径。
# 实现逻辑：任一文件缺失立即调用 fail；全部存在时正常返回。
check_required_files()
{
    for file in $RESPONSE_HEADERS $RESPONSE_SOURCES \
        $REQUEST_SOURCES $CONFIG_SOURCES $PROJECT_HEADERS
    do
        [ -f "$file" ] || fail "缺少正式项目文件：$file"
    done
}

check_required_files

if [ ! -f includes/Config.hpp ] && [ ! -f srcs/Config/Config.hpp ]; then
    fail "缺少真实 Config.hpp（应位于 includes/ 或 srcs/Config/）"
fi

# 检查 Response 保持其他模块已经使用的公开接口，同时允许实现正常分布在多个 .cpp 中。
grep -q 'class Response' includes/Response.hpp \
    || fail "Response.hpp 未定义 class Response"
grep -q 'Response buildResponse' includes/Response.hpp \
    || fail "buildResponse 返回类型已改变；应继续返回 Response"
grep -q 'void parseCgiOutput' includes/Response.hpp \
    || fail "Response.hpp 缺少 ServerManager 已使用的 parseCgiOutput"
grep -q 'enum RequestAction' includes/EffectiveRoute.hpp \
    || fail "RequestAction 未合并到 EffectiveRoute.hpp"

# 检查拆分后没有回退到旧类型、旧宏、直接访问 Request 字段或文本包含式 .inc 方案。
[ ! -e includes/RequestAction.hpp ] \
    || fail "仍残留独立 RequestAction.hpp"
[ ! -e includes/ResponseUtils.hpp ] \
    || fail "仍残留 ResponseUtils.hpp"
[ ! -e srcs/Response/ResponseUtils.cpp ] \
    || fail "仍残留 ResponseUtils.cpp"
if find srcs/Response -type f -name '*.inc' | grep -q .; then
    fail "Response 仍使用 .inc 文本包含方案；应拆成独立 .cpp"
fi
if grep -R -n 'struct Response' includes srcs/Response >/dev/null 2>&1; then
    fail "仍残留旧 struct Response"
fi
if grep -R -n -E 'request\.(method|uri|version|headers|body|config|closeConnection)' \
    srcs/Response >/dev/null 2>&1; then
    fail "Response 仍直接访问旧 Request 公开字段"
fi
if grep -R -n -E '#define[[:space:]]+(GET|POST|DELETE)' \
    includes srcs/Response >/dev/null 2>&1; then
    fail "仍残留 GET/POST/DELETE 方法宏"
fi
if grep -R -n -E 'ResponseBuildResult|ResponseBuildKind' \
    includes/Response.hpp srcs/Response/*.cpp >/dev/null 2>&1; then
    fail "Response 仍要求其他模块迁移到 ResponseBuildResult"
fi
if ! grep -R -q 'X-Internal-CGI-Path' srcs/Response/*.cpp; then
    fail "CGI 分支未保留现有 ServerManager 使用的内部路径接口"
fi
grep -q 'parseCgiOutput' tests/module_tests/response_module_test.cpp \
    || fail "测试未覆盖 ServerManager 已使用的 parseCgiOutput"
grep -q '#include "Config.hpp"' tests/module_tests/response_module_test.cpp \
    || fail "测试未使用真实 Config"
if grep -q '#include "CgiHandler.hpp"' \
        tests/module_tests/response_module_test.cpp \
    || grep -q -E '(^|[^[:alnum:]_])CgiFds[[:space:]]+[A-Za-z_][A-Za-z0-9_]*' \
        tests/module_tests/response_module_test.cpp \
    || grep -q -E 'async_launch[[:space:]]*\(' \
        tests/module_tests/response_module_test.cpp; then
    fail "Response-only 测试不应包含 CGI 运行时类型或启动调用"
fi

printf '%s\n' "[PASS] buildResponse 继续返回 Response"
printf '%s\n' "[PASS] parseCgiOutput 兼容 ServerManager 现有调用"
printf '%s\n' "[PASS] CGI 内部路径接口可位于拆分后的 ResponseBuilder.cpp"
printf '%s\n' "[PASS] 测试编译全部拆分 .cpp，且不存在 .inc"
printf '%s\n' "[PASS] 测试不依赖 CgiHandler、CgiFds、pipe、fork、poll 或 waitpid"
printf '%s\n' "[PASS] 使用项目真实 Config、RequestParser 和 Response"
printf '%s\n' "[PASS] Config/test.cpp 不会被编入测试程序"

rm -rf tests/tmp_response_www
rm -f tests/module_tests/response_module_test.conf
mkdir -p \
    tests/tmp_response_www/upload \
    tests/tmp_response_www/readonly \
    tests/tmp_response_www/list \
    tests/tmp_response_www/alias_target \
    tests/tmp_response_www/delete_dir \
    tests/tmp_response_www/cgi \
    tests/tmp_response_www/text-index \
    tests/tmp_response_www/joined_upload

INCLUDE_FLAGS="-Iincludes -Isrcs/Config -Isrcs/Request -Isrcs/Response"

"$CXX" $BASE_CXXFLAGS $INCLUDE_FLAGS \
    tests/module_tests/response_module_test.cpp \
    $REQUEST_SOURCES \
    $CONFIG_SOURCES \
    $RESPONSE_SOURCES \
    -o "$BUILD_DIR/response_module_test"

printf '%s\n' "[PASS] Response-only 测试编译成功"
"$BUILD_DIR/response_module_test"
