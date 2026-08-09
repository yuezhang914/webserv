#!/usr/bin/env python3
import os
import sys
from urllib.parse import parse_qs

method = os.environ.get("REQUEST_METHOD", "GET")
cookie_header = os.environ.get("HTTP_COOKIE", "")

# 从 HTTP_COOKIE 环境变量中解析 session_id
def get_session_id():
    cookies = cookie_header.split(";")
    for c in cookies:
        if "=" in c:
            key, val = c.strip().split("=", 1)
            if key == "session_id":
                return val
    return None

current_session = get_session_id()

# -------------------------------------------------------------
# 1. POST 请求：处理 Login 或 Logout（会发送 Set-Cookie）
# -------------------------------------------------------------
if method == "POST":
    content_length = int(os.environ.get("CONTENT_LENGTH", 0))
    body = sys.stdin.read(content_length) if content_length > 0 else ""
    parsed_params = parse_qs(body)

    action = parsed_params.get("action", [""])[0]

    # --- Logout 逻辑 ---
    if action == "logout":
        print("Status: 200 OK")
        print("Content-Type: text/html; charset=utf-8")
        # 清除 Cookie（将 Max-Age 设为 0）
        print("Set-Cookie: session_id=; Path=/; Max-Age=0")
        print()
        print("<!DOCTYPE html><html><body><h2>Logged Out</h2><p>Session cleared.</p></body></html>")
        sys.exit(0)

    # --- Login 逻辑 ---
    user = parsed_params.get("user", ["student"])[0]
    new_session_id = f"sess_{user}_123456"

    print("Status: 200 OK")
    print("Content-Type: text/html; charset=utf-8")
    # 只有 Login 时才设置 Set-Cookie！
    print(f"Set-Cookie: session_id={new_session_id}; Path=/")
    print()
    print("<!DOCTYPE html><html><body>")
    print(f"<h2>Login Successful</h2>")
    print(f"<p>Welcome, <strong>{user}</strong>! Session created: <code>{new_session_id}</code></p>")
    print("</body></html>")
    sys.exit(0)

# -------------------------------------------------------------
# 2. GET 请求：检查 Session 状态（绝不发送 Set-Cookie！）
# -------------------------------------------------------------
print("Status: 200 OK")
print("Content-Type: text/html; charset=utf-8")
print()  # 直接结束 Header 区域，不带 Set-Cookie

print("<!DOCTYPE html><html><body>")
if current_session:
    print(f"<h2>Session Active</h2>")
    print(f"<p>Server validated your Cookie! Session ID: <code>{current_session}</code></p>")
else:
    print(f"<h2>No Active Session</h2>")
    print(f"<p>No valid <code>session_id</code> cookie was received in the request headers.</p>")
print("</body></html>")