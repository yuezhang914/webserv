#!/usr/bin/env python3
import time

# 故意挂起 30 秒或死循环
time.sleep(30)

print("Content-Type: text/html\r\n\r\n")
print("I survived timeout!")