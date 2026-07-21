#!/bin/sh

# ==============================================================================
# 初学者阅读说明
# ==============================================================================
# 这是一份“只增加注释、不改变原命令”的学习版脚本。
# 每条可执行语句前都有“原第 N 行”说明；跨行命令会在整块命令前逐行解释，
# 避免把注释插入反斜杠续行中而破坏 Shell 语法。
#
# 常用符号速查：
#   #          注释开始；本行后面的内容不执行。
#   $VAR       展开变量 VAR 的值。
#   ${VAR}     用花括号明确变量名边界。
#   ${VAR:-x}  VAR 未设置或为空时使用默认值 x，但不会修改 VAR。
#   $0         当前脚本路径；$1、$2 是函数或脚本的位置参数。
#   $?         上一条命令的退出状态；0 表示成功，非 0 表示失败。
#   $(命令)    命令替换：执行命令并把它的标准输出放到当前位置。
#   "..."      双引号：允许 $变量 展开，同时避免空格、* 等被再次拆分或展开。
#   '...'      单引号：内容完全按字面处理，不展开变量。
#   \          行末反斜杠表示下一物理行仍属于同一条命令。
#   ;          在同一物理行结束一条命令，例如 `; then`。
#   &&         左侧成功后才执行右侧。
#   ||         左侧失败后才执行右侧。
#   !          对命令或测试结果取反。
#   |          管道：把左侧标准输出交给右侧标准输入。
#   >文件      覆盖写入标准输出。
#   >>文件     追加写入标准输出。
#   2>文件     重定向文件描述符 2，也就是标准错误。
#   2>&1       让标准错误指向标准输出当前去向。
#   >&2        把本命令标准输出送到标准错误。
#   /dev/null  丢弃写进去的内容。
#   [ 条件 ]   `test` 命令；`[` 与 `]` 两边必须有空格。
#   (...)      子 Shell 或 Bash 数组，具体含义取决于上下文。
#   {...}      当前 Shell 中的命令组；花括号两侧需要空格或换行。
#
# 常用退出码：
#   0          成功/条件为真。
#   非 0       失败/条件为假；本脚本常用 1 表示检查失败、2 表示编译失败。
# ==============================================================================

# 文件：tests/module_tests/run_response_test.sh
# 用途：编译真实 Config、RequestParser、Session 与拆分后的全部 Response .cpp，并运行不依赖 CGI 进程实现的 Response/Session 集成回归测试。
# 测试边界：不启动 pipe/fork/poll/execve；CGI 只验证内部脚本路径交接和 stdout 到 Response 的转换。
# 原第 5 行说明：`set` 修改 Shell 运行选项；`-e` 让未被条件结构接住的失败命令终止脚本，`-u` 让未定义变量报错。
set -eu

# 函数：fail
# 用途：统一打印 Response/Session 测试的前置检查或编译失败原因，并立即结束脚本。
# 参数来源：$1 由文件检查、接口检查或依赖检查分支传入。
# 变量说明：本函数没有局部变量，直接读取第一个位置参数。
# 实现逻辑：把带有 [FAIL] 的错误消息写到标准错误，再以状态码 1 退出，避免继续测试不完整源码。
# 原第 12 行说明：定义名为 `fail` 的 Shell 函数；空括号表示函数声明本身不写形参，参数通过 `$1`、`$2` 等位置参数读取。
fail()
# 原第 13 行说明：`{` 开始函数体或命令组；在 Shell 中它是语法关键字，不是 C++ 作用域。
{
# 原第 14 行说明：`echo` 输出后面的文本并自动换行；双引号会展开 `$变量`，同时保留文本中的空格；`>&2` 把标准输出重定向到标准错误，便于失败信息与普通输出区分。
    echo "[FAIL] $1" >&2
# 原第 15 行说明：`exit 1` 立即结束整个脚本，并把状态码 1 返回给调用者；0 通常表示成功，非 0 表示失败。
    exit 1
# 原第 16 行说明：`}` 结束函数体或命令组。
}

# 函数：find_project_root
# 用途：从当前测试脚本目录逐层向上寻找真实 Webserv 项目根目录。
# 参数来源：无参数；起点使用脚本自身所在目录 SCRIPT_DIR。
# 变量说明：candidate 保存当前检查的目录路径。
# 实现逻辑：逐层检查 includes/Response.hpp 与 srcs/Response/Response.cpp；找到后输出路径，走到根目录仍未找到则返回失败。
# 原第 23 行说明：定义名为 `find_project_root` 的 Shell 函数；空括号表示函数声明本身不写形参，参数通过 `$1`、`$2` 等位置参数读取。
find_project_root()
# 原第 24 行说明：`{` 开始函数体或命令组；在 Shell 中它是语法关键字，不是 C++ 作用域。
{
# 原第 25 行说明：给变量 `candidate` 赋值；Shell 赋值号两侧不能有空格。右侧若带双引号，可保持路径完整。
    candidate="$SCRIPT_DIR"
# 原第 26 行说明：开始 `while` 循环；方括号 `[...]` 实际是 `test` 命令。`!=` 比较字符串不相等，只要候选目录还不是 `/` 就继续向上查找。
    while [ "$candidate" != "/" ]
# 原第 27 行说明：`do` 开始 `for` 或 `while` 循环体。
    do
# 原第 28 行说明：开始 `if` 条件判断。`-f` 判断普通文件是否存在
# 原第 29 行说明：这是前面源码/头文件列表或编译命令中的一个路径参数；Shell 会把它作为独立文件传给检查命令或编译器。
        if [ -f "$candidate/includes/Response.hpp" ] \
            && [ -f "$candidate/srcs/Response/Response.cpp" ]
# 原第 30 行说明：`then` 开始执行条件为真的分支；它也可以写在条件行的 `; then` 后。
        then
# 原第 31 行说明：`printf` 按格式输出。`%s` 放入字符串参数，`\n` 表示换行；与 `echo` 相比，格式和转义行为更可预测。
            printf '%s\n' "$candidate"
# 原第 32 行说明：`return 0` 从当前函数返回退出状态 0；Shell 中 0 表示成功，非 0 表示失败。
            return 0
# 原第 33 行说明：`fi` 是 `if` 的反向拼写，用来结束当前条件分支。
        fi
# 原第 34 行说明：给变量 `candidate` 赋值；Shell 赋值号两侧不能有空格。右侧若带双引号，可保持路径完整。
        candidate=$(dirname "$candidate")
# 原第 35 行说明：`done` 结束 `for` 或 `while` 循环体。
    done
# 原第 36 行说明：`return 1` 从当前函数返回退出状态 1；Shell 中 0 表示成功，非 0 表示失败。
    return 1
# 原第 37 行说明：`}` 结束函数体或命令组。
}

