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

# 原第 2 行说明：`set` 修改 Shell 运行选项；`-e` 让未被条件结构接住的失败命令终止脚本，`-u` 让未定义变量报错。
set -eu

# 本脚本可以从任意目录启动；先回到包含 includes/、srcs/ 和 tests/ 的项目根目录。
# 原第 5 行说明：计算并保存脚本自身所在目录的绝对路径。`$0` 是脚本路径，`dirname` 取父目录，`$(...)` 捕获命令输出；`CDPATH=` 临时清空 cd 搜索路径，`cd --` 中 `--` 结束选项解析，`&&` 仅在 cd 成功后执行 `pwd`。
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
# 原第 6 行说明：计算项目根目录。`$(...)` 是命令替换；`../..` 从测试脚本目录向上两级，`&&` 保证前一个 `cd` 成功后才执行 `pwd`；双引号保护路径空格。
PROJECT_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)
# 原第 7 行说明：`cd` 切换当前工作目录；路径使用双引号，避免目录名中的空格被拆词。
cd "$PROJECT_ROOT"

# 原第 9 行说明：开始给变量 `REQUEST_SOURCES` 赋一个跨多行的字符串列表。行末反斜杠 `\` 取消本行换行，使下一物理行继续属于同一个字符串。
# 原第 10 行说明：这是上一条跨行命令或字符串的续行；末尾 `\` 取消换行，下一物理行仍属于同一条语句。本行通常提供一个文件、目录或参数。
# 原第 11 行说明：这是上一条跨行命令或字符串的续行；末尾 `\` 取消换行，下一物理行仍属于同一条语句。本行通常提供一个文件、目录或参数。
# 原第 12 行说明：这是上一条跨行命令或字符串的续行；末尾 `\` 取消换行，下一物理行仍属于同一条语句。本行通常提供一个文件、目录或参数。
# 原第 13 行说明：这是上一条跨行命令或字符串的续行；末尾 `\` 取消换行，下一物理行仍属于同一条语句。本行通常提供一个文件、目录或参数。
# 原第 14 行说明：这是上一条跨行命令或字符串的续行；末尾 `\` 取消换行，下一物理行仍属于同一条语句。本行通常提供一个文件、目录或参数。
# 原第 15 行说明：这是前面源码/头文件列表或编译命令中的一个路径参数；Shell 会把它作为独立文件传给检查命令或编译器。
REQUEST_SOURCES="srcs/Request/Request.cpp \
srcs/Request/RequestParser.cpp \
srcs/Request/RequestParserRequestLine.cpp \
srcs/Request/RequestParserUri.cpp \
srcs/Request/RequestParserHeaders.cpp \
srcs/Request/RequestParserBody.cpp \
srcs/Request/RequestParserChunked.cpp"

# 结构检查：仍然只有一个 class 静态入口，拆文件不能重新引入旧自由函数或中间转发层。
# 原第 18 行说明：开始 `if` 条件判断。Shell 根据命令退出码判断真假：0 为真，非 0 为假。
# 原第 19 行说明：这是上一条跨行命令或字符串的续行；末尾 `\` 取消换行，下一物理行仍属于同一条语句。本行通常提供一个文件、目录或参数。
# 原第 20 行说明：这是前面源码/头文件列表或编译命令中的一个路径参数；Shell 会把它作为独立文件传给检查命令或编译器。
if grep -R "parseRequestBuffer\|parse_buffer_internal" \
    includes/RequestParser.hpp srcs/Request \
    tests/module_tests/request_module_test.cpp >/dev/null 2>&1; then
# 原第 21 行说明：`echo` 输出后面的文本并自动换行；双引号会展开 `$变量`，同时保留文本中的空格。
    echo "[FAIL] 发现旧解析入口或无意义转发层"
# 原第 22 行说明：`exit 1` 立即结束整个脚本，并把状态码 1 返回给调用者；0 通常表示成功，非 0 表示失败。
    exit 1
# 原第 23 行说明：`fi` 是 `if` 的反向拼写，用来结束当前条件分支。
fi
# 原第 24 行说明：开始 `if` 条件判断。`-ne` 表示整数不相等
if [ "$(grep -c 'static int parseBuffer' includes/RequestParser.hpp)" -ne 1 ]; then
# 原第 25 行说明：`echo` 输出后面的文本并自动换行；双引号会展开 `$变量`，同时保留文本中的空格。
    echo "[FAIL] RequestParser.hpp 必须只声明一个公开 parseBuffer"
