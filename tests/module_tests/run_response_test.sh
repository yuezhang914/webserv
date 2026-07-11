#!/bin/sh
set -eu

# 函数：fail_if_forbidden_include_exists
# 用途：在编译前检查 mandatory Response 是否还直接包含尚未实现的 Session/CGI 头文件。
# 参数：无；检查当前项目的 srcs/Response 目录。
# 返回值：发现禁止依赖时退出 1；没有发现时正常返回。
# 实现逻辑：只检查真正的 #include 行，避免普通注释中的模块名称造成误报。
fail_if_forbidden_include_exists()
{
    if grep -R -n -E '^[[:space:]]*#include.*(SessionManager|CgiProtocol|CgiRuntime)' srcs/Response >/tmp/webserv_forbidden_response_includes.txt 2>/dev/null; then
        echo "[FAIL] Response 仍直接 include 尚未实现的 Session/CGI 头文件：" >&2
        cat /tmp/webserv_forbidden_response_includes.txt >&2
        rm -f /tmp/webserv_forbidden_response_includes.txt
        exit 1
    fi
    rm -f /tmp/webserv_forbidden_response_includes.txt
}

# 函数：prepare_response_test_tree
# 用途：为 GET、POST、DELETE、autoindex、alias 和 CGI fallback 创建干净的临时目录。
# 参数：无。
# 返回值：创建失败时 shell 因 set -e 退出；成功时正常返回。
# 实现逻辑：先删除上一次测试目录，再一次性创建所有需要的子目录，避免旧文件污染结果。
prepare_response_test_tree()
{
    rm -rf tests/tmp_response_www
    mkdir -p \
        tests/tmp_response_www/upload \
        tests/tmp_response_www/readonly \
        tests/tmp_response_www/list \
        tests/tmp_response_www/noindex \
        tests/tmp_response_www/alias_target \
        tests/tmp_response_www/delete_dir \
        tests/tmp_response_www/cgi
}

# 函数：compile_response_module_test
# 用途：只编译 mandatory Response 独立测试需要的源文件；Config.cpp/ConfigParser.cpp 用于满足配置对象实现中的链接依赖。
# 参数：无；使用全局 CXX、CXXFLAGS 和 BUILD_DIR。
# 返回值：编译或链接失败时退出；成功时生成 response_module_test。
# 实现逻辑：不编译 ServerManager、ClientIO、SessionManager 或任何 CGI 源文件；因此测试同时验证 Response 没有隐藏的可选模块依赖。
compile_response_module_test()
{
    "$CXX" $CXXFLAGS \
        tests/module_tests/response_module_test.cpp \
        srcs/Request/Request.cpp \
        srcs/Config/Config.cpp \
        srcs/Config/ConfigParser.cpp \
        srcs/Config/ConfigRouteUtils.cpp \
        srcs/Config/ServerConfig.cpp \
        srcs/Config/LocationConfig.cpp \
        srcs/Response/Response.cpp \
        srcs/Response/RequestHandler.cpp \
        srcs/Response/ResponseUtils.cpp \
        srcs/Response/EffectiveRoute.cpp \
        -o "$BUILD_DIR/response_module_test"
}

# 本脚本可以从任意目录启动；先定位脚本，再回到含 srcs/、includes/ 的项目根目录。
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PROJECT_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)
cd "$PROJECT_ROOT"

CXX=${CXX:-c++}
CXXFLAGS="-Wall -Wextra -Werror -std=c++98 -Iincludes -Isrcs -Isrcs/Request -Isrcs/Response -Isrcs/Config -I."
BUILD_DIR="tests/module_tests/.build"
mkdir -p "$BUILD_DIR"

fail_if_forbidden_include_exists
prepare_response_test_tree
compile_response_module_test
"$BUILD_DIR/response_module_test"