# 函数：cleanup_build
# 用途：删除本脚本生成的 Response/Session 测试可执行文件和临时构建目录。
# 参数来源：无；使用全局 BUILD_DIR，前置失败时该变量可能尚未设置。
# 变量说明：无局部变量。
# 实现逻辑：BUILD_DIR 非空时 rm -rf，确保正常退出和信号退出都不留下编译产物。
# 原第 44 行说明：定义名为 `cleanup_build` 的 Shell 函数；空括号表示函数声明本身不写形参，参数通过 `$1`、`$2` 等位置参数读取。
cleanup_build()
# 原第 45 行说明：`{` 开始函数体或命令组；在 Shell 中它是语法关键字，不是 C++ 作用域。
{
# 原第 46 行说明：开始 `if` 条件判断。`!=` 表示字符串不相等
    if [ "${BUILD_DIR:-}" != "" ]; then
# 原第 47 行说明：`rm` 删除文件或目录；`-r` 递归删除目录内容，`-f` 强制删除且文件不存在时不报错。变量放在双引号中可防止路径中的空格被拆开。
        rm -rf "$BUILD_DIR"
# 原第 48 行说明：`fi` 是 `if` 的反向拼写，用来结束当前条件分支。
    fi
# 原第 49 行说明：`}` 结束函数体或命令组。
}

# 原第 51 行说明：计算并保存脚本自身所在目录的绝对路径。`$0` 是脚本路径，`dirname` 取父目录，`$(...)` 捕获命令输出；`CDPATH=` 临时清空 cd 搜索路径，`cd --` 中 `--` 结束选项解析，`&&` 仅在 cd 成功后执行 `pwd`。
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
# 原第 52 行说明：计算项目根目录。`$(...)` 是命令替换；`../..` 从测试脚本目录向上两级，`&&` 保证前一个 `cd` 成功后才执行 `pwd`；双引号保护路径空格。
# 原第 53 行说明：这是前面源码/头文件列表或编译命令中的一个路径参数；Shell 会把它作为独立文件传给检查命令或编译器。
PROJECT_ROOT=$(find_project_root) \
    || fail "找不到项目根目录：需要 includes/Response.hpp 和 srcs/Response/Response.cpp"
# 原第 54 行说明：`cd` 切换当前工作目录；路径使用双引号，避免目录名中的空格被拆词。
cd "$PROJECT_ROOT"
# 原第 55 行说明：`printf` 按格式输出。`%s` 放入字符串参数，`\n` 表示换行；与 `echo` 相比，格式和转义行为更可预测。
printf '%s\n' "[PASS] 已定位项目根目录：$PROJECT_ROOT"

# 原第 57 行说明：选择 C++ 编译器。`${CXX:-c++}` 表示：环境变量 `CXX` 已设置且非空时使用它，否则使用默认值 `c++`；`:-` 是带默认值的参数展开。
CXX=${CXX:-c++}
# 原第 58 行说明：保存编译选项字符串：`-Wall` 开启常见警告，`-Wextra` 开启额外警告，`-Werror` 把警告当错误，`-std=c++98` 强制 C++98；`-pedantic-errors`（若出现）把非标准扩展视为错误；`-I目录` 添加头文件搜索目录。
BASE_CXXFLAGS="-Wall -Wextra -Werror -std=c++98 ${EXTRA_CXXFLAGS:-}"
# 原第 59 行说明：保存测试构建目录路径；测试可执行文件放在这里，退出时统一删除。
BUILD_DIR="tests/module_tests/.build_response_only"
# 原第 60 行说明：`trap` 为退出或信号注册清理函数。`EXIT` 是正常/异常退出，`HUP`、`INT`、`TERM` 分别是挂起、中断和终止信号；收到时先执行清理。
trap cleanup_build EXIT HUP INT TERM
# 原第 61 行说明：`rm` 删除文件或目录；`-r` 递归删除目录内容，`-f` 强制删除且文件不存在时不报错。变量放在双引号中可防止路径中的空格被拆开。
rm -rf "$BUILD_DIR"
# 原第 62 行说明：`mkdir` 创建目录；`-p` 会同时创建缺失的父目录，并在目录已经存在时不报错。
mkdir -p "$BUILD_DIR"

