#!/bin/sh
# 文件：tests/module_tests/run_session_test.sh
# 用途：定位 Webserv 根目录，检查 Session Bonus 文件、注释和代码限制，使用 C++98 严格编译并运行独立测试。
# 测试边界：只编译 srcs/Session，不依赖 Request、Response、ServerManager、Config、socket 或 CGI。
# 清理行为：测试结束后自动删除 .build_session，避免把编译产物留在仓库或交付包中。
set -eu

# 函数：fail
# 用途：统一输出前置检查或编译准备失败原因，并立即退出。
# 参数：$1 是调用分支传入的错误说明。
# 变量：无局部变量。
# 实现逻辑：向标准错误打印 [FAIL]，再以状态码 1 结束脚本。
fail()
{
    echo "[FAIL] $1" >&2
    exit 1
}

# 函数：looks_like_project_root
# 用途：判断候选目录是否是正式 Webserv 根目录或可独立测试的 Session 模块根目录。
# 参数：$1 是 find_project_root() 当前检查的目录。
# 变量：candidate 保存位置参数，便于多次使用。
# 实现逻辑：Makefile 与 srcs 同时存在时接受；独立包具有 srcs/Session 和 include(s) 时也接受。
looks_like_project_root()
{
    candidate=$1
    if [ -f "$candidate/Makefile" ] && [ -d "$candidate/srcs" ]; then
        return 0
    fi
    if [ -d "$candidate/srcs/Session" ] \
        && { [ -d "$candidate/includes" ] || [ -d "$candidate/include" ]; }
    then
        return 0
    fi
    return 1
}

# 函数：find_project_root
# 用途：从测试脚本目录逐层向上查找项目根目录。
# 参数：无；起点来自全局 SCRIPT_DIR。
# 变量：candidate 保存当前候选目录。
# 实现逻辑：每层调用 looks_like_project_root()；找到时输出绝对路径，到 / 仍未找到则失败。
find_project_root()
{
    candidate="$SCRIPT_DIR"
    while [ "$candidate" != "/" ]
    do
        if looks_like_project_root "$candidate"; then
            printf '%s\n' "$candidate"
            return 0
        fi
        candidate=$(dirname "$candidate")
    done
    return 1
}

# 函数：find_header_directory
# 用途：确定项目公共头文件目录使用 includes/ 还是 include/。
# 参数：无；在 PROJECT_ROOT 已确定并 cd 后调用。
# 变量：无局部变量。
# 实现逻辑：优先选择已经包含 SessionStore.hpp 的目录，否则选择现有目录；都不存在时失败。
find_header_directory()
{
    if [ -f "includes/SessionStore.hpp" ]; then
        printf '%s\n' "includes"
        return 0
    fi
    if [ -f "include/SessionStore.hpp" ]; then
        printf '%s\n' "include"
        return 0
    fi
    if [ -d "includes" ]; then
        printf '%s\n' "includes"
        return 0
    fi
    if [ -d "include" ]; then
        printf '%s\n' "include"
        return 0
    fi
    return 1
}

# 函数：cleanup_build
# 用途：删除本脚本生成的测试可执行文件和临时构建目录。
# 参数：无；使用全局 BUILD_DIR，变量可能在脚本前置失败时尚未设置。
# 变量：无局部变量。
# 实现逻辑：BUILD_DIR 非空时执行 rm -rf，确保正常退出和信号退出都不留下产物。
cleanup_build()
{
    if [ "${BUILD_DIR:-}" != "" ]; then
        rm -rf "$BUILD_DIR"
    fi
}

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PROJECT_ROOT=$(find_project_root) \
    || fail "找不到 Webserv 项目根目录：脚本应位于 <项目>/tests/module_tests/，根目录需包含 Makefile 和 srcs/"
cd "$PROJECT_ROOT"
printf '%s\n' "[PASS] 已定位项目根目录：$PROJECT_ROOT"

HEADER_DIR=$(find_header_directory) \
    || fail "项目根目录中既没有 includes/，也没有 include/"
printf '%s\n' "[PASS] 使用头文件目录：$HEADER_DIR"

CXX=${CXX:-c++}
BASE_CXXFLAGS="-Wall -Wextra -Werror -std=c++98 -pedantic-errors ${EXTRA_CXXFLAGS:-}"
BUILD_DIR="tests/module_tests/.build_session"
trap cleanup_build EXIT HUP INT TERM
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"

# 文件检查：逐个报告缺失文件，避免把复制不完整误报成根目录错误。
for file in \
    "$HEADER_DIR/SessionStore.hpp" \
    "$HEADER_DIR/SessionCookie.hpp" \
    "$HEADER_DIR/SessionDemo.hpp" \
    srcs/Session/SessionStore.cpp \
    srcs/Session/SessionCookie.cpp \
    srcs/Session/SessionDemo.cpp \
    tests/module_tests/session_module_test.cpp
 do
    [ -f "$file" ] || fail "缺少 Session 模块文件：$file"
 done

