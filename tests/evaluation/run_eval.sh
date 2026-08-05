#!/usr/bin/env bash

# 文件：tests/evaluation/run_eval.sh
# 用途：按新版 42 EvalHub Webserv 评测表准备独立测试环境并执行可自动化检查。
# 说明：终端输出使用英语；脚本注释使用中文。源码讲解、浏览器 Network 和完整泄漏判断仍需人工完成。
# Siege：保留传统文本与新版 JSON 两种输出格式的兼容解析。

set -u

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd -P)"
PROJECT_ROOT="$SCRIPT_DIR"
while [ ! -f "$PROJECT_ROOT/Makefile" ] && [ "$PROJECT_ROOT" != "/" ]; do
    PROJECT_ROOT="$(dirname "$PROJECT_ROOT")"
done

if [ ! -f "$PROJECT_ROOT/Makefile" ]; then
    echo "[FATAL] Could not locate the project root containing Makefile."
    exit 2
fi

MODE="full"
RUN_BUILD=1
RUN_MODULES=1
RUN_STRESS=1
RUN_BONUS=0
KEEP_RUNTIME=0
HOST="${EVAL_HOST:-127.0.0.1}"
PORT="${EVAL_PORT:-18080}"
PORT_B=$((PORT + 1))
PORT_C=$((PORT + 2))
PORT_HOST_A=$((PORT + 3))
PORT_HOST_B=$((PORT + 4))
PORT_DUPLICATE=$((PORT + 5))
PORT_COMMON=$((PORT + 6))
PORT_UNIQUE=$((PORT + 7))

usage()
{
    cat <<'EOF'
Usage: bash tests/evaluation/run_eval.sh [options]

Options:
  --quick                 Skip slow CGI timeout, port-conflict and Siege tests.
  --full                  Run all automatic mandatory tests, including Siege (default).
  --stress                Alias for --full; kept for compatibility with the old script.
  --bonus                 Run bonus cookie/session and second-CGI checks, but
                          only when automatic mandatory checks have no failure.
  --no-build              Do not run make fclean && make.
  --skip-modules          Do not run tests/module_tests scripts.
  --keep                  Keep generated runtime files and logs.
  --help                  Show this help.

Environment overrides:
  EVAL_HOST=127.0.0.1
  EVAL_PORT=18080
EOF
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --quick)
            MODE="quick"
            RUN_STRESS=0
            ;;
        --full)
            MODE="full"
            RUN_STRESS=1
            ;;
        --stress)
            MODE="full"
            RUN_STRESS=1
            ;;
        --bonus)
            RUN_BONUS=1
            ;;
        --no-build)
            RUN_BUILD=0
            ;;
        --skip-modules)
            RUN_MODULES=0
            ;;
        --keep)
            KEEP_RUNTIME=1
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        *)
            echo "[FATAL] Unknown option: $1"
            usage
            exit 2
            ;;
    esac
    shift
done

RUNTIME="$SCRIPT_DIR/.runtime"
SITE_ROOT="$RUNTIME/site"
MAPPED_ROOT="$RUNTIME/mapped"
ERROR_ROOT="$RUNTIME/errors"
LOG_ROOT="$RUNTIME/logs"
TMP_ROOT="$RUNTIME/tmp"
CONFIG_MAIN="$RUNTIME/evaluation.conf"
CONFIG_MULTI="$RUNTIME/multi_ports.conf"
CONFIG_VHOST="$RUNTIME/virtual_hosts.conf"
CONFIG_DUPLICATE="$RUNTIME/duplicate_port.conf"
CONFIG_COMMON_OWNER="$RUNTIME/common_port_owner.conf"
CONFIG_COMMON_CONFLICT="$RUNTIME/common_port_conflict.conf"
BIN="$PROJECT_ROOT/webserv"
RAW_HTTP="$SCRIPT_DIR/raw_http.py"
BASE="http://$HOST:$PORT"

PASS_COUNT=0
FAIL_COUNT=0
WARN_COUNT=0
SKIP_COUNT=0
MANUAL_COUNT=0
MANDATORY_FAIL_COUNT=0
BONUS_PASS_COUNT=0
BONUS_FAIL_COUNT=0
SUPPLEMENTAL_PASS_COUNT=0
SUPPLEMENTAL_FAIL_COUNT=0
CURRENT_SCOPE="mandatory"
SERVER_PID=""

if [ -t 1 ]; then
    GREEN=$'\033[0;32m'
    RED=$'\033[0;31m'
    YELLOW=$'\033[0;33m'
    BLUE=$'\033[0;34m'
    RESET=$'\033[0m'
else
    GREEN=''
    RED=''
    YELLOW=''
    BLUE=''
    RESET=''
fi

section()
{
    printf '\n%s==== %s ====%s\n' "$BLUE" "$1" "$RESET"
}

pass()
{
    PASS_COUNT=$((PASS_COUNT + 1))
    if [ "$CURRENT_SCOPE" = "bonus" ]; then
        BONUS_PASS_COUNT=$((BONUS_PASS_COUNT + 1))
    elif [ "$CURRENT_SCOPE" = "supplemental" ]; then
        SUPPLEMENTAL_PASS_COUNT=$((SUPPLEMENTAL_PASS_COUNT + 1))
    fi
    printf '%s[PASS]%s %s\n' "$GREEN" "$RESET" "$1"
}

fail()
{
    FAIL_COUNT=$((FAIL_COUNT + 1))
    if [ "$CURRENT_SCOPE" = "bonus" ]; then
        BONUS_FAIL_COUNT=$((BONUS_FAIL_COUNT + 1))
    elif [ "$CURRENT_SCOPE" = "supplemental" ]; then
        SUPPLEMENTAL_FAIL_COUNT=$((SUPPLEMENTAL_FAIL_COUNT + 1))
    else
        MANDATORY_FAIL_COUNT=$((MANDATORY_FAIL_COUNT + 1))
    fi
    printf '%s[FAIL]%s %s\n' "$RED" "$RESET" "$1"
}

warn()
{
    WARN_COUNT=$((WARN_COUNT + 1))
    printf '%s[WARN]%s %s\n' "$YELLOW" "$RESET" "$1"
}

skip()
{
    SKIP_COUNT=$((SKIP_COUNT + 1))
    printf '[SKIP] %s\n' "$1"
}

manual()
{
    MANUAL_COUNT=$((MANUAL_COUNT + 1))
    printf '[MANUAL] %s\n' "$1"
}

cleanup_server()
{
    if [ -n "$SERVER_PID" ] && kill -0 "$SERVER_PID" 2>/dev/null; then
        kill -INT "$SERVER_PID" 2>/dev/null || true
        sleep 1
        if kill -0 "$SERVER_PID" 2>/dev/null; then
            kill -TERM "$SERVER_PID" 2>/dev/null || true
            sleep 1
        fi
        if kill -0 "$SERVER_PID" 2>/dev/null; then
            kill -KILL "$SERVER_PID" 2>/dev/null || true
        fi
        wait "$SERVER_PID" 2>/dev/null || true
    fi
    SERVER_PID=""
}

cleanup()
{
    cleanup_server
    if [ "$KEEP_RUNTIME" -eq 0 ]; then
        rm -rf "$RUNTIME"
    else
        echo "[INFO] Runtime files kept at: $RUNTIME"
    fi
}

interrupt_handler()
{
    printf '\n[INFO] Interrupted by user. Cleaning up...\n'
    exit 130
}

terminate_handler()
{
    printf '\n[INFO] Terminated. Cleaning up...\n'
    exit 143
}

trap cleanup EXIT
trap interrupt_handler INT
trap terminate_handler TERM

require_command()
{
    if command -v "$1" >/dev/null 2>&1; then
        pass "Required command is available: $1"
        return 0
    fi
    fail "Required command is missing: $1"
    return 1
}

file_mtime()
{
    python3 - "$1" <<'PY'
import os
import sys
print(int(os.path.getmtime(sys.argv[1])))
PY
}

port_is_free()
{
    python3 - "$HOST" "$1" <<'PY'
import socket
import sys
host = sys.argv[1]
port = int(sys.argv[2])
sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
try:
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind((host, port))
except OSError:
    raise SystemExit(1)
finally:
    sock.close()
PY
}

wait_for_port()
{
    python3 - "$HOST" "$1" <<'PY'
import socket
import sys
import time
host = sys.argv[1]
port = int(sys.argv[2])
for _ in range(50):
    try:
        with socket.create_connection((host, port), timeout=0.2):
            raise SystemExit(0)
    except OSError:
        time.sleep(0.1)
raise SystemExit(1)
PY
}

wait_for_port_free()
{
    attempts=0
    while [ "$attempts" -lt 50 ]; do
        if port_is_free "$1"; then
            return 0
        fi
        sleep 0.1
        attempts=$((attempts + 1))
    done
    return 1
}