# 拆分后的 Response 源文件使用显式列表：既保证每个实现被编译，也让缺失文件在链接前就能给出清楚错误。
# 原第 65 行说明：开始给变量 `RESPONSE_HEADERS` 赋一个跨多行的字符串列表。行末反斜杠 `\` 取消本行换行，使下一物理行继续属于同一个字符串。
# 原第 66 行说明：这是上一条跨行命令或字符串的续行；末尾 `\` 取消换行，下一物理行仍属于同一条语句。本行通常提供一个文件、目录或参数。
# 原第 67 行说明：这是上一条跨行命令或字符串的续行；末尾 `\` 取消换行，下一物理行仍属于同一条语句。本行通常提供一个文件、目录或参数。
# 原第 68 行说明：这是上一条跨行命令或字符串的续行；末尾 `\` 取消换行，下一物理行仍属于同一条语句。本行通常提供一个文件、目录或参数。
# 原第 69 行说明：这是上一条跨行命令或字符串的续行；末尾 `\` 取消换行，下一物理行仍属于同一条语句。本行通常提供一个文件、目录或参数。
# 原第 70 行说明：这是上一条跨行命令或字符串的续行；末尾 `\` 取消换行，下一物理行仍属于同一条语句。本行通常提供一个文件、目录或参数。
# 原第 71 行说明：这是上一条跨行命令或字符串的续行；末尾 `\` 取消换行，下一物理行仍属于同一条语句。本行通常提供一个文件、目录或参数。
# 原第 72 行说明：这是上一条跨行命令或字符串的续行；末尾 `\` 取消换行，下一物理行仍属于同一条语句。本行通常提供一个文件、目录或参数。
# 原第 73 行说明：这是上一条跨行命令或字符串的续行；末尾 `\` 取消换行，下一物理行仍属于同一条语句。本行通常提供一个文件、目录或参数。
# 原第 74 行说明：这是前面源码/头文件列表或编译命令中的一个路径参数；Shell 会把它作为独立文件传给检查命令或编译器。
RESPONSE_HEADERS="includes/Response.hpp \
includes/ResponseInternal.hpp \
includes/EffectiveRoute.hpp \
includes/RequestHandler.hpp \
includes/RequestHandlerInternal.hpp \
includes/SessionResponse.hpp \
includes/SessionResponseInternal.hpp \
includes/SessionStore.hpp \
includes/SessionCookie.hpp \
includes/SessionDemo.hpp"
# 原第 75 行说明：开始给变量 `RESPONSE_SOURCES` 赋一个跨多行的字符串列表。行末反斜杠 `\` 取消本行换行，使下一物理行继续属于同一个字符串。
# 原第 76 行说明：这是上一条跨行命令或字符串的续行；末尾 `\` 取消换行，下一物理行仍属于同一条语句。本行通常提供一个文件、目录或参数。
# 原第 77 行说明：这是上一条跨行命令或字符串的续行；末尾 `\` 取消换行，下一物理行仍属于同一条语句。本行通常提供一个文件、目录或参数。
# 原第 78 行说明：这是上一条跨行命令或字符串的续行；末尾 `\` 取消换行，下一物理行仍属于同一条语句。本行通常提供一个文件、目录或参数。
# 原第 79 行说明：这是上一条跨行命令或字符串的续行；末尾 `\` 取消换行，下一物理行仍属于同一条语句。本行通常提供一个文件、目录或参数。
# 原第 80 行说明：这是上一条跨行命令或字符串的续行；末尾 `\` 取消换行，下一物理行仍属于同一条语句。本行通常提供一个文件、目录或参数。
# 原第 81 行说明：这是上一条跨行命令或字符串的续行；末尾 `\` 取消换行，下一物理行仍属于同一条语句。本行通常提供一个文件、目录或参数。
# 原第 82 行说明：这是上一条跨行命令或字符串的续行；末尾 `\` 取消换行，下一物理行仍属于同一条语句。本行通常提供一个文件、目录或参数。
# 原第 83 行说明：这是上一条跨行命令或字符串的续行；末尾 `\` 取消换行，下一物理行仍属于同一条语句。本行通常提供一个文件、目录或参数。
# 原第 84 行说明：这是上一条跨行命令或字符串的续行；末尾 `\` 取消换行，下一物理行仍属于同一条语句。本行通常提供一个文件、目录或参数。
# 原第 85 行说明：这是上一条跨行命令或字符串的续行；末尾 `\` 取消换行，下一物理行仍属于同一条语句。本行通常提供一个文件、目录或参数。
# 原第 86 行说明：这是上一条跨行命令或字符串的续行；末尾 `\` 取消换行，下一物理行仍属于同一条语句。本行通常提供一个文件、目录或参数。
# 原第 87 行说明：这是上一条跨行命令或字符串的续行；末尾 `\` 取消换行，下一物理行仍属于同一条语句。本行通常提供一个文件、目录或参数。
# 原第 88 行说明：这是上一条跨行命令或字符串的续行；末尾 `\` 取消换行，下一物理行仍属于同一条语句。本行通常提供一个文件、目录或参数。
# 原第 89 行说明：这是前面源码/头文件列表或编译命令中的一个路径参数；Shell 会把它作为独立文件传给检查命令或编译器。
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
srcs/Response/RequestDelete.cpp \
srcs/Response/SessionResponse.cpp \
srcs/Response/SessionForm.cpp"
# 原第 90 行说明：开始给变量 `REQUEST_SOURCES` 赋一个跨多行的字符串列表。行末反斜杠 `\` 取消本行换行，使下一物理行继续属于同一个字符串。
# 原第 91 行说明：这是上一条跨行命令或字符串的续行；末尾 `\` 取消换行，下一物理行仍属于同一条语句。本行通常提供一个文件、目录或参数。
# 原第 92 行说明：这是上一条跨行命令或字符串的续行；末尾 `\` 取消换行，下一物理行仍属于同一条语句。本行通常提供一个文件、目录或参数。
# 原第 93 行说明：这是上一条跨行命令或字符串的续行；末尾 `\` 取消换行，下一物理行仍属于同一条语句。本行通常提供一个文件、目录或参数。
# 原第 94 行说明：这是上一条跨行命令或字符串的续行；末尾 `\` 取消换行，下一物理行仍属于同一条语句。本行通常提供一个文件、目录或参数。
# 原第 95 行说明：这是上一条跨行命令或字符串的续行；末尾 `\` 取消换行，下一物理行仍属于同一条语句。本行通常提供一个文件、目录或参数。
# 原第 96 行说明：这是前面源码/头文件列表或编译命令中的一个路径参数；Shell 会把它作为独立文件传给检查命令或编译器。
REQUEST_SOURCES="srcs/Request/Request.cpp \
srcs/Request/RequestParser.cpp \
srcs/Request/RequestParserRequestLine.cpp \
srcs/Request/RequestParserUri.cpp \
srcs/Request/RequestParserHeaders.cpp \
srcs/Request/RequestParserBody.cpp \
srcs/Request/RequestParserChunked.cpp"
# 原第 97 行说明：开始给变量 `CONFIG_SOURCES` 赋一个跨多行的字符串列表。行末反斜杠 `\` 取消本行换行，使下一物理行继续属于同一个字符串。
# 原第 98 行说明：这是上一条跨行命令或字符串的续行；末尾 `\` 取消换行，下一物理行仍属于同一条语句。本行通常提供一个文件、目录或参数。
# 原第 99 行说明：这是上一条跨行命令或字符串的续行；末尾 `\` 取消换行，下一物理行仍属于同一条语句。本行通常提供一个文件、目录或参数。
# 原第 100 行说明：这是上一条跨行命令或字符串的续行；末尾 `\` 取消换行，下一物理行仍属于同一条语句。本行通常提供一个文件、目录或参数。
# 原第 101 行说明：这是前面源码/头文件列表或编译命令中的一个路径参数；Shell 会把它作为独立文件传给检查命令或编译器。
CONFIG_SOURCES="srcs/Config/Config.cpp \
srcs/Config/ConfigParser.cpp \
srcs/Config/ConfigRouteUtils.cpp \
srcs/Config/LocationConfig.cpp \
srcs/Config/ServerConfig.cpp"
# 原第 102 行说明：开始给变量 `SESSION_SOURCES` 赋一个跨多行的字符串列表。行末反斜杠 `\` 取消本行换行，使下一物理行继续属于同一个字符串。
# 原第 103 行说明：这是上一条跨行命令或字符串的续行；末尾 `\` 取消换行，下一物理行仍属于同一条语句。本行通常提供一个文件、目录或参数。
# 原第 104 行说明：这是前面源码/头文件列表或编译命令中的一个路径参数；Shell 会把它作为独立文件传给检查命令或编译器。
SESSION_SOURCES="srcs/Session/SessionStore.cpp \
srcs/Session/SessionCookie.cpp \
srcs/Session/SessionDemo.cpp"
# 原第 105 行说明：开始给变量 `PROJECT_HEADERS` 赋一个跨多行的字符串列表。行末反斜杠 `\` 取消本行换行，使下一物理行继续属于同一个字符串。
# 原第 106 行说明：这是上一条跨行命令或字符串的续行；末尾 `\` 取消换行，下一物理行仍属于同一条语句。本行通常提供一个文件、目录或参数。
# 原第 107 行说明：这是上一条跨行命令或字符串的续行；末尾 `\` 取消换行，下一物理行仍属于同一条语句。本行通常提供一个文件、目录或参数。
# 原第 108 行说明：这是上一条跨行命令或字符串的续行；末尾 `\` 取消换行，下一物理行仍属于同一条语句。本行通常提供一个文件、目录或参数。
# 原第 109 行说明：这是前面源码/头文件列表或编译命令中的一个路径参数；Shell 会把它作为独立文件传给检查命令或编译器。
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
# 原第 116 行说明：定义名为 `check_required_files` 的 Shell 函数；空括号表示函数声明本身不写形参，参数通过 `$1`、`$2` 等位置参数读取。
check_required_files()
# 原第 117 行说明：`{` 开始函数体或命令组；在 Shell 中它是语法关键字，不是 C++ 作用域。
{
# 原第 118 行说明：开始 `for` 循环；`in` 后的单词依次赋给循环变量。变量列表未加引号时会按空白拆词，这里各项本身都是无空格路径。
# 原第 119 行说明：本行继续完成当前 Shell 操作；`$变量`/`${...}` 展开变量或参数。
    for file in $RESPONSE_HEADERS $RESPONSE_SOURCES \
        $REQUEST_SOURCES $CONFIG_SOURCES $SESSION_SOURCES $PROJECT_HEADERS
# 原第 120 行说明：`do` 开始 `for` 或 `while` 循环体。
    do
# 原第 121 行说明：方括号是 `test` 命令的另一种写法；结尾 `]` 是必需参数。；`-f` 检查普通文件，`||` 左边失败时执行右边
        [ -f "$file" ] || fail "缺少正式项目文件：$file"
# 原第 122 行说明：`done` 结束 `for` 或 `while` 循环体。
    done
# 原第 123 行说明：`}` 结束函数体或命令组。
}