# 接口检查：确认模块覆盖 Cookie、Session 管理、ID 轮换、退出删除和简单示例。
grep -q 'class SessionStore' "$HEADER_DIR/SessionStore.hpp" \
    || fail "SessionStore.hpp 未定义 SessionStore"
grep -q 'class SessionCookie' "$HEADER_DIR/SessionCookie.hpp" \
    || fail "SessionCookie.hpp 未定义 SessionCookie"
grep -q 'class SessionDemo' "$HEADER_DIR/SessionDemo.hpp" \
    || fail "SessionDemo.hpp 未定义 SessionDemo"
grep -q 'regenerateSession' "$HEADER_DIR/SessionStore.hpp" \
    || fail "SessionStore 缺少 ID 轮换接口"
grep -q 'buildExpiredCookie' "$HEADER_DIR/SessionCookie.hpp" \
    || fail "Cookie 模块缺少删除 Cookie 接口"
grep -q 'buildCounterExample' "$HEADER_DIR/SessionDemo.hpp" \
    || fail "SessionDemo 缺少访问计数示例"
grep -q 'buildLoginExample' "$HEADER_DIR/SessionDemo.hpp" \
    || fail "SessionDemo 缺少登录示例"
grep -q 'buildLogoutExample' "$HEADER_DIR/SessionDemo.hpp" \
    || fail "SessionDemo 缺少退出示例"

# 结构检查：正式 C++ 文件和测试 C++ 文件不能定义命名空间，也不能残留 .inc 拆分文件。
if grep -R -n -E '(^|[^[:alnum:]_])namespace([^[:alnum:]_]|$)' \
    "$HEADER_DIR/SessionStore.hpp" \
    "$HEADER_DIR/SessionCookie.hpp" \
    "$HEADER_DIR/SessionDemo.hpp" \
    srcs/Session \
    tests/module_tests/session_module_test.cpp >/dev/null 2>&1; then
    fail "Session 模块仍使用 namespace"
fi
if find srcs/Session -type f -name '*.inc' | grep . >/dev/null 2>&1; then
    fail "Session 模块不应包含 .inc 实现片段"
fi

# 注释检查：确认每个类、结构体和测试入口都有顶部中文说明，并检查包含区写明库用途。
for marker in \
    '类：SessionStore' \
    '类：SessionCookie' \
    '类：SessionDemo' \
    '结构体：SessionDemoResult'
 do
    grep -R -q "$marker" "$HEADER_DIR" \
        || fail "缺少类或结构体顶部注释：$marker"
 done
for file in \
    "$HEADER_DIR/SessionStore.hpp" \
    "$HEADER_DIR/SessionCookie.hpp" \
    "$HEADER_DIR/SessionDemo.hpp" \
    srcs/Session/SessionStore.cpp \
    srcs/Session/SessionCookie.cpp \
    srcs/Session/SessionDemo.cpp \
    tests/module_tests/session_module_test.cpp
 do
    grep -q '包含：' "$file" \
        || fail "包含库区域缺少用途注释：$file"
 done
grep -q '函数：main' tests/module_tests/session_module_test.cpp \
    || fail "Session 测试 main 缺少函数顶部注释"

# 交付检查：源码目录中不能残留常见编译产物；测试脚本自己不属于该检查范围。
if find srcs/Session "$HEADER_DIR" tests/module_tests \
    -type f \( -name '*.o' -o -name '*.a' -o -name '*.so' \
    -o -name '*.dylib' -o -name 'session_module_test' \) \
    | grep . >/dev/null 2>&1; then
    fail "Session 目录中残留编译产物"
fi

printf '%s\n' "[PASS] Cookie 解析与 Set-Cookie 接口存在"
printf '%s\n' "[PASS] Session 生命周期与 ID 轮换接口存在"
printf '%s\n' "[PASS] counter/login/logout 简单示例存在"
printf '%s\n' "[PASS] C++ 文件未使用 namespace 或 .inc"
printf '%s\n' "[PASS] 类、函数和包含库注释检查通过"
printf '%s\n' "[PASS] 交付目录没有编译产物"

SESSION_SOURCES="srcs/Session/SessionStore.cpp \
srcs/Session/SessionCookie.cpp \
srcs/Session/SessionDemo.cpp"

"$CXX" $BASE_CXXFLAGS -I"$HEADER_DIR" \
    tests/module_tests/session_module_test.cpp \
    $SESSION_SOURCES \
    -o "$BUILD_DIR/session_module_test"

printf '%s\n' "[PASS] Session Bonus 模块编译成功"
"$BUILD_DIR/session_module_test"