start_server()
{
    config_path="$1"
    log_path="$2"
    expected_port="$3"

    cleanup_server
    if ! wait_for_port_free "$expected_port"; then
        fail "Port $expected_port is still in use before server startup."
        if command -v lsof >/dev/null 2>&1; then
            echo "[INFO] Listener on port $expected_port:"
            lsof -nP -iTCP:"$expected_port" -sTCP:LISTEN || true
        fi
        return 1
    fi

    (
        cd "$PROJECT_ROOT" || exit 1
        exec "$BIN" "$config_path"
    ) >"$log_path" 2>&1 &
    SERVER_PID=$!

    if wait_for_port "$expected_port" && kill -0 "$SERVER_PID" 2>/dev/null; then
        pass "Webserv started on $HOST:$expected_port (PID $SERVER_PID)"
        return 0
    fi

    fail "Webserv did not start on $HOST:$expected_port"
    echo "[INFO] Last server log lines:"
    tail -n 30 "$log_path" 2>/dev/null || true
    cleanup_server
    return 1
}

server_alive()
{
    if [ -n "$SERVER_PID" ] && kill -0 "$SERVER_PID" 2>/dev/null; then
        return 0
    fi
    return 1
}

http_request()
{
    method="$1"
    url="$2"
    headers_file="$3"
    body_file="$4"
    shift 4

    curl -sS --max-time 3 \
        -D "$headers_file" \
        -o "$body_file" \
        -w '%{http_code}' \
        -X "$method" \
        "$@" \
        "$url" 2>"$TMP_ROOT/curl_error.log"
}

header_value()
{
    python3 - "$1" "$2" <<'PY'
import sys
path = sys.argv[1]
name = sys.argv[2].lower()
value = ""
with open(path, "r", encoding="latin1") as file:
    for raw_line in file:
        line = raw_line.rstrip("\r\n")
        if ":" not in line:
            continue
        key, current = line.split(":", 1)
        if key.strip().lower() == name:
            value = current.strip()
print(value)
PY
}

assert_status()
{
    actual="$1"
    expected="$2"
    name="$3"
    if [ "$actual" = "$expected" ]; then
        pass "$name: HTTP $actual"
    else
        fail "$name: expected HTTP $expected, got ${actual:-<empty>}"
    fi
}

assert_status_one_of()
{
    actual="$1"
    expected_list="$2"
    name="$3"
    for expected in $expected_list; do
        if [ "$actual" = "$expected" ]; then
            pass "$name: HTTP $actual"
            return
        fi
    done
    fail "$name: expected one of [$expected_list], got ${actual:-<empty>}"
}

assert_file_contains()
{
    file="$1"
    needle="$2"
    name="$3"
    if grep -F "$needle" "$file" >/dev/null 2>&1; then
        pass "$name"
    else
        fail "$name: missing [$needle]"
    fi
}

assert_file_not_contains()
{
    file="$1"
    needle="$2"
    name="$3"
    if grep -F "$needle" "$file" >/dev/null 2>&1; then
        fail "$name: unexpected [$needle]"
    else
        pass "$name"
    fi
}

assert_files_equal()
{
    expected="$1"
    actual="$2"
    name="$3"
    if cmp -s "$expected" "$actual"; then
        pass "$name"
    else
        fail "$name: file contents differ"
    fi
}

raw_status()
{
    python3 - "$1" <<'PY'
import sys
raw = open(sys.argv[1], "rb").read()
first = raw.split(b"\r\n", 1)[0].split(b"\n", 1)[0]
parts = first.split()
print(parts[1].decode("ascii", "replace") if len(parts) >= 2 else "")
PY
}

raw_body_size()
{
    python3 - "$1" <<'PY'
import sys
raw = open(sys.argv[1], "rb").read()
if b"\r\n\r\n" in raw:
    body = raw.split(b"\r\n\r\n", 1)[1]
elif b"\n\n" in raw:
    body = raw.split(b"\n\n", 1)[1]
else:
    body = b""
print(len(body))
PY
}

raw_body_contains()
{
    python3 - "$1" "$2" <<'PY'
import sys
raw = open(sys.argv[1], "rb").read()
needle = sys.argv[2].encode("utf-8")
if b"\r\n\r\n" in raw:
    body = raw.split(b"\r\n\r\n", 1)[1]
elif b"\n\n" in raw:
    body = raw.split(b"\n\n", 1)[1]
else:
    body = b""
raise SystemExit(0 if needle in body else 1)
PY
}

send_raw_request()
{
    request_file="$1"
    response_file="$2"
    shift 2
    python3 "$RAW_HTTP" \
        --host "$HOST" \
        --port "$PORT" \
        --request-file "$request_file" \
        --response-file "$response_file" \
        "$@"
}

prepare_runtime()
{
    rm -rf "$RUNTIME"
    mkdir -p "$SITE_ROOT/readonly"
    mkdir -p "$SITE_ROOT/upload"
    mkdir -p "$SITE_ROOT/list"
    mkdir -p "$SITE_ROOT/no-list"
    mkdir -p "$SITE_ROOT/default"
    mkdir -p "$SITE_ROOT/new"
    mkdir -p "$SITE_ROOT/cgi"
    mkdir -p "$MAPPED_ROOT"
    mkdir -p "$ERROR_ROOT"
    mkdir -p "$LOG_ROOT"
    mkdir -p "$TMP_ROOT"

    cat > "$SITE_ROOT/index.html" <<'EOF'
<!DOCTYPE html><html><body><h1>EVAL_SITE_A_OK</h1></body></html>
EOF
    printf 'HELLO_WEBSERV\n' > "$SITE_ROOT/hello.txt"
    : > "$SITE_ROOT/empty.html"
    printf 'READ_ONLY_FILE\n' > "$SITE_ROOT/readonly/keep.txt"
    printf 'AUTO_INDEX_A\n' > "$SITE_ROOT/list/a.txt"
    printf 'AUTO_INDEX_B\n' > "$SITE_ROOT/list/b.txt"
    printf 'SHOULD_NOT_BE_LISTED\n' > "$SITE_ROOT/no-list/secret.txt"
    printf 'DEFAULT_HOME_OK\n' > "$SITE_ROOT/default/home.html"
    printf 'REDIRECT_TARGET_OK\n' > "$SITE_ROOT/new/index.html"
    printf 'ROUTE_ROOT_OK\n' > "$MAPPED_ROOT/route.txt"

    for code in 400 403 404 405 409 411 413 414 415 423 500 501 502 504 505; do
        printf 'CUSTOM_ERROR_%s\n' "$code" > "$ERROR_ROOT/$code.html"
    done

    printf 'RELATIVE_OK\n' > "$SITE_ROOT/cgi/relative.txt"

    cat > "$SITE_ROOT/cgi/echo.py" <<'PY'
#!/usr/bin/env python3
import os
import sys

body = sys.stdin.read()
try:
    with open("relative.txt", "r", encoding="utf-8") as handle:
        relative_value = handle.read().strip()
except Exception as error:
    relative_value = "ERROR:" + str(error)

print("Content-Type: text/plain")
print()
print("CGI=PYTHON")
print("METHOD=" + os.environ.get("REQUEST_METHOD", ""))
print("QUERY=" + os.environ.get("QUERY_STRING", ""))
print("CONTENT_TYPE=" + os.environ.get("CONTENT_TYPE", ""))
print("CONTENT_LENGTH=" + os.environ.get("CONTENT_LENGTH", ""))
print("BODY=" + body)
print("RELATIVE=" + relative_value)
PY

    cat > "$SITE_ROOT/cgi/no_length.py" <<'PY'
#!/usr/bin/env python3
print("Content-Type: text/plain")
print()
print("CGI_NO_LENGTH_OK")
PY

    cat > "$SITE_ROOT/cgi/bad.py" <<'PY'
#!/usr/bin/env python3
raise RuntimeError("intentional CGI failure")
PY

    cat > "$SITE_ROOT/cgi/slow.py" <<'PY'
#!/usr/bin/env python3
import time
time.sleep(30)
print("Content-Type: text/plain")
print()
print("SLOW_CGI_FINISHED")
PY

    cat > "$SITE_ROOT/cgi/echo.sh" <<'SHCGI'
#!/bin/sh
BODY=$(cat)
printf 'Content-Type: text/plain\r\n'
printf '\r\n'
printf 'CGI=SHELL\n'
printf 'METHOD=%s\n' "$REQUEST_METHOD"
printf 'QUERY=%s\n' "$QUERY_STRING"
printf 'BODY=%s\n' "$BODY"
SHCGI

    chmod +x "$SITE_ROOT/cgi/echo.py" \
        "$SITE_ROOT/cgi/no_length.py" \
        "$SITE_ROOT/cgi/bad.py" \
        "$SITE_ROOT/cgi/slow.py" \
        "$SITE_ROOT/cgi/echo.sh"

    PYTHON_BIN="$(command -v python3)"
    SH_BIN="$(command -v sh)"

    cat > "$CONFIG_MAIN" <<EOF
server {
    listen $HOST:$PORT;
    server_name eval.local;
    root $SITE_ROOT;
    index index.html;
    autoindex off;
    max_body_size 100;
    allow_methods GET POST DELETE;
    upload_path upload;

    error_page 400 $ERROR_ROOT/400.html;
    error_page 403 $ERROR_ROOT/403.html;
    error_page 404 $ERROR_ROOT/404.html;
    error_page 405 $ERROR_ROOT/405.html;
    error_page 409 $ERROR_ROOT/409.html;
    error_page 411 $ERROR_ROOT/411.html;
    error_page 413 $ERROR_ROOT/413.html;
    error_page 414 $ERROR_ROOT/414.html;
    error_page 415 $ERROR_ROOT/415.html;
    error_page 423 $ERROR_ROOT/423.html;
    error_page 500 $ERROR_ROOT/500.html;
    error_page 501 $ERROR_ROOT/501.html;
    error_page 502 $ERROR_ROOT/502.html;
    error_page 504 $ERROR_ROOT/504.html;
    error_page 505 $ERROR_ROOT/505.html;

    location /readonly/ {
        allow_methods GET;
    }

    location /upload/ {
        allow_methods GET POST DELETE;
        upload_path upload;
    }

    location /multipart/ {
        allow_methods POST;
        upload_path upload;
        max_body_size 1M;
    }

    location /list/ {
        allow_methods GET;
        autoindex on;
    }

    location /no-list/ {
        allow_methods GET;
        autoindex off;
    }

    location /default/ {
        allow_methods GET;
        index home.html;
    }

    location /mapped/ {
        alias $MAPPED_ROOT;
        allow_methods GET;
    }

    location /old/ {
        return 301 /new/;
    }

    location /cgi/ {
        root $SITE_ROOT;
        allow_methods GET POST;
        cgi_extension .py $PYTHON_BIN;
        cgi_extension .sh $SH_BIN;
    }
}
EOF

    mkdir -p "$RUNTIME/site_b"
    printf '<h1>EVAL_SITE_B_OK</h1>\n' > "$RUNTIME/site_b/index.html"

    cat > "$CONFIG_MULTI" <<EOF
server {
    listen $HOST:$PORT_B;
    server_name site-a.local;
    root $SITE_ROOT;
    index index.html;
    allow_methods GET;
}

server {
    listen $HOST:$PORT_C;
    server_name site-b.local;
    root $RUNTIME/site_b;
    index index.html;
    allow_methods GET;
}
EOF

    mkdir -p "$RUNTIME/host_alpha" "$RUNTIME/host_beta"
    printf '<h1>HOST_ALPHA_OK</h1>\n' > "$RUNTIME/host_alpha/index.html"
    printf '<h1>HOST_BETA_OK</h1>\n' > "$RUNTIME/host_beta/index.html"

    # 两个不同 hostname 分别监听不同端口，用 curl --resolve 验证 Host 配置。
    # 新评测表随后会单独要求“同一端口配置多次”必须失败。
    cat > "$CONFIG_VHOST" <<EOF
server {
    listen $HOST:$PORT_HOST_A;
    server_name alpha.test;
    root $RUNTIME/host_alpha;
    index index.html;
    allow_methods GET;
}

server {
    listen $HOST:$PORT_HOST_B;
    server_name beta.test;
    root $RUNTIME/host_beta;
    index index.html;
    allow_methods GET;
}
EOF

    # 新评测表明确要求：同一个配置中，同一端口出现多次必须失败。
    cat > "$CONFIG_DUPLICATE" <<EOF
server {
    listen $HOST:$PORT_DUPLICATE;
    server_name duplicate-a.test;
    root $RUNTIME/host_alpha;
    index index.html;
    allow_methods GET;
}

server {
    listen $HOST:$PORT_DUPLICATE;
    server_name duplicate-b.test;
    root $RUNTIME/host_beta;
    index index.html;
    allow_methods GET;
}
EOF

    # 第一个进程独占公共端口。
    cat > "$CONFIG_COMMON_OWNER" <<EOF
server {
    listen $HOST:$PORT_COMMON;
    server_name owner.test;
    root $RUNTIME/host_alpha;
    index index.html;
    allow_methods GET;
}
EOF

    # 第二个进程同时请求已占用端口和一个空闲端口。
    # 新评测表要求确认：不能因为还有一个端口可用就忽略失效配置继续运行。
    cat > "$CONFIG_COMMON_CONFLICT" <<EOF
server {
    listen $HOST:$PORT_COMMON;
    server_name conflict.test;
    root $RUNTIME/host_beta;
    index index.html;
    allow_methods GET;
}

server {
    listen $HOST:$PORT_UNIQUE;
    server_name unique.test;
    root $RUNTIME/host_beta;
    index index.html;
    allow_methods GET;
}
EOF
}