# 原第 125 行说明：调用前面定义的 `check_required_files` 函数，正式执行依赖文件检查。
check_required_files

# 原第 127 行说明：开始 `if` 条件判断。`-f` 判断普通文件是否存在；`&&` 表示左侧成功才检查右侧
if [ ! -f includes/Config.hpp ] && [ ! -f srcs/Config/Config.hpp ]; then
# 原第 128 行说明：本行继续完成当前 Shell 操作；双引号允许变量展开并阻止空白拆词。
    fail "缺少真实 Config.hpp（应位于 includes/ 或 srcs/Config/）"
# 原第 129 行说明：`fi` 是 `if` 的反向拼写，用来结束当前条件分支。
fi

# 检查 Response 只保留唯一的共享 SessionStore 入口，同时允许实现正常分布在多个 .cpp 中。
# 原第 132 行说明：`grep` 搜索文本。`-q` 静默模式，只看退出码
# 原第 133 行说明：`||` 表示只有上一条命令失败（退出码非 0）时，才执行右侧的失败处理。
grep -q 'class Response' includes/Response.hpp \
    || fail "Response.hpp 未定义 class Response"
# 原第 134 行说明：`grep` 搜索文本。`-q` 静默模式，只看退出码
# 原第 135 行说明：`||` 表示只有上一条命令失败（退出码非 0）时，才执行右侧的失败处理。
grep -q 'Response buildResponse(const Request &request,' includes/Response.hpp \
    || fail "Response.hpp 缺少唯一的 buildResponse(request, sessionStore) 入口"