# 原第 26 行说明：`exit 1` 立即结束整个脚本，并把状态码 1 返回给调用者；0 通常表示成功，非 0 表示失败。
    exit 1
# 原第 27 行说明：`fi` 是 `if` 的反向拼写，用来结束当前条件分支。
fi
# 原第 28 行说明：开始 `if` 条件判断。`-d` 判断目录是否存在；`-ne` 表示整数不相等
if [ "$(grep -R -h '^int RequestParser::parseBuffer' srcs/Request/*.cpp | wc -l | tr -d ' ')" -ne 1 ]; then
# 原第 29 行说明：`echo` 输出后面的文本并自动换行；双引号会展开 `$变量`，同时保留文本中的空格。
    echo "[FAIL] RequestParser 必须只实现一个 parseBuffer"
# 原第 30 行说明：`exit 1` 立即结束整个脚本，并把状态码 1 返回给调用者；0 通常表示成功，非 0 表示失败。
    exit 1
# 原第 31 行说明：`fi` 是 `if` 的反向拼写，用来结束当前条件分支。
fi

# 文件职责检查：主文件只保留 parseBuffer；其余原有成员函数分布到职责文件，不增加新的 RequestParser 成员函数。
# 原第 34 行说明：开始 `if` 条件判断。`-ne` 表示整数不相等
if [ "$(grep -c '^\(bool\|int\|std::string\) RequestParser::' srcs/Request/RequestParser.cpp)" -ne 1 ]; then
# 原第 35 行说明：`echo` 输出后面的文本并自动换行；双引号会展开 `$变量`，同时保留文本中的空格。
    echo "[FAIL] RequestParser.cpp 应只保留唯一公开入口实现"
# 原第 36 行说明：`exit 1` 立即结束整个脚本，并把状态码 1 返回给调用者；0 通常表示成功，非 0 表示失败。
    exit 1
# 原第 37 行说明：`fi` 是 `if` 的反向拼写，用来结束当前条件分支。
fi
# 原第 38 行说明：开始 `if` 条件判断。`-f` 判断普通文件是否存在
# 原第 39 行说明：这是上一条跨行命令或字符串的续行；末尾 `\` 取消换行，下一物理行仍属于同一条语句。本行通常提供一个文件、目录或参数。
# 原第 40 行说明：这是上一条跨行命令或字符串的续行；末尾 `\` 取消换行，下一物理行仍属于同一条语句。本行通常提供一个文件、目录或参数。
# 原第 41 行说明：这是上一条跨行命令或字符串的续行；末尾 `\` 取消换行，下一物理行仍属于同一条语句。本行通常提供一个文件、目录或参数。
# 原第 42 行说明：`||` 表示只有上一条命令失败（退出码非 0）时，才执行右侧的失败处理。
if [ ! -f srcs/Request/RequestParserRequestLine.cpp ] \
    || [ ! -f srcs/Request/RequestParserUri.cpp ] \
    || [ ! -f srcs/Request/RequestParserHeaders.cpp ] \
    || [ ! -f srcs/Request/RequestParserBody.cpp ] \
    || [ ! -f srcs/Request/RequestParserChunked.cpp ]; then
# 原第 43 行说明：`echo` 输出后面的文本并自动换行；双引号会展开 `$变量`，同时保留文本中的空格。
    echo "[FAIL] RequestParser 拆分实现文件不完整"
# 原第 44 行说明：`exit 1` 立即结束整个脚本，并把状态码 1 返回给调用者；0 通常表示成功，非 0 表示失败。
    exit 1
# 原第 45 行说明：`fi` 是 `if` 的反向拼写，用来结束当前条件分支。
fi

# 冗余检查：解析状态统一放在 enum；Request 不保存连接生命周期；内部 lowercase 不暴露为全局函数。
# 原第 48 行说明：开始 `if` 条件判断。Shell 根据命令退出码判断真假：0 为真，非 0 为假。
# 原第 49 行说明：这是上一条跨行命令或字符串的续行；末尾 `\` 取消换行，下一物理行仍属于同一条语句。本行通常提供一个文件、目录或参数。
# 原第 50 行说明：这是前面源码/头文件列表或编译命令中的一个路径参数；Shell 会把它作为独立文件传给检查命令或编译器。
if grep -R "ERROR_MAX_BODY_LENGTH" \
    includes/Request.hpp includes/RequestParser.hpp \
    srcs/Request tests/module_tests/request_module_test.cpp >/dev/null 2>&1; then
