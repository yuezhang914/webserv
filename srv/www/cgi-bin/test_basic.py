#!/usr/bin/env python3
import os
import sys

print("Status: 200 OK\r")
print("Content-Type: text/html\r")
print("\r")

print("<h1>CGI Success</h1>")
print(f"<p>REQUEST_METHOD: {os.environ.get('REQUEST_METHOD')}</p>")
print(f"<p>QUERY_STRING: {os.environ.get('QUERY_STRING')}</p>")

# 如果是 POST，读取 stdin
if os.environ.get('REQUEST_METHOD') == 'POST':
    content_length = int(os.environ.get('CONTENT_LENGTH', 0))
    body = sys.stdin.read(content_length)
    print(f"<p>POST Body Received ({len(body)} bytes): {body}</p>")