run_module_with_timeout()
{
    module_script="$1"
    module_log="$2"
    module_timeout="${MODULE_TEST_TIMEOUT:-120}"

    python3 - "$PROJECT_ROOT" "$module_script" "$module_log" "$module_timeout" <<'PYMODULE'
import subprocess
import sys

root, script, log_path, timeout_text = sys.argv[1:]
timeout = float(timeout_text)
with open(log_path, "wb") as log:
    try:
        result = subprocess.run(
            ["bash", script],
            cwd=root,
            stdin=subprocess.DEVNULL,
            stdout=log,
            stderr=subprocess.STDOUT,
            timeout=timeout,
            check=False,
        )
    except subprocess.TimeoutExpired:
        log.write(("\n[ERROR] Module test timed out after %s seconds.\n" % timeout_text).encode())
        raise SystemExit(124)
raise SystemExit(result.returncode)
PYMODULE
}

run_source_review_checks()
{
    section "Mandatory source review precheck"

    scan_result="$TMP_ROOT/source_scan_result.txt"
    python3 - "$PROJECT_ROOT/srcs" "$LOG_ROOT" > "$scan_result" <<'PYSCAN'
import pathlib
import re
import sys

source_root = pathlib.Path(sys.argv[1])
log_root = pathlib.Path(sys.argv[2])

# 删除注释和字符串内容，但保留换行与大致字符位置，减少 grep 把注释当代码的误报。
def mask_non_code(text):
    out = []
    i = 0
    state = "code"
    while i < len(text):
        ch = text[i]
        nxt = text[i + 1] if i + 1 < len(text) else ""
        if state == "code":
            if ch == "/" and nxt == "/":
                out.extend("  ")
                i += 2
                state = "line_comment"
                continue
            if ch == "/" and nxt == "*":
                out.extend("  ")
                i += 2
                state = "block_comment"
                continue
            if ch == '"':
                out.append(" ")
                i += 1
                state = "string"
                continue
            if ch == "'":
                out.append(" ")
                i += 1
                state = "char"
                continue
            out.append(ch)
            i += 1
            continue
        if state == "line_comment":
            if ch == "\n":
                out.append("\n")
                state = "code"
            else:
                out.append(" ")
            i += 1
            continue
        if state == "block_comment":
            if ch == "*" and nxt == "/":
                out.extend("  ")
                i += 2
                state = "code"
            else:
                out.append("\n" if ch == "\n" else " ")
                i += 1
            continue
        if state in ("string", "char"):
            if ch == "\\":
                out.append(" ")
                if i + 1 < len(text):
                    out.append("\n" if text[i + 1] == "\n" else " ")
                i += 2
                continue
            end_char = '"' if state == "string" else "'"
            if ch == end_char:
                out.append(" ")
                i += 1
                state = "code"
            else:
                out.append("\n" if ch == "\n" else " ")
                i += 1
    return "".join(out)

multiplex_pattern = re.compile(r"\b(?:poll|select|epoll_wait|kevent)\s*\(")
io_pattern = re.compile(r"\b(?:read|recv|write|send)\s*\(")
errno_pattern = re.compile(r"\berrno\b")

multiplex_sites = []
io_sites = []
errno_near_io = []

for path in sorted(source_root.rglob("*")):
    if path.suffix not in {".cpp", ".cc", ".cxx", ".hpp", ".h"}:
        continue
    try:
        raw = path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        continue
    code = mask_non_code(raw)
    lines = code.splitlines()
    relative = path.relative_to(source_root.parent)
    for line_no, line in enumerate(lines, 1):
        for match in multiplex_pattern.finditer(line):
            multiplex_sites.append(f"{relative}:{line_no}:{match.group(0).strip()}")
        for match in io_pattern.finditer(line):
            io_sites.append(f"{relative}:{line_no}:{match.group(0).strip()}")
            end = min(len(lines), line_no + 8)
            nearby = "\n".join(lines[line_no - 1:end])
            if errno_pattern.search(nearby):
                errno_near_io.append(f"{relative}:{line_no}")

(log_root / "multiplex_calls.txt").write_text(
    "\n".join(multiplex_sites) + ("\n" if multiplex_sites else ""),
    encoding="utf-8",
)
(log_root / "fd_io_calls.txt").write_text(
    "\n".join(io_sites) + ("\n" if io_sites else ""),
    encoding="utf-8",
)
(log_root / "errno_near_fd_io.txt").write_text(
    "\n".join(errno_near_io) + ("\n" if errno_near_io else ""),
    encoding="utf-8",
)

print(f"MULTIPLEX_COUNT={len(multiplex_sites)}")
print(f"IO_COUNT={len(io_sites)}")
print(f"ERRNO_NEAR_IO_COUNT={len(errno_near_io)}")
PYSCAN

    multiplex_count="$(awk -F= '$1 == "MULTIPLEX_COUNT" {print $2}' "$scan_result")"
    io_count="$(awk -F= '$1 == "IO_COUNT" {print $2}' "$scan_result")"
    errno_near_count="$(awk -F= '$1 == "ERRNO_NEAR_IO_COUNT" {print $2}' "$scan_result")"

    if [ "$multiplex_count" = "1" ]; then
        pass "Exactly one multiplexing call was found under srcs/"
    elif [ "$multiplex_count" = "0" ]; then
        fail "No poll/select-equivalent call was found under srcs/"
    else
        fail "Found $multiplex_count poll/select-equivalent calls; the new EvalHub sheet requires one central call"
    fi

    if [ -n "$io_count" ] && [ "$io_count" -gt 0 ]; then
        pass "Found and logged $io_count read/recv/write/send call sites for evaluator review"
    else
        fail "No read/recv/write/send call site was found under srcs/"
    fi

    if [ -n "$errno_near_count" ] && [ "$errno_near_count" -gt 0 ]; then
        warn "Found $errno_near_count possible errno checks near fd I/O; inspect $LOG_ROOT/errno_near_fd_io.txt"
    else
        pass "No nearby errno token was found by the source-scan heuristic"
    fi

    manual "Explain the HTTP server basics and how the single poll/select-equivalent works in the main loop."
    manual "Show that read readiness and write readiness are checked in the same multiplexing loop."
    manual "Trace poll/select to accept, one client read/recv, one client write/send, and CGI-pipe I/O; verify at most one I/O action per fd per readiness event."
    manual "Review every read/recv/write/send return value: both zero and negative results must be handled, and socket/pipe errors must remove or clean the client/task."
    manual "Confirm no code checks errno after read/recv/write/send; any such check is a mandatory-stop condition in the new sheet."
    manual "Confirm every fd read/write is readiness-driven by the single multiplexing mechanism; the heuristic scan cannot prove control flow."
}