# 原第 136 行说明：`grep` 搜索文本。`-q` 静默模式，只看退出码
# 原第 137 行说明：`||` 表示只有上一条命令失败（退出码非 0）时，才执行右侧的失败处理。
grep -q 'SessionStore &sessionStore' includes/Response.hpp \
    || fail "buildResponse 缺少共享 SessionStore 参数"
# 原第 138 行说明：开始 `if` 条件判断。Shell 根据命令退出码判断真假：0 为真，非 0 为假。
if grep -q 'Response buildResponse(const Request &request);' includes/Response.hpp; then
# 原第 139 行说明：本行继续完成当前 Shell 操作；双引号允许变量展开并阻止空白拆词。
    fail "Response.hpp 仍保留会绕过 Session 的旧 buildResponse(request) 接口"
# 原第 140 行说明：`fi` 是 `if` 的反向拼写，用来结束当前条件分支。
fi
# 原第 141 行说明：开始 `if` 条件判断。Shell 根据命令退出码判断真假：0 为真，非 0 为假。
# 原第 142 行说明：这是前面源码/头文件列表或编译命令中的一个路径参数；Shell 会把它作为独立文件传给检查命令或编译器。
if grep -q 'Response buildResponse(const Request &request)$' \
        srcs/Response/ResponseBuilder.cpp; then
# 原第 143 行说明：本行继续完成当前 Shell 操作；双引号允许变量展开并阻止空白拆词。
    fail "ResponseBuilder.cpp 仍定义会绕过 Session 的旧 buildResponse(request)"
# 原第 144 行说明：`fi` 是 `if` 的反向拼写，用来结束当前条件分支。
fi
# 原第 145 行说明：`grep` 搜索文本。`-q` 静默模式，只看退出码
# 原第 146 行说明：`||` 表示只有上一条命令失败（退出码非 0）时，才执行右侧的失败处理。
grep -q 'buildSessionDemoResponse' includes/SessionResponse.hpp \
    || fail "SessionResponse.hpp 缺少 Session 响应接入接口"
# 原第 147 行说明：`grep` 搜索文本。`-q` 静默模式，只看退出码
# 原第 148 行说明：`||` 表示只有上一条命令失败（退出码非 0）时，才执行右侧的失败处理。
grep -q 'void parseCgiOutput' includes/Response.hpp \
    || fail "Response.hpp 缺少 ServerManager 已使用的 parseCgiOutput"
# 原第 149 行说明：`grep` 搜索文本。`-q` 静默模式，只看退出码
# 原第 150 行说明：这是前面源码/头文件列表或编译命令中的一个路径参数；Shell 会把它作为独立文件传给检查命令或编译器。
grep -q 'enum RequestAction' includes/EffectiveRoute.hpp \
    || fail "RequestAction 未合并到 EffectiveRoute.hpp"

# 检查拆分后没有回退到旧类型、旧宏、直接访问 Request 字段或文本包含式 .inc 方案。
# 原第 153 行说明：方括号是 `test` 命令的另一种写法；结尾 `]` 是必需参数。；`-e` 检查路径存在，`!` 对判断结果取反
# 原第 154 行说明：这是前面源码/头文件列表或编译命令中的一个路径参数；Shell 会把它作为独立文件传给检查命令或编译器。
[ ! -e includes/RequestAction.hpp ] \
    || fail "仍残留独立 RequestAction.hpp"
# 原第 155 行说明：方括号是 `test` 命令的另一种写法；结尾 `]` 是必需参数。；`-e` 检查路径存在，`!` 对判断结果取反
# 原第 156 行说明：这是前面源码/头文件列表或编译命令中的一个路径参数；Shell 会把它作为独立文件传给检查命令或编译器。
[ ! -e includes/ResponseUtils.hpp ] \
    || fail "仍残留 ResponseUtils.hpp"
