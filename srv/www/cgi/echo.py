#!/usr/bin/env python3
import os
import sys

# 必须先打印 HTTP Header
print("Content-Type: text/html; charset=utf-8\r\n\r\n")

# 获取 POST Body
body = ""
if os.environ.get("REQUEST_METHOD") == "POST":
    try:
        length = int(os.environ.get("CONTENT_LENGTH", 0))
        body = sys.stdin.read(length)
    except Exception as e:
        body = f"Error reading body: {e}"

print(f"<h1>CGI Execution Success</h1>")
print(f"<h3>Request Method: {os.environ.get('REQUEST_METHOD')}</h3>")
print(f"<h3>Query String: {os.environ.get('QUERY_STRING')}</h3>")
print(f"<h3>POST Body:</h3><pre>{body}</pre>")
print("<h3>Environment Variables:</h3><ul>")
for key, value in os.environ.items():
    print(f"<li><b>{key}</b>: {value}</li>")
print("</ul>")