# 原第 51 行说明：`echo` 输出后面的文本并自动换行；双引号会展开 `$变量`，同时保留文本中的空格。
    echo "[FAIL] 仍存在旧的 ERROR_MAX_BODY_LENGTH 宏状态"
# 原第 52 行说明：`exit 1` 立即结束整个脚本，并把状态码 1 返回给调用者；0 通常表示成功，非 0 表示失败。
    exit 1
# 原第 53 行说明：`fi` 是 `if` 的反向拼写，用来结束当前条件分支。
fi
# 原第 54 行说明：开始 `if` 条件判断。Shell 根据命令退出码判断真假：0 为真，非 0 为假。
# 原第 55 行说明：这是上一条跨行命令或字符串的续行；末尾 `\` 取消换行，下一物理行仍属于同一条语句。本行通常提供一个文件、目录或参数。
# 原第 56 行说明：这是前面源码/头文件列表或编译命令中的一个路径参数；Shell 会把它作为独立文件传给检查命令或编译器。
if grep -R "closeConnection\|shouldCloseConnection\|setCloseConnection" \
    includes/Request.hpp srcs/Request \
    tests/module_tests/request_module_test.cpp >/dev/null 2>&1; then
# 原第 57 行说明：`echo` 输出后面的文本并自动换行；双引号会展开 `$变量`，同时保留文本中的空格。
    echo "[FAIL] Request 仍保存不属于解析结果的连接生命周期状态"
# 原第 58 行说明：`exit 1` 立即结束整个脚本，并把状态码 1 返回给调用者；0 通常表示成功，非 0 表示失败。
    exit 1
# 原第 59 行说明：`fi` 是 `if` 的反向拼写，用来结束当前条件分支。
fi
# 原第 60 行说明：开始 `if` 条件判断。Shell 根据命令退出码判断真假：0 为真，非 0 为假。
# 原第 61 行说明：这是上一条跨行命令或字符串的续行；末尾 `\` 取消换行，下一物理行仍属于同一条语句。本行通常提供一个文件、目录或参数。
# 原第 62 行说明：执行检查命令并丢弃输出：`>` 重定向标准输出到 `/dev/null`，`2>&1` 再让文件描述符 2（标准错误）指向文件描述符 1 的当前位置；只保留退出码。
if grep -R "std::string to_lower\|to_lower(" \
    includes/Request.hpp srcs/Request tests/module_tests/request_module_test.cpp \
    >/dev/null 2>&1; then
# 原第 63 行说明：`echo` 输出后面的文本并自动换行；双引号会展开 `$变量`，同时保留文本中的空格。
    echo "[FAIL] Request 模块仍暴露全局 lowercase 工具"
# 原第 64 行说明：`exit 1` 立即结束整个脚本，并把状态码 1 返回给调用者；0 通常表示成功，非 0 表示失败。
    exit 1
# 原第 65 行说明：`fi` 是 `if` 的反向拼写，用来结束当前条件分支。
fi
# 原第 66 行说明：开始 `if` 条件判断。Shell 根据命令退出码判断真假：0 为真，非 0 为假。
# 原第 67 行说明：这是前面源码/头文件列表或编译命令中的一个路径参数；Shell 会把它作为独立文件传给检查命令或编译器。
if grep -R "has_required_host" \
    includes/RequestParser.hpp srcs/Request >/dev/null 2>&1; then
# 原第 68 行说明：`echo` 输出后面的文本并自动换行；双引号会展开 `$变量`，同时保留文本中的空格。
    echo "[FAIL] Host 必填检查仍存在额外转调用函数"
# 原第 69 行说明：`exit 1` 立即结束整个脚本，并把状态码 1 返回给调用者；0 通常表示成功，非 0 表示失败。
    exit 1
# 原第 70 行说明：`fi` 是 `if` 的反向拼写，用来结束当前条件分支。
fi