# 原第 157 行说明：方括号是 `test` 命令的另一种写法；结尾 `]` 是必需参数。；`-e` 检查路径存在，`!` 对判断结果取反
# 原第 158 行说明：这是前面源码/头文件列表或编译命令中的一个路径参数；Shell 会把它作为独立文件传给检查命令或编译器。
[ ! -e srcs/Response/ResponseUtils.cpp ] \
    || fail "仍残留 ResponseUtils.cpp"
# 原第 159 行说明：开始 `if` 条件判断。Shell 根据命令退出码判断真假：0 为真，非 0 为假。
if find srcs/Response -type f -name '*.inc' | grep -q .; then
# 原第 160 行说明：这是前面源码/头文件列表或编译命令中的一个路径参数；Shell 会把它作为独立文件传给检查命令或编译器。
    fail "Response 仍使用 .inc 文本包含方案；应拆成独立 .cpp"
# 原第 161 行说明：`fi` 是 `if` 的反向拼写，用来结束当前条件分支。
fi
# 原第 162 行说明：开始 `if` 条件判断。Shell 根据命令退出码判断真假：0 为真，非 0 为假。
if grep -R -n 'struct Response' includes srcs/Response >/dev/null 2>&1; then
# 原第 163 行说明：本行继续完成当前 Shell 操作；双引号允许变量展开并阻止空白拆词。
    fail "仍残留旧 struct Response"
# 原第 164 行说明：`fi` 是 `if` 的反向拼写，用来结束当前条件分支。
fi
# 原第 165 行说明：开始 `if` 条件判断。Shell 根据命令退出码判断真假：0 为真，非 0 为假。
# 原第 166 行说明：这是前面源码/头文件列表或编译命令中的一个路径参数；Shell 会把它作为独立文件传给检查命令或编译器。
if grep -R -n -E 'request\.(method|uri|version|headers|body|config|closeConnection)' \
    srcs/Response >/dev/null 2>&1; then
# 原第 167 行说明：本行继续完成当前 Shell 操作；双引号允许变量展开并阻止空白拆词。
    fail "Response 仍直接访问旧 Request 公开字段"
# 原第 168 行说明：`fi` 是 `if` 的反向拼写，用来结束当前条件分支。
fi
# 原第 169 行说明：开始 `if` 条件判断。Shell 根据命令退出码判断真假：0 为真，非 0 为假。
# 原第 170 行说明：执行检查命令并丢弃输出：`>` 重定向标准输出到 `/dev/null`，`2>&1` 再让文件描述符 2（标准错误）指向文件描述符 1 的当前位置；只保留退出码。
if grep -R -n -E '#define[[:space:]]+(GET|POST|DELETE)' \
    includes srcs/Response >/dev/null 2>&1; then
# 原第 171 行说明：本行继续完成当前 Shell 操作；双引号允许变量展开并阻止空白拆词。
    fail "仍残留 GET/POST/DELETE 方法宏"
