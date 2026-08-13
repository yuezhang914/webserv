#!/usr/bin/env python3
import os
import sys

body = sys.stdin.read()

print("Content-Type: text/plain")
print()
print("METHOD=" + os.environ.get("REQUEST_METHOD", ""))
print("QUERY=" + os.environ.get("QUERY_STRING", ""))
print("BODY=" + body)

try:
    with open("relative.txt", "r") as f:
        print("RELATIVE=" + f.read().strip())
except Exception as e:
    print("RELATIVE_ERROR=" + str(e))