run_build_checks()
{
    section "Build and module tests"

    if [ "$RUN_BUILD" -eq 0 ]; then
        skip "Build was disabled by --no-build."
    else
        if (cd "$PROJECT_ROOT" && make fclean >"$LOG_ROOT/make_fclean.log" 2>&1 \
            && make >"$LOG_ROOT/make.log" 2>&1); then
            pass "make fclean && make"
        else
            fail "Compilation failed. See $LOG_ROOT/make.log"
            tail -n 30 "$LOG_ROOT/make.log" 2>/dev/null || true
            return 1
        fi

        if [ -x "$BIN" ]; then
            pass "webserv executable was produced"
        else
            fail "webserv executable is missing"
            return 1
        fi

        before_mtime="$(file_mtime "$BIN")"
        sleep 1
        if (cd "$PROJECT_ROOT" && make >"$LOG_ROOT/make_second.log" 2>&1); then
            after_mtime="$(file_mtime "$BIN")"
            if [ "$before_mtime" = "$after_mtime" ]; then
                pass "Second make does not relink webserv"
            else
                fail "Second make changed the webserv timestamp (unnecessary relink)"
            fi
        else
            fail "Second make failed"
        fi
    fi

    if [ "$RUN_MODULES" -eq 0 ]; then
        skip "Supplemental module tests were disabled by --skip-modules."
    else
        CURRENT_SCOPE="supplemental"
        found_module=0
        for script in "$PROJECT_ROOT"/tests/module_tests/run_*_test_annotated.sh; do
            if [ ! -f "$script" ]; then
                continue
            fi
            found_module=1
            name="$(basename "$script")"
            printf '[RUN] Module test: %s (timeout: %ss)
' "$name" "${MODULE_TEST_TIMEOUT:-120}"
            if run_module_with_timeout "$script" "$LOG_ROOT/$name.log"; then
                pass "Module test: $name"
            else
                module_status=$?
                if [ "$module_status" -eq 124 ]; then
                    fail "Module test timed out: $name (see $LOG_ROOT/$name.log)"
                else
                    fail "Module test failed: $name (see $LOG_ROOT/$name.log)"
                fi
            fi
        done
        if [ "$found_module" -eq 0 ]; then
            skip "No annotated module test scripts were found."
        fi
        CURRENT_SCOPE="mandatory"
    fi

    if command -v nm >/dev/null 2>&1; then
        forbidden_symbols="$(nm -u "$BIN" 2>/dev/null \
            | grep -E '(^|[[:space:]_])(system|popen|fopen|fread|fwrite)(@|$)' || true)"
        if [ -z "$forbidden_symbols" ]; then
            pass "No obvious forbidden C I/O/process symbols in the executable"
        else
            fail "Possible forbidden symbols found in the executable"
            printf '%s\n' "$forbidden_symbols"
        fi
    else
        skip "nm is unavailable; forbidden symbol scan was skipped."
    fi

    run_source_review_checks
}

run_core_http_tests()
{
    section "Configuration and basic HTTP"

    headers="$TMP_ROOT/headers"
    body="$TMP_ROOT/body"

    status="$(http_request GET "$BASE/" "$headers" "$body")"
    assert_status "$status" 200 "GET server index"
    assert_file_contains "$body" "EVAL_SITE_A_OK" "Server index body"

    status="$(http_request GET "$BASE/hello.txt" "$headers" "$body")"
    assert_status "$status" 200 "GET static file"
    assert_file_contains "$body" "HELLO_WEBSERV" "Static file body"
    content_type="$(header_value "$headers" "Content-Type")"
    if [ "$content_type" = "text/plain" ]; then
        pass "Static Content-Type is text/plain"
    else
        fail "Static Content-Type: expected text/plain, got [$content_type]"
    fi

    status="$(http_request GET "$BASE/empty.html" "$headers" "$body")"
    assert_status "$status" 200 "GET empty file"
    body_size="$(wc -c < "$body" | tr -d ' ')"
    if [ "$body_size" = "0" ]; then
        pass "Empty file body has zero bytes"
    else
        fail "Empty file body should be zero bytes, got $body_size"
    fi

    status="$(http_request GET "$BASE/missing-resource" "$headers" "$body")"
    assert_status "$status" 404 "Custom 404"
    assert_file_contains "$body" "CUSTOM_ERROR_404" "Custom 404 body"

    status="$(http_request GET "$BASE/default/" "$headers" "$body")"
    assert_status "$status" 200 "Directory default index"
    assert_file_contains "$body" "DEFAULT_HOME_OK" "Default index content"

    status="$(http_request GET "$BASE/list/" "$headers" "$body")"
    assert_status "$status" 200 "Autoindex enabled"
    assert_file_contains "$body" "a.txt" "Autoindex contains a.txt"
    assert_file_contains "$body" "b.txt" "Autoindex contains b.txt"

    status="$(http_request GET "$BASE/no-list/" "$headers" "$body")"
    assert_status "$status" 404 "Autoindex disabled (school tester policy)"
    assert_file_not_contains "$body" "secret.txt" "Disabled autoindex does not leak entries"

    status="$(http_request GET "$BASE/mapped/route.txt" "$headers" "$body")"
    assert_status "$status" 200 "Location alias mapping"
    assert_file_contains "$body" "ROUTE_ROOT_OK" "Location alias content"

    status="$(http_request GET "$BASE/old/anything" "$headers" "$body")"
    assert_status "$status" 301 "Configured redirect"
    location="$(header_value "$headers" "Location")"
    if [ "$location" = "/new/" ]; then
        pass "Redirect Location header"
    else
        fail "Redirect Location: expected /new/, got [$location]"
    fi

    follow_status="$(curl -sS --max-time 10 -L -o "$body" -w '%{http_code}' "$BASE/old/anything" 2>/dev/null)"
    assert_status "$follow_status" 200 "Follow redirect"
    assert_file_contains "$body" "REDIRECT_TARGET_OK" "Redirect target content"

    rm -f "$SITE_ROOT/readonly/forbidden.txt"
    status="$(http_request POST "$BASE/readonly/forbidden.txt" "$headers" "$body" \
        -H 'Content-Type: text/plain' --data-binary 'x')"
    assert_status "$status" 405 "POST rejected by GET-only route"
    allow="$(header_value "$headers" "Allow")"
    normalized_allow="$(printf '%s' "$allow" | tr -d '[:space:]' | tr '[:lower:]' '[:upper:]')"
    if [ "$normalized_allow" = "GET" ]; then
        pass "405 Allow header advertises only configured GET"
    else
        fail "405 Allow header should be exactly GET; got [$allow]"
    fi
    if [ ! -e "$SITE_ROOT/readonly/forbidden.txt" ]; then
        pass "Rejected POST did not create a file"
    else
        fail "Rejected POST created a file"
    fi

    status="$(http_request DELETE "$BASE/readonly/keep.txt" "$headers" "$body")"
    assert_status "$status" 405 "DELETE rejected by GET-only route"
    if [ -f "$SITE_ROOT/readonly/keep.txt" ]; then
        pass "Rejected DELETE kept the source file"
    else
        fail "Rejected DELETE removed the source file"
    fi
}