# 原第 172 行说明：`fi` 是 `if` 的反向拼写，用来结束当前条件分支。
fi
# 原第 173 行说明：开始 `if` 条件判断。Shell 根据命令退出码判断真假：0 为真，非 0 为假。
# 原第 174 行说明：这是前面源码/头文件列表或编译命令中的一个路径参数；Shell 会把它作为独立文件传给检查命令或编译器。
if grep -R -n -E 'ResponseBuildResult|ResponseBuildKind' \
    includes/Response.hpp srcs/Response/*.cpp >/dev/null 2>&1; then
# 原第 175 行说明：本行继续完成当前 Shell 操作；双引号允许变量展开并阻止空白拆词。
    fail "Response 仍要求其他模块迁移到 ResponseBuildResult"
# 原第 176 行说明：`fi` 是 `if` 的反向拼写，用来结束当前条件分支。
fi
# 原第 177 行说明：开始条件判断；`!` 取反后面命令的退出状态，因此只有命令失败时条件才为真；`; then` 中分号结束条件命令，随后进入真分支。
if ! grep -R -q 'X-Internal-CGI-Path' srcs/Response/*.cpp; then
# 原第 178 行说明：本行继续完成当前 Shell 操作；双引号允许变量展开并阻止空白拆词。
    fail "CGI 分支未保留现有 ServerManager 使用的内部路径接口"
# 原第 179 行说明：`fi` 是 `if` 的反向拼写，用来结束当前条件分支。
fi
# 原第 180 行说明：开始 `if` 条件判断。Shell 根据命令退出码判断真假：0 为真，非 0 为假。
# 原第 181 行说明：这是上一条跨行命令或字符串的续行；末尾 `\` 取消换行，下一物理行仍属于同一条语句。本行通常提供一个文件、目录或参数。
# 原第 182 行说明：这是上一条跨行命令或字符串的续行；末尾 `\` 取消换行，下一物理行仍属于同一条语句。本行通常提供一个文件、目录或参数。
# 原第 183 行说明：执行检查命令并丢弃输出：`>` 重定向标准输出到 `/dev/null`，`2>&1` 再让文件描述符 2（标准错误）指向文件描述符 1 的当前位置；只保留退出码。
if grep -R -n -E '(^|[^[:alnum:]_])namespace([^[:alnum:]_]|$)' \
    includes/Response*.hpp includes/Session*.hpp includes/EffectiveRoute.hpp \
    includes/RequestHandler*.hpp srcs/Response srcs/Session \
    >/dev/null 2>&1; then
# 原第 184 行说明：本行继续完成当前 Shell 操作；双引号允许变量展开并阻止空白拆词。
    fail "Response/Session 代码仍使用 namespace"
# 原第 185 行说明：`fi` 是 `if` 的反向拼写，用来结束当前条件分支。
fi
# 原第 186 行说明：`grep` 搜索文本。`-q` 静默模式，只看退出码
# 原第 187 行说明：`||` 表示只有上一条命令失败（退出码非 0）时，才执行右侧的失败处理。
grep -q 'parseCgiOutput' tests/module_tests/response_module_test.cpp \
    || fail "测试未覆盖 ServerManager 已使用的 parseCgiOutput"
# 原第 188 行说明：`grep` 搜索文本。`-q` 静默模式，只看退出码
# 原第 189 行说明：这是上一条跨行命令或字符串的续行；末尾 `\` 取消换行，下一物理行仍属于同一条语句。本行通常提供一个文件、目录或参数。
# 原第 190 行说明：`||` 表示只有上一条命令失败（退出码非 0）时，才执行右侧的失败处理。
grep -q 'testSessionResponseIntegration' \
    tests/module_tests/response_module_test.cpp \
    || fail "测试未覆盖 Response 与 Session 集成"
# 原第 191 行说明：`grep` 搜索文本。`-q` 静默模式，只看退出码
# 原第 192 行说明：这是上一条跨行命令或字符串的续行；末尾 `\` 取消换行，下一物理行仍属于同一条语句。本行通常提供一个文件、目录或参数。
# 原第 193 行说明：`||` 表示只有上一条命令失败（退出码非 0）时，才执行右侧的失败处理。
grep -q 'buildResponse(request, store)' \
    tests/module_tests/response_module_test.cpp \
    || fail "测试未调用唯一的共享 SessionStore 入口"
# 原第 194 行说明：`grep` 搜索文本。`-q` 静默模式，只看退出码
# 原第 195 行说明：`||` 表示只有上一条命令失败（退出码非 0）时，才执行右侧的失败处理。
grep -q '#include "Config.hpp"' tests/module_tests/response_module_test.cpp \
    || fail "测试未使用真实 Config"
# 原第 196 行说明：开始 `if` 条件判断。Shell 根据命令退出码判断真假：0 为真，非 0 为假。
# 原第 197 行说明：这是上一条跨行命令或字符串的续行；末尾 `\` 取消换行，下一物理行仍属于同一条语句。本行通常提供一个文件、目录或参数。
# 原第 198 行说明：这是上一条跨行命令或字符串的续行；末尾 `\` 取消换行，下一物理行仍属于同一条语句。本行通常提供一个文件、目录或参数。
# 原第 199 行说明：这是上一条跨行命令或字符串的续行；末尾 `\` 取消换行，下一物理行仍属于同一条语句。本行通常提供一个文件、目录或参数。
# 原第 200 行说明：这是上一条跨行命令或字符串的续行；末尾 `\` 取消换行，下一物理行仍属于同一条语句。本行通常提供一个文件、目录或参数。
# 原第 201 行说明：这是前面源码/头文件列表或编译命令中的一个路径参数；Shell 会把它作为独立文件传给检查命令或编译器。
if grep -q '#include "CgiHandler.hpp"' \
        tests/module_tests/response_module_test.cpp \
    || grep -q -E '(^|[^[:alnum:]_])CgiFds[[:space:]]+[A-Za-z_][A-Za-z0-9_]*' \
        tests/module_tests/response_module_test.cpp \
    || grep -q -E 'async_launch[[:space:]]*\(' \
        tests/module_tests/response_module_test.cpp; then
# 原第 202 行说明：本行继续完成当前 Shell 操作；双引号允许变量展开并阻止空白拆词。
    fail "Response/Session 测试不应包含 CGI 运行时类型或启动调用"
# 原第 203 行说明：`fi` 是 `if` 的反向拼写，用来结束当前条件分支。
fi

# 原第 205 行说明：`printf` 按格式输出。`%s` 放入字符串参数，`\n` 表示换行；与 `echo` 相比，格式和转义行为更可预测。
printf '%s\n' "[PASS] buildResponse 继续返回 Response"
# 原第 206 行说明：`printf` 按格式输出。`%s` 放入字符串参数，`\n` 表示换行；与 `echo` 相比，格式和转义行为更可预测。
printf '%s\n' "[PASS] buildResponse 只保留共享 SessionStore 唯一入口"
# 原第 207 行说明：`printf` 按格式输出。`%s` 放入字符串参数，`\n` 表示换行；与 `echo` 相比，格式和转义行为更可预测。
printf '%s\n' "[PASS] counter/login/logout 已接入 Response 虚拟路由"
# 原第 208 行说明：`printf` 按格式输出。`%s` 放入字符串参数，`\n` 表示换行；与 `echo` 相比，格式和转义行为更可预测。
printf '%s\n' "[PASS] parseCgiOutput 兼容 ServerManager 现有调用"
# 原第 209 行说明：`printf` 按格式输出。`%s` 放入字符串参数，`\n` 表示换行；与 `echo` 相比，格式和转义行为更可预测。
printf '%s\n' "[PASS] CGI 内部路径接口可位于拆分后的 ResponseBuilder.cpp"
# 原第 210 行说明：`printf` 按格式输出。`%s` 放入字符串参数，`\n` 表示换行；与 `echo` 相比，格式和转义行为更可预测。
printf '%s\n' "[PASS] 测试编译全部拆分 .cpp，且不存在 .inc"
# 原第 211 行说明：`printf` 按格式输出。`%s` 放入字符串参数，`\n` 表示换行；与 `echo` 相比，格式和转义行为更可预测。
printf '%s\n' "[PASS] 测试不依赖 CgiHandler、CgiFds、pipe、fork、poll 或 waitpid"
# 原第 212 行说明：`printf` 按格式输出。`%s` 放入字符串参数，`\n` 表示换行；与 `echo` 相比，格式和转义行为更可预测。
printf '%s\n' "[PASS] 使用项目真实 Config、RequestParser 和 Response"
# 原第 213 行说明：`printf` 按格式输出。`%s` 放入字符串参数，`\n` 表示换行；与 `echo` 相比，格式和转义行为更可预测。
printf '%s\n' "[PASS] Config/test.cpp 不会被编入测试程序"

# 原第 215 行说明：`rm` 删除文件或目录；`-r` 递归删除目录内容，`-f` 强制删除且文件不存在时不报错。变量放在双引号中可防止路径中的空格被拆开。
rm -rf tests/tmp_response_www
# 原第 216 行说明：`rm` 删除文件或目录；`-f` 强制删除且文件不存在时不报错。变量放在双引号中可防止路径中的空格被拆开。
rm -f tests/module_tests/response_module_test.conf
# 原第 217 行说明：`mkdir` 创建目录；`-p` 会同时创建缺失的父目录，并在目录已经存在时不报错。
# 原第 218 行说明：这是上一条跨行命令或字符串的续行；末尾 `\` 取消换行，下一物理行仍属于同一条语句。本行通常提供一个文件、目录或参数。
# 原第 219 行说明：这是上一条跨行命令或字符串的续行；末尾 `\` 取消换行，下一物理行仍属于同一条语句。本行通常提供一个文件、目录或参数。
# 原第 220 行说明：这是上一条跨行命令或字符串的续行；末尾 `\` 取消换行，下一物理行仍属于同一条语句。本行通常提供一个文件、目录或参数。
# 原第 221 行说明：这是上一条跨行命令或字符串的续行；末尾 `\` 取消换行，下一物理行仍属于同一条语句。本行通常提供一个文件、目录或参数。
# 原第 222 行说明：这是上一条跨行命令或字符串的续行；末尾 `\` 取消换行，下一物理行仍属于同一条语句。本行通常提供一个文件、目录或参数。
# 原第 223 行说明：这是上一条跨行命令或字符串的续行；末尾 `\` 取消换行，下一物理行仍属于同一条语句。本行通常提供一个文件、目录或参数。
# 原第 224 行说明：这是上一条跨行命令或字符串的续行；末尾 `\` 取消换行，下一物理行仍属于同一条语句。本行通常提供一个文件、目录或参数。
# 原第 225 行说明：这是前面源码/头文件列表或编译命令中的一个路径参数；Shell 会把它作为独立文件传给检查命令或编译器。
mkdir -p \
    tests/tmp_response_www/upload \
    tests/tmp_response_www/readonly \
    tests/tmp_response_www/list \
    tests/tmp_response_www/alias_target \
    tests/tmp_response_www/delete_dir \
    tests/tmp_response_www/cgi \
    tests/tmp_response_www/text-index \
    tests/tmp_response_www/joined_upload

# 原第 227 行说明：给变量 `INCLUDE_FLAGS` 赋值；后续通过 `$INCLUDE_FLAGS` 或 `$$INCLUDE_FLAGS`（实际写法为 `$INCLUDE_FLAGS`/`$name`）展开。
INCLUDE_FLAGS="-Iincludes -Isrcs/Config -Isrcs/Request -Isrcs/Response -Isrcs/Session"

# 原第 229 行说明：调用 C++ 编译器。未加引号的 flags/source 列表需要按空格拆成多个参数；`-I` 添加头文件搜索路径，`-o` 指定输出可执行文件。编译器路径本身加引号可保护空格。
# 原第 230 行说明：这是上一条跨行命令或字符串的续行；末尾 `\` 取消换行，下一物理行仍属于同一条语句。本行通常提供一个文件、目录或参数。
# 原第 231 行说明：这是上一条跨行命令或字符串的续行；末尾 `\` 取消换行，下一物理行仍属于同一条语句。本行通常提供一个文件、目录或参数。
# 原第 232 行说明：这是上一条跨行命令或字符串的续行；末尾 `\` 取消换行，下一物理行仍属于同一条语句。本行通常提供一个文件、目录或参数。
# 原第 233 行说明：这是上一条跨行命令或字符串的续行；末尾 `\` 取消换行，下一物理行仍属于同一条语句。本行通常提供一个文件、目录或参数。
# 原第 234 行说明：这是上一条跨行命令或字符串的续行；末尾 `\` 取消换行，下一物理行仍属于同一条语句。本行通常提供一个文件、目录或参数。
# 原第 235 行说明：本行继续完成当前 Shell 操作；`$变量`/`${...}` 展开变量或参数，双引号允许变量展开并阻止空白拆词。
"$CXX" $BASE_CXXFLAGS $INCLUDE_FLAGS \
    tests/module_tests/response_module_test.cpp \
    $REQUEST_SOURCES \
    $CONFIG_SOURCES \
    $SESSION_SOURCES \
    $RESPONSE_SOURCES \
    -o "$BUILD_DIR/response_module_test"

# 原第 237 行说明：`printf` 按格式输出。`%s` 放入字符串参数，`\n` 表示换行；与 `echo` 相比，格式和转义行为更可预测。
printf '%s\n' "[PASS] Response/Session 测试编译成功"
# 原第 238 行说明：运行刚编译出的测试可执行文件；双引号保护构建目录路径中的空格。
"$BUILD_DIR/response_module_test"