# ConfigRouteUtils 是 Request body-limit 的直接依赖：不得保留冗余 bool 输出、const_cast 或再次截 query。
# 原第 73 行说明：开始 `if` 条件判断。Shell 根据命令退出码判断真假：0 为真，非 0 为假。
# 原第 74 行说明：这是前面源码/头文件列表或编译命令中的一个路径参数；Shell 会把它作为独立文件传给检查命令或编译器。
if grep -R "use_location\|const_cast\|stripQueryString" \
    includes/ConfigRouteUtils.hpp srcs/Config/ConfigRouteUtils.cpp >/dev/null 2>&1; then
# 原第 75 行说明：`echo` 输出后面的文本并自动换行；双引号会展开 `$变量`，同时保留文本中的空格。
    echo "[FAIL] Request 路由辅助函数仍有冗余状态、const_cast 或重复 query 截断"
# 原第 76 行说明：`exit 1` 立即结束整个脚本，并把状态码 1 返回给调用者；0 通常表示成功，非 0 表示失败。
    exit 1
# 原第 77 行说明：`fi` 是 `if` 的反向拼写，用来结束当前条件分支。
fi

# 头文件只保留 includes/ 一份；同名旧副本会让不同源文件读取不同定义。
# 原第 80 行说明：开始 `if` 条件判断。`-e` 判断任意类型路径是否存在；`||` 表示左侧失败才执行右侧
# 原第 81 行说明：`||` 表示只有上一条命令失败（退出码非 0）时，才执行右侧的失败处理。
if [ -e srcs/Request/Request.hpp ] || [ -e srcs/Request/RequestParser.hpp ] \
    || [ -e srcs/Config/ConfigRouteUtils.hpp ]; then
# 原第 82 行说明：`echo` 输出后面的文本并自动换行；双引号会展开 `$变量`，同时保留文本中的空格。
    echo "[FAIL] srcs/ 中仍存在重复头文件，请只保留 includes/ 版本"
# 原第 83 行说明：`exit 1` 立即结束整个脚本，并把状态码 1 返回给调用者；0 通常表示成功，非 0 表示失败。
    exit 1
# 原第 84 行说明：`fi` 是 `if` 的反向拼写，用来结束当前条件分支。
fi

# 原第 86 行说明：`echo` 输出后面的文本并自动换行；双引号会展开 `$变量`，同时保留文本中的空格。
echo "[PASS] RequestParser 仍只有一个 class 静态入口"
# 原第 87 行说明：`echo` 输出后面的文本并自动换行；双引号会展开 `$变量`，同时保留文本中的空格。
echo "[PASS] 原有函数仅按职责移动到多个 .cpp，没有恢复 wrapper 或转发层"
# 原第 88 行说明：`echo` 输出后面的文本并自动换行；双引号会展开 `$变量`，同时保留文本中的空格。
echo "[PASS] Request 状态、成员和内部工具已清除旧宏与冗余接口"
# 原第 89 行说明：`echo` 输出后面的文本并自动换行；双引号会展开 `$变量`，同时保留文本中的空格。
echo "[PASS] body-limit 路由辅助函数已移除重复 query 处理与 const_cast"
# 原第 90 行说明：`echo` 输出后面的文本并自动换行；双引号会展开 `$变量`，同时保留文本中的空格。
echo "[PASS] Request 相关头文件只保留 includes/ 一份"

# 使用 Subject 要求的 C++98、全部 warning 和 Werror；EXTRA_CXXFLAGS 用于 sanitizer 复测。
# 原第 93 行说明：选择 C++ 编译器。`${CXX:-c++}` 表示：环境变量 `CXX` 已设置且非空时使用它，否则使用默认值 `c++`；`:-` 是带默认值的参数展开。
CXX=${CXX:-c++}
# 原第 94 行说明：保存编译选项字符串：`-Wall` 开启常见警告，`-Wextra` 开启额外警告，`-Werror` 把警告当错误，`-std=c++98` 强制 C++98；`-pedantic-errors`（若出现）把非标准扩展视为错误；`-I目录` 添加头文件搜索目录。
BASE_FLAGS="-Wall -Wextra -Werror -std=c++98 -Iincludes -I."
# 原第 95 行说明：读取可选的额外编译参数；`${EXTRA_CXXFLAGS:-}` 在变量未设置时展开为空，常用于外部传入 sanitizer 参数。
EXTRA_CXXFLAGS=${EXTRA_CXXFLAGS:-}
# 原第 96 行说明：保存测试构建目录路径；测试可执行文件放在这里，退出时统一删除。
BUILD_DIR="tests/module_tests/.build"
# 原第 97 行说明：`mkdir` 创建目录；`-p` 会同时创建缺失的父目录，并在目录已经存在时不报错。
mkdir -p "$BUILD_DIR"