run_upload_tests()
{
    section "Upload, body limit and DELETE"

    headers="$TMP_ROOT/headers"
    body="$TMP_ROOT/body"

    python3 - "$TMP_ROOT" <<'PY'
from pathlib import Path
import sys
root = Path(sys.argv[1])
(root / "body99").write_bytes(b"a" * 99)
(root / "body100").write_bytes(b"b" * 100)
(root / "body101").write_bytes(b"c" * 101)
(root / "binary.bin").write_bytes(bytes(range(80)))
PY

    rm -f "$SITE_ROOT/upload/body99.txt" "$SITE_ROOT/upload/body99_"*.txt 2>/dev/null || true
    status="$(http_request POST "$BASE/upload/body99.txt" "$headers" "$body" \
        -H 'Content-Type: text/plain' --data-binary "@$TMP_ROOT/body99")"
    assert_status_one_of "$status" "200 201" "POST 99-byte body"
    if [ -f "$SITE_ROOT/upload/body99.txt" ]; then
        assert_files_equal "$TMP_ROOT/body99" "$SITE_ROOT/upload/body99.txt" "99-byte upload content"
    else
        fail "99-byte upload target file is missing"
    fi

    rm -f "$SITE_ROOT/upload/body100.txt" "$SITE_ROOT/upload/body100_"*.txt 2>/dev/null || true
    status="$(http_request POST "$BASE/upload/body100.txt" "$headers" "$body" \
        -H 'Content-Type: text/plain' --data-binary "@$TMP_ROOT/body100")"
    assert_status_one_of "$status" "200 201" "POST exact body limit"
    if [ -f "$SITE_ROOT/upload/body100.txt" ]; then
        assert_files_equal "$TMP_ROOT/body100" "$SITE_ROOT/upload/body100.txt" "100-byte upload content"
    else
        fail "100-byte upload target file is missing"
    fi

    rm -f "$SITE_ROOT/upload/body101.txt" "$SITE_ROOT/upload/body101_"*.txt 2>/dev/null || true
    status="$(http_request POST "$BASE/upload/body101.txt" "$headers" "$body" \
        -H 'Content-Type: text/plain' --data-binary "@$TMP_ROOT/body101")"
    assert_status "$status" 413 "POST over body limit"
    if [ ! -e "$SITE_ROOT/upload/body101.txt" ]; then
        pass "Over-limit POST did not create a file"
    else
        fail "Over-limit POST created a file"
    fi

    rm -f "$SITE_ROOT/upload/eval.bin" "$SITE_ROOT/upload/eval_"*.bin 2>/dev/null || true
    status="$(http_request POST "$BASE/upload/eval.bin" "$headers" "$body" \
        -H 'Content-Type: application/octet-stream' --data-binary "@$TMP_ROOT/binary.bin")"
    assert_status_one_of "$status" "200 201" "Binary upload creates a resource"

    status="$(http_request GET "$BASE/upload/eval.bin" "$headers" "$body")"
    assert_status "$status" 200 "Retrieve uploaded binary"
    assert_files_equal "$TMP_ROOT/binary.bin" "$body" "Uploaded binary round trip"

    multipart_status="$(curl -sS --max-time 3 -D "$headers" -o "$body" \
        -w '%{http_code}' -F "file=@$TMP_ROOT/binary.bin" "$BASE/multipart/form.bin" 2>/dev/null)"
    if [ "$multipart_status" = "200" ] || [ "$multipart_status" = "201" ]; then
        pass "multipart/form-data upload is implemented"
    elif [ "$multipart_status" = "415" ]; then
        pass "multipart/form-data is deliberately rejected with 415 (not mandatory)"
    else
        fail "multipart/form-data should succeed with 200/201 or be rejected with 415; got $multipart_status"
    fi

    status="$(http_request DELETE "$BASE/upload/eval.bin" "$headers" "$body")"
    assert_status_one_of "$status" "200 204" "DELETE uploaded resource"
    if [ "$status" = "204" ]; then
        body_size="$(wc -c < "$body" | tr -d ' ')"
        if [ "$body_size" = "0" ]; then
            pass "204 response has no body"
        else
            fail "204 response body should be empty, got $body_size bytes"
        fi
    fi
    if [ ! -e "$SITE_ROOT/upload/eval.bin" ]; then
        pass "DELETE removed the target file"
    else
        fail "DELETE did not remove the target file"
    fi

    status="$(http_request GET "$BASE/upload/eval.bin" "$headers" "$body")"
    assert_status "$status" 404 "GET deleted resource"

    status="$(http_request DELETE "$BASE/upload/no-such-file.txt" "$headers" "$body")"
    assert_status "$status" 404 "DELETE missing resource"

    status="$(http_request DELETE "$BASE/upload/" "$headers" "$body")"
    assert_status "$status" 403 "DELETE directory is forbidden"
}

run_raw_request_tests()
{
    section "Raw HTTP parser and method handling"

    request="$TMP_ROOT/request.raw"
    response="$TMP_ROOT/response.raw"

    printf 'BREW /hello.txt HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n' > "$request"
    if send_raw_request "$request" "$response"; then
        status="$(raw_status "$response")"
        assert_status "$status" 501 "Unknown method"
    else
        fail "Unknown method request could not be sent"
    fi

    printf 'GET\r\nHost: localhost\r\nConnection: close\r\n\r\n' > "$request"
    if send_raw_request "$request" "$response"; then
        status="$(raw_status "$response")"
        assert_status "$status" 400 "Malformed request line"
    else
        fail "Malformed request test could not be sent"
    fi

    printf 'GET /hello.txt HTTP/1.1\r\nConnection: close\r\n\r\n' > "$request"
    if send_raw_request "$request" "$response"; then
        status="$(raw_status "$response")"
        assert_status "$status" 400 "HTTP/1.1 request without Host"
    else
        fail "Missing Host test could not be sent"
    fi

    printf 'POST /upload/conflict.txt HTTP/1.1\r\nHost: localhost\r\nContent-Type: text/plain\r\nContent-Length: 3\r\nContent-Length: 6\r\nConnection: close\r\n\r\nabc' > "$request"
    if send_raw_request "$request" "$response"; then
        status="$(raw_status "$response")"
        assert_status "$status" 400 "Conflicting Content-Length"
    else
        fail "Conflicting Content-Length test could not be sent"
    fi

    rm -f "$SITE_ROOT/upload/chunk.txt" "$SITE_ROOT/upload/chunk_"*.txt 2>/dev/null || true
    printf 'POST /upload/chunk.txt HTTP/1.1\r\nHost: localhost\r\nTransfer-Encoding: chunked\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\n4\r\nWiki\r\n5\r\npedia\r\n0\r\n\r\n' > "$request"
    if send_raw_request "$request" "$response"; then
        status="$(raw_status "$response")"
        assert_status_one_of "$status" "200 201" "Chunked upload"
        if [ -f "$SITE_ROOT/upload/chunk.txt" ]; then
            if [ "$(cat "$SITE_ROOT/upload/chunk.txt")" = "Wikipedia" ]; then
                pass "Chunked upload was decoded before writing"
            else
                fail "Chunked upload file does not contain decoded body"
            fi
        else
            fail "Chunked upload file is missing"
        fi
    else
        fail "Chunked upload request could not be sent"
    fi

    printf 'GET /hello.txt HTTP/1.1\r\n<FRAGMENT>Host: localhost\r\n<FRAGMENT>Connection: close\r\n\r\n' > "$request"
    if send_raw_request "$request" "$response" --fragment-delay 0.25; then
        status="$(raw_status "$response")"
        assert_status "$status" 200 "Fragmented request across multiple sends"
        if raw_body_contains "$response" "HELLO_WEBSERV"; then
            pass "Fragmented request body is correct"
        else
            fail "Fragmented request returned an unexpected body"
        fi
    else
        fail "Fragmented request test could not be sent"
    fi

    if server_alive; then
        pass "Server is still alive after malformed and unknown requests"
    else
        fail "Server terminated during raw HTTP tests"
    fi
}

