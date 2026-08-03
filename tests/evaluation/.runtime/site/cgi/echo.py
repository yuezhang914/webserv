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