# 完整项目中优先编译真实 Config；独立下载包中使用只含必要字段的 test_support 替身。
# 原第 100 行说明：开始 `if` 条件判断。`-f` 判断普通文件是否存在；`&&` 表示左侧成功才检查右侧
if [ -f srcs/Config/ServerConfig.cpp ] && [ -f srcs/Config/LocationConfig.cpp ]; then
# 原第 101 行说明：保存编译选项字符串：`-Wall` 开启常见警告，`-Wextra` 开启额外警告，`-Werror` 把警告当错误，`-std=c++98` 强制 C++98；`-pedantic-errors`（若出现）把非标准扩展视为错误；`-I目录` 添加头文件搜索目录。
    CXXFLAGS="$BASE_FLAGS -Isrcs/Config"
# 原第 102 行说明：给变量 `CONFIG_SOURCES` 赋值；后续通过 `$CONFIG_SOURCES` 或 `$$CONFIG_SOURCES`（实际写法为 `$CONFIG_SOURCES`/`$name`）展开。
    CONFIG_SOURCES="srcs/Config/Config.cpp srcs/Config/ConfigParser.cpp srcs/Config/ConfigRouteUtils.cpp srcs/Config/ServerConfig.cpp srcs/Config/LocationConfig.cpp"
# 原第 103 行说明：`echo` 输出后面的文本并自动换行；双引号会展开 `$变量`，同时保留文本中的空格。
    echo "[INFO] 使用完整项目中的真实 Config 类型运行 Request 测试"
# 原第 104 行说明：`else` 开始执行前面条件为假的分支。
else
# 原第 105 行说明：保存编译选项字符串：`-Wall` 开启常见警告，`-Wextra` 开启额外警告，`-Werror` 把警告当错误，`-std=c++98` 强制 C++98；`-pedantic-errors`（若出现）把非标准扩展视为错误；`-I目录` 添加头文件搜索目录。
    CXXFLAGS="$BASE_FLAGS -Itests/module_tests/test_support"
# 原第 106 行说明：给变量 `CONFIG_SOURCES` 赋值；后续通过 `$CONFIG_SOURCES` 或 `$$CONFIG_SOURCES`（实际写法为 `$CONFIG_SOURCES`/`$name`）展开。
    CONFIG_SOURCES="srcs/Config/ConfigRouteUtils.cpp"
# 原第 107 行说明：`echo` 输出后面的文本并自动换行；双引号会展开 `$变量`，同时保留文本中的空格。
    echo "[INFO] 使用下载包 test_support 运行独立 Request 测试"
# 原第 108 行说明：`fi` 是 `if` 的反向拼写，用来结束当前条件分支。
fi

# 原第 110 行说明：调用 C++ 编译器。未加引号的 flags/source 列表需要按空格拆成多个参数；`-I` 添加头文件搜索路径，`-o` 指定输出可执行文件。编译器路径本身加引号可保护空格。
# 原第 111 行说明：这是上一条跨行命令或字符串的续行；末尾 `\` 取消换行，下一物理行仍属于同一条语句。本行通常提供一个文件、目录或参数。
# 原第 112 行说明：这是上一条跨行命令或字符串的续行；末尾 `\` 取消换行，下一物理行仍属于同一条语句。本行通常提供一个文件、目录或参数。
# 原第 113 行说明：这是上一条跨行命令或字符串的续行；末尾 `\` 取消换行，下一物理行仍属于同一条语句。本行通常提供一个文件、目录或参数。
# 原第 114 行说明：本行继续完成当前 Shell 操作；`$变量`/`${...}` 展开变量或参数，双引号允许变量展开并阻止空白拆词。
$CXX $CXXFLAGS $EXTRA_CXXFLAGS \
    tests/module_tests/request_module_test.cpp \
    $REQUEST_SOURCES \
    $CONFIG_SOURCES \
    -o "$BUILD_DIR/request_module_test"

# 原第 116 行说明：运行刚编译出的测试可执行文件；双引号保护构建目录路径中的空格。
"$BUILD_DIR/request_module_test"