run_cgi_tests()
{
    section "CGI mandatory tests"

    headers="$TMP_ROOT/headers"
    body="$TMP_ROOT/body"

    status="$(http_request GET "$BASE/cgi/echo.py?name=Alice&value=42" "$headers" "$body")"
    assert_status "$status" 200 "Python CGI GET"
    assert_file_contains "$body" "CGI=PYTHON" "Python CGI executed"
    assert_file_contains "$body" "METHOD=GET" "CGI GET method"
    assert_file_contains "$body" "QUERY=name=Alice&value=42" "CGI query string"
    assert_file_contains "$body" "RELATIVE=RELATIVE_OK" "CGI working directory"

    status="$(http_request POST "$BASE/cgi/echo.py?source=post" "$headers" "$body" \
        -H 'Content-Type: text/plain' --data-binary 'CGI_POST_BODY')"
    assert_status "$status" 200 "Python CGI POST"
    assert_file_contains "$body" "METHOD=POST" "CGI POST method"
    assert_file_contains "$body" "QUERY=source=post" "CGI POST query"
    assert_file_contains "$body" "BODY=CGI_POST_BODY" "CGI POST body"
    assert_file_contains "$body" "CONTENT_TYPE=text/plain" "CGI Content-Type environment"

    status="$(http_request GET "$BASE/cgi/no_length.py" "$headers" "$body")"
    assert_status "$status" 200 "CGI output without Content-Length"
    assert_file_contains "$body" "CGI_NO_LENGTH_OK" "CGI EOF-delimited body"

    request="$TMP_ROOT/request.raw"
    response="$TMP_ROOT/response.raw"
    printf 'POST /cgi/echo.py HTTP/1.1\r\nHost: localhost\r\nTransfer-Encoding: chunked\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\n4\r\nWiki\r\n5\r\npedia\r\n0\r\n\r\n' > "$request"
    if send_raw_request "$request" "$response" --timeout 10; then
        status="$(raw_status "$response")"
        assert_status "$status" 200 "Chunked CGI POST"
        if raw_body_contains "$response" "BODY=Wikipedia"; then
            pass "Chunked CGI body was decoded before CGI stdin"
        else
            fail "CGI did not receive the decoded chunked body"
        fi
    else
        fail "Chunked CGI request could not be sent"
    fi

    status="$(http_request GET "$BASE/cgi/missing.py" "$headers" "$body")"
    assert_status "$status" 404 "Missing CGI script"

    status="$(http_request GET "$BASE/cgi/bad.py" "$headers" "$body")"
    if [ "$status" = "500" ] || [ "$status" = "502" ]; then
        pass "CGI process failure: HTTP $status"
    else
        fail "CGI process failure: expected HTTP 500 or 502, got $status"
    fi
    if server_alive; then
        pass "Server survived the failing CGI"
    else
        fail "Server terminated after the failing CGI"
    fi

    if [ "$MODE" = "quick" ]; then
        skip "Slow CGI timeout and concurrency test in quick mode."
        return
    fi

    section "CGI timeout and non-blocking behavior"
    slow_headers="$TMP_ROOT/slow_headers"
    slow_body="$TMP_ROOT/slow_body"
    slow_status_file="$TMP_ROOT/slow_status"

    (
        curl -sS --max-time 20 \
            -D "$slow_headers" \
            -o "$slow_body" \
            -w '%{http_code}' \
            "$BASE/cgi/slow.py" > "$slow_status_file" 2>"$TMP_ROOT/slow_curl_error"
    ) &
    slow_pid=$!

    sleep 1
    start_time="$(python3 - <<'PY'
import time
print(time.monotonic())
PY
)"
    normal_status="$(curl -sS --max-time 5 -o "$TMP_ROOT/during_slow" -w '%{http_code}' "$BASE/hello.txt" 2>/dev/null)"
    end_time="$(python3 - <<'PY'
import time
print(time.monotonic())
PY
)"
    elapsed="$(python3 - "$start_time" "$end_time" <<'PY'
import sys
print(float(sys.argv[2]) - float(sys.argv[1]))
PY
)"

    assert_status "$normal_status" 200 "Normal GET during slow CGI"
    if python3 - "$elapsed" <<'PY'
import sys
raise SystemExit(0 if float(sys.argv[1]) < 3.0 else 1)
PY
    then
        pass "Slow CGI did not block a normal GET (${elapsed}s)"
    else
        fail "Normal GET was delayed by slow CGI (${elapsed}s)"
    fi

    wait "$slow_pid" 2>/dev/null || true
    slow_status="$(cat "$slow_status_file" 2>/dev/null || true)"
    assert_status "$slow_status" 504 "Slow CGI timeout"

    zombie_count="$(ps -eo stat=,command= 2>/dev/null \
        | grep -E '[s]low.py|[e]cho.py|[b]ad.py|[e]cho.sh' \
        | awk '$1 ~ /^Z/ {count++} END {print count+0}')"
    if [ "$zombie_count" = "0" ]; then
        pass "No visible zombie CGI process"
    else
        fail "Zombie CGI processes detected: $zombie_count"
    fi
}

run_bonus_cgi_tests()
{
    section "Bonus: second CGI system"

    headers="$TMP_ROOT/headers"
    body="$TMP_ROOT/body"

    status="$(http_request POST "$BASE/cgi/echo.sh?kind=shell" "$headers" "$body" \
        -H 'Content-Type: text/plain' --data-binary 'SHELL_BODY')"
    assert_status "$status" 200 "Shell CGI POST"
    assert_file_contains "$body" "CGI=SHELL" "Second CGI system executed"
    assert_file_contains "$body" "BODY=SHELL_BODY" "Second CGI system received POST body"
}

run_session_tests()
{
    section "Bonus cookie and session"

    cookie_jar="$TMP_ROOT/cookies.txt"
    headers="$TMP_ROOT/headers"
    body="$TMP_ROOT/body"
    rm -f "$cookie_jar"

    status="$(curl -sS --max-time 10 -D "$headers" -o "$body" \
        -c "$cookie_jar" -w '%{http_code}' "$BASE/session/counter" 2>/dev/null)"
    assert_status "$status" 200 "Session counter first request"
    assert_file_contains "$body" "Visits: 1" "Session first visit"
    set_cookie="$(header_value "$headers" "Set-Cookie")"
    if printf '%s' "$set_cookie" | grep -F 'WEBSERV_SESSION=' >/dev/null 2>&1; then
        pass "Session Set-Cookie header"
    else
        fail "Session cookie was not set"
    fi

    status="$(curl -sS --max-time 10 -D "$headers" -o "$body" \
        -b "$cookie_jar" -c "$cookie_jar" -w '%{http_code}' "$BASE/session/counter" 2>/dev/null)"
    assert_status "$status" 200 "Session counter second request"
    assert_file_contains "$body" "Visits: 2" "Session was resumed"

    old_id="$(awk '$6 == "WEBSERV_SESSION" {print $7}' "$cookie_jar" | tail -n 1)"
    status="$(curl -sS --max-time 10 -D "$headers" -o "$body" \
        -b "$cookie_jar" -c "$cookie_jar" -X POST \
        -H 'Content-Type: text/plain' --data-binary 'Alice' \
        -w '%{http_code}' "$BASE/session/login" 2>/dev/null)"
    assert_status "$status" 200 "Session login"
    assert_file_contains "$body" "User: Alice" "Session login body"
    new_id="$(awk '$6 == "WEBSERV_SESSION" {print $7}' "$cookie_jar" | tail -n 1)"
    if [ -n "$old_id" ] && [ -n "$new_id" ] && [ "$old_id" != "$new_id" ]; then
        pass "Session ID rotated during login"
    else
        fail "Session ID did not rotate during login"
    fi

    status="$(curl -sS --max-time 10 -D "$headers" -o "$body" \
        -b "$cookie_jar" -c "$cookie_jar" -X POST --data '' \
        -w '%{http_code}' "$BASE/session/logout" 2>/dev/null)"
    assert_status "$status" 200 "Session logout"
    set_cookie="$(header_value "$headers" "Set-Cookie")"
    if printf '%s' "$set_cookie" | grep -E 'Max-Age=0|Expires=' >/dev/null 2>&1; then
        pass "Logout expires the session cookie"
    else
        fail "Logout did not expire the session cookie"
    fi

    status="$(http_request GET "$BASE/session/login" "$headers" "$body")"
    assert_status "$status" 405 "Session login rejects GET"
    allow="$(header_value "$headers" "Allow")"
    if [ "$allow" = "POST" ]; then
        pass "Session 405 Allow header"
    else
        fail "Session 405 Allow: expected POST, got [$allow]"
    fi
}

run_multi_port_tests()
{
    if [ "$MODE" = "quick" ]; then
        skip "Multi-port, hostname and occupied-port tests in quick mode."
        return
    fi

    section "Configuration: multiple ports"
    cleanup_server

    if ! start_server "$CONFIG_MULTI" "$LOG_ROOT/multi_server.log" "$PORT_B"; then
        return
    fi

    if wait_for_port "$PORT_C"; then
        pass "Second configured port is listening: $PORT_C"
    else
        fail "Second configured port did not start: $PORT_C"
    fi

    body_a="$(curl -sS --max-time 5 "http://$HOST:$PORT_B/" 2>/dev/null || true)"
    body_b="$(curl -sS --max-time 5 "http://$HOST:$PORT_C/" 2>/dev/null || true)"
    if printf '%s' "$body_a" | grep -F 'EVAL_SITE_A_OK' >/dev/null 2>&1; then
        pass "First port serves site A"
    else
        fail "First port did not serve site A"
    fi
    if printf '%s' "$body_b" | grep -F 'EVAL_SITE_B_OK' >/dev/null 2>&1; then
        pass "Second port serves site B"
    else
        fail "Second port did not serve site B"
    fi

    section "Configuration: hostnames with curl --resolve"
    cleanup_server

    if start_server "$CONFIG_VHOST" "$LOG_ROOT/vhost_server.log" "$PORT_HOST_A"; then
        if wait_for_port "$PORT_HOST_B"; then
            pass "Second hostname server is listening: $PORT_HOST_B"
        else
            fail "Second hostname server did not start: $PORT_HOST_B"
        fi

        alpha_body="$(curl -sS --noproxy '*' --max-time 5 \
            --resolve "alpha.test:$PORT_HOST_A:$HOST" \
            "http://alpha.test:$PORT_HOST_A/" 2>/dev/null || true)"
        beta_body="$(curl -sS --noproxy '*' --max-time 5 \
            --resolve "beta.test:$PORT_HOST_B:$HOST" \
            "http://beta.test:$PORT_HOST_B/" 2>/dev/null || true)"

        if printf '%s' "$alpha_body" | grep -F 'HOST_ALPHA_OK' >/dev/null 2>&1; then
            pass "alpha.test resolves to and serves the alpha host configuration"
        else
            fail "alpha.test did not serve the alpha host configuration"
        fi
        if printf '%s' "$beta_body" | grep -F 'HOST_BETA_OK' >/dev/null 2>&1; then
            pass "beta.test resolves to and serves the beta host configuration"
        else
            fail "beta.test did not serve the beta host configuration"
        fi
    fi

    section "Port issues: duplicate configuration"
    cleanup_server

    if ! port_is_free "$PORT_DUPLICATE"; then
        fail "Duplicate-test port $PORT_DUPLICATE was already in use"
    else
        (
            cd "$PROJECT_ROOT" || exit 1
            exec "$BIN" "$CONFIG_DUPLICATE"
        ) >"$LOG_ROOT/duplicate_port_server.log" 2>&1 &
        duplicate_pid=$!
        sleep 1

        if kill -0 "$duplicate_pid" 2>/dev/null; then
            fail "A configuration that declares the same port twice was accepted"
            kill -INT "$duplicate_pid" 2>/dev/null || true
            sleep 1
            kill -KILL "$duplicate_pid" 2>/dev/null || true
            wait "$duplicate_pid" 2>/dev/null || true
        else
            wait "$duplicate_pid" 2>/dev/null || true
            pass "A configuration that declares the same port twice was rejected"
        fi
    fi

    section "Port issues: two processes with a common port"
    cleanup_server

    if start_server "$CONFIG_COMMON_OWNER" "$LOG_ROOT/common_owner.log" "$PORT_COMMON"; then
        owner_pid="$SERVER_PID"
        (
            cd "$PROJECT_ROOT" || exit 1
            exec "$BIN" "$CONFIG_COMMON_CONFLICT"
        ) >"$LOG_ROOT/common_conflict.log" 2>&1 &
        conflict_pid=$!
        sleep 1

        if kill -0 "$conflict_pid" 2>/dev/null; then
            fail "Second Webserv kept running although one configured port was already occupied"
            kill -INT "$conflict_pid" 2>/dev/null || true
            sleep 1
            kill -KILL "$conflict_pid" 2>/dev/null || true
            wait "$conflict_pid" 2>/dev/null || true
        else
            wait "$conflict_pid" 2>/dev/null || true
            pass "Second Webserv rejected the configuration containing an occupied port"
        fi

        if port_is_free "$PORT_UNIQUE"; then
            pass "The rejected second configuration did not partially keep its free port open"
        else
            fail "The second Webserv partially started on $PORT_UNIQUE despite the occupied common port"
        fi

        if kill -0 "$owner_pid" 2>/dev/null; then
            owner_body="$(curl -sS --noproxy '*' --max-time 5 \
                --resolve "owner.test:$PORT_COMMON:$HOST" \
                "http://owner.test:$PORT_COMMON/" 2>/dev/null || true)"
            if printf '%s' "$owner_body" | grep -F 'HOST_ALPHA_OK' >/dev/null 2>&1; then
                pass "The first Webserv remained functional after the bind conflict"
            else
                fail "The first Webserv stayed alive but stopped serving the expected site"
            fi
        else
            fail "The first Webserv terminated when the second process hit a common-port conflict"
        fi
    fi

    # 后续 Siege 与 bonus 仍使用主评测站点，因此无条件恢复主服务器。
    if start_server "$CONFIG_MAIN" "$LOG_ROOT/main_server_restart.log" "$PORT"; then
        pass "Main evaluation server restarted after port tests"
    fi
}

fd_count()
{
    pid="$1"
    if [ -d "/proc/$pid/fd" ]; then
        find "/proc/$pid/fd" -mindepth 1 -maxdepth 1 2>/dev/null | wc -l | tr -d ' '
        return
    fi
    if command -v lsof >/dev/null 2>&1; then
        lsof -p "$pid" 2>/dev/null | wc -l | tr -d ' '
        return
    fi
    echo "-1"
}

rss_kb()
{
    ps -o rss= -p "$1" 2>/dev/null | tr -d ' '
}

# 同时兼容 Siege 传统文本输出和新版 JSON 输出。
# 参数 1：Siege 日志路径。
# 参数 2：字段名，例如 availability、failed_transactions。
# 找不到或字段值不是数字时输出空字符串。
parse_siege_metric()
{
    log_file="$1"
    metric_name="$2"

    python3 - "$log_file" "$metric_name" <<'PY'
import json
import re
import sys

log_path = sys.argv[1]
metric = sys.argv[2]

try:
    with open(log_path, "r", encoding="utf-8", errors="replace") as handle:
        content = handle.read()
except OSError:
    raise SystemExit(0)

# 新版 Siege 通常输出一个 JSON 对象。
try:
    parsed = json.loads(content)
except (TypeError, ValueError):
    parsed = None

if isinstance(parsed, dict) and metric in parsed:
    value = parsed[metric]
    if isinstance(value, (int, float)):
        print(value)
        raise SystemExit(0)

# 某些版本可能在 JSON 前后附加额外提示，因此再做一次字段匹配。
json_pattern = r'["\']' + re.escape(metric) + r'["\']\s*:\s*([0-9]+(?:\.[0-9]+)?)'
match = re.search(json_pattern, content, re.IGNORECASE)
if match:
    print(match.group(1))
    raise SystemExit(0)

# 兼容传统 Siege 文本，例如：Availability: 100.00 %
legacy_names = {
    "availability": "Availability",
    "failed_transactions": "Failed transactions",
    "successful_transactions": "Successful transactions",
    "transactions": "Transactions",
}
legacy_name = legacy_names.get(metric)
if legacy_name:
    legacy_pattern = re.escape(legacy_name) + r'\s*:\s*([0-9]+(?:\.[0-9]+)?)'
    match = re.search(legacy_pattern, content, re.IGNORECASE)
    if match:
        print(match.group(1))
PY
}

connection_state_count()
{
    pid="$1"
    state="$2"
    port="$3"

    if command -v ss >/dev/null 2>&1; then
        ss -tanH 2>/dev/null \
            | awk -v wanted="$state" -v suffix=":$port" '
                {
                    current = $1
                    gsub(/-/, "_", current)
                    if (current == wanted && ($4 ~ suffix "$" || $5 ~ suffix "$"))
                        count++
                }
                END {print count + 0}
            '
        return
    fi

    if command -v lsof >/dev/null 2>&1; then
        lsof -nP -a -p "$pid" -iTCP 2>/dev/null \
            | awk -v wanted="$state" '
                NR > 1 {
                    current = $NF
                    gsub(/[()]/, "", current)
                    gsub(/-/, "_", current)
                    if (current == wanted)
                        count++
                }
                END {print count + 0}
            '
        return
    fi

    echo "-1"
}

run_stress_tests()
{
    if [ "$RUN_STRESS" -eq 0 ]; then
        skip "Stress test was not requested. Use --stress."
        return
    fi

    section "Siege and stress"

    if ! server_alive; then
        fail "Server is not running before stress test"
        return
    fi

    stress_headers="$TMP_ROOT/stress_headers"
    stress_body="$TMP_ROOT/stress_body"
    stress_status="$(http_request GET "$BASE/empty.html" "$stress_headers" "$stress_body")"
    assert_status "$stress_status" 200 "Empty-page precheck before Siege"
    stress_body_size="$(wc -c < "$stress_body" | tr -d ' ')"
    if [ "$stress_body_size" = "0" ]; then
        pass "Siege target is an empty response body"
    else
        fail "Siege target must be empty, got $stress_body_size bytes"
    fi

    baseline_fd="$(fd_count "$SERVER_PID")"
    baseline_rss="$(rss_kb "$SERVER_PID")"

    if command -v siege >/dev/null 2>&1; then
        if siege -b -c 20 -r 50 "$BASE/empty.html" \
            >"$LOG_ROOT/siege.log" 2>&1; then
            availability="$(parse_siege_metric "$LOG_ROOT/siege.log" "availability")"
            failed_transactions="$(parse_siege_metric "$LOG_ROOT/siege.log" "failed_transactions")"
            successful_transactions="$(parse_siege_metric "$LOG_ROOT/siege.log" "successful_transactions")"

            if [ -z "$availability" ]; then
                fail "Could not parse Siege availability. See $LOG_ROOT/siege.log"
            elif python3 - "$availability" <<'PY'
import sys
try:
    value = float(sys.argv[1])
except ValueError:
    raise SystemExit(1)
raise SystemExit(0 if value >= 99.5 else 1)
PY
            then
                details="availability ${availability}%"
                if [ -n "$successful_transactions" ]; then
                    details="$details, successful $successful_transactions"
                fi
                if [ -n "$failed_transactions" ]; then
                    details="$details, failed $failed_transactions"
                fi
                pass "Siege $details (required availability >= 99.5%)"
                if [ -n "$failed_transactions" ] && [ "$failed_transactions" != "0" ]; then
                    warn "Siege reported $failed_transactions failed transactions despite acceptable availability"
                fi
            else
                fail "Siege availability is ${availability}%, below 99.5%"
            fi
        else
            fail "Siege command failed. See $LOG_ROOT/siege.log"
        fi
    else
        fail "Siege is not installed; the new EvalHub mandatory stress check cannot be completed."
        warn "Running a Python fallback for diagnostics only; it does not replace Siege."
        python3 - "$HOST" "$PORT" >"$LOG_ROOT/python_stress.log" <<'PY'
import concurrent.futures
import http.client
import sys
host = sys.argv[1]
port = int(sys.argv[2])

def one(_):
    try:
        conn = http.client.HTTPConnection(host, port, timeout=5)
        conn.request("GET", "/empty.html", headers={"Connection": "close"})
        response = conn.getresponse()
        response.read()
        conn.close()
        return response.status == 200
    except Exception:
        return False

count = 1000
with concurrent.futures.ThreadPoolExecutor(max_workers=30) as pool:
    results = list(pool.map(one, range(count)))
success = sum(results)
print(f"success={success} total={count} availability={success * 100.0 / count:.2f}")
raise SystemExit(0 if success * 100.0 / count >= 99.5 else 1)
PY
        if [ "$?" -eq 0 ]; then
            pass "Python fallback stress availability >= 99.5%"
        else
            fail "Python fallback stress availability < 99.5%"
        fi
    fi

    sleep 2
    after_fd="$(fd_count "$SERVER_PID")"
    after_rss="$(rss_kb "$SERVER_PID")"

    if [ "$baseline_fd" != "-1" ] && [ "$after_fd" != "-1" ]; then
        if [ "$after_fd" -le $((baseline_fd + 5)) ]; then
            pass "File descriptor count returned near baseline ($baseline_fd -> $after_fd)"
        else
            warn "File descriptor count stayed elevated ($baseline_fd -> $after_fd); inspect manually."
        fi
    else
        skip "FD count is unavailable on this system."
    fi

    if [ -n "$baseline_rss" ] && [ -n "$after_rss" ]; then
        rss_growth=$((after_rss - baseline_rss))
        if [ "$rss_growth" -le 10240 ]; then
            pass "RSS did not grow by more than 10 MiB ($baseline_rss KiB -> $after_rss KiB)"
        else
            warn "RSS grew by more than 10 MiB ($baseline_rss KiB -> $after_rss KiB); repeat and inspect for leaks."
        fi
    else
        skip "RSS measurement is unavailable."
    fi

    close_wait_count="$(connection_state_count "$SERVER_PID" "CLOSE_WAIT" "$PORT")"
    established_count="$(connection_state_count "$SERVER_PID" "ESTAB" "$PORT")"
    if [ "$close_wait_count" = "-1" ]; then
        skip "TCP connection-state inspection is unavailable on this system."
    elif [ "$close_wait_count" = "0" ]; then
        pass "No CLOSE-WAIT connection remained after Siege"
    else
        fail "$close_wait_count CLOSE-WAIT connections remained after Siege"
    fi

    if [ "$established_count" != "-1" ]; then
        if [ "$established_count" -le 5 ]; then
            pass "Established connections returned near baseline after Siege ($established_count)"
        else
            warn "$established_count established connections remained after Siege; inspect keep-alive cleanup manually"
        fi
    fi

    if server_alive; then
        status="$(curl -sS --max-time 5 -o "$TMP_ROOT/after_stress" -w '%{http_code}' "$BASE/hello.txt" 2>/dev/null)"
        assert_status "$status" 200 "Server remains available after stress"
    else
        fail "Server terminated during stress testing"
    fi

    manual "Run a longer evaluator-chosen Siege test such as siege -b -c 50 -t 5M and watch RSS, FD count and CLOSE-WAIT connections."
}

run_bonus_tests()
{
    if [ "$RUN_BONUS" -eq 0 ]; then
        skip "Bonus tests were not requested. Re-run with --bonus after mandatory checks pass."
        return
    fi

    if [ "$MODE" != "full" ] || [ "$RUN_STRESS" -ne 1 ]; then
        skip "Bonus tests require the complete mandatory automatic suite; use --full --bonus."
        return
    fi

    if [ "$MANDATORY_FAIL_COUNT" -ne 0 ]; then
        skip "Bonus tests were ignored because mandatory automatic checks have failures."
        return
    fi

    CURRENT_SCOPE="bonus"
    if ! server_alive; then
        if ! start_server "$CONFIG_MAIN" "$LOG_ROOT/main_server_bonus_restart.log" "$PORT"; then
            fail "Bonus tests could not start because the main evaluation server is unavailable."
            CURRENT_SCOPE="mandatory"
            return
        fi
    fi

    section "Bonus prerequisites"
    manual "Count bonus points only after every mandatory automatic and manual item has been confirmed as entirely correct."
    run_session_tests
    run_bonus_cgi_tests
    CURRENT_SCOPE="mandatory"
}

print_summary()
{
    section "Summary"
    echo "PASS             : $PASS_COUNT"
    echo "FAIL             : $FAIL_COUNT"
    echo "WARN             : $WARN_COUNT"
    echo "SKIP             : $SKIP_COUNT"
    echo "MANUAL           : $MANUAL_COUNT"
    echo "MANDATORY FAIL   : $MANDATORY_FAIL_COUNT"
    echo "SUPPLEMENTAL PASS: $SUPPLEMENTAL_PASS_COUNT"
    echo "SUPPLEMENTAL FAIL: $SUPPLEMENTAL_FAIL_COUNT"
    if [ "$RUN_BONUS" -eq 1 ]; then
        echo "BONUS PASS       : $BONUS_PASS_COUNT"
        echo "BONUS FAIL       : $BONUS_FAIL_COUNT"
    fi
    echo
    echo "Logs: $LOG_ROOT"

    if [ "$MODE" != "full" ] || [ "$RUN_STRESS" -ne 1 ]; then
        printf '%sAUTOMATIC MANDATORY RESULT: INCOMPLETE%s\n' "$YELLOW" "$RESET"
        echo "Quick mode intentionally skipped mandatory EvalHub sections; run --full for a grade-oriented result."
        return 1
    fi

    if [ "$MANDATORY_FAIL_COUNT" -eq 0 ]; then
        printf '%sAUTOMATIC MANDATORY RESULT: PASS%s\n' "$GREEN" "$RESET"
        echo "Manual source review, browser checks, official tester and leak verification are still required."
        if [ "$RUN_BONUS" -eq 1 ]; then
            if [ "$BONUS_FAIL_COUNT" -eq 0 ]; then
                printf '%sAUTOMATIC BONUS CHECKS: PASS%s\n' "$GREEN" "$RESET"
            else
                printf '%sAUTOMATIC BONUS CHECKS: FAIL%s\n' "$RED" "$RESET"
                echo "Bonus failures do not change the mandatory automatic result."
            fi
        fi
        return 0
    fi

    printf '%sAUTOMATIC MANDATORY RESULT: FAIL%s\n' "$RED" "$RESET"
    echo "Fix every mandatory [FAIL], rerun the same command, then complete MANUAL_CHECKLIST.md."
    return 1
}

main()
{
    section "Preflight"
    echo "Project root : $PROJECT_ROOT"
    echo "Mode         : $MODE"
    echo "Main URL     : $BASE"
    echo "Stress       : $RUN_STRESS"
    echo "Bonus        : $RUN_BONUS"

    dependencies_ok=1
    require_command bash || dependencies_ok=0
    require_command make || dependencies_ok=0
    require_command curl || dependencies_ok=0
    require_command python3 || dependencies_ok=0
    require_command grep || dependencies_ok=0
    require_command awk || dependencies_ok=0

    if [ "$dependencies_ok" -ne 1 ]; then
        print_summary
        exit 1
    fi

    prepare_runtime

    if ! run_build_checks; then
        print_summary
        exit 1
    fi

    if ! start_server "$CONFIG_MAIN" "$LOG_ROOT/main_server.log" "$PORT"; then
        print_summary
        exit 1
    fi

    run_core_http_tests
    run_upload_tests
    run_raw_request_tests
    run_cgi_tests
    run_multi_port_tests
    run_stress_tests

    manual "Before grading, verify the repository ownership, clone into an empty folder, inspect git status and check for malicious aliases or wrappers."
    manual "Review together every helper/testing script used during grading before relying on its result."
    manual "Launch/install Siege as requested by the evaluation environment; the Python fallback is diagnostic only."
    manual "Repeat representative GET and UNKNOWN requests with telnet as requested by the sheet, not only curl/raw_http.py."
    manual "Look up the HTTP response status-code list and recheck every status observed during the defence."
    manual "Use the team's reference browser: inspect request/response headers, serve a complete static site, try a wrong URL, directory listing and redirect."
    manual "Run Valgrind, leaks or another accepted tool and verify all heap allocations are freed; definite and indirect leaks must be zero."
    manual "Run the official school tester and the supplied CGI testers separately with their dedicated configurations."

    run_bonus_tests

    if print_summary; then
        exit 0
    fi
    exit 1
}

main "$@"