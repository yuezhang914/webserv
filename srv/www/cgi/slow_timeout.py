#!/usr/bin/env python3
import time
import sys

# 连续 11 秒没有任何 stdout 进展
time.sleep(15)

print("Content-Type: text/plain\r\n\r\n")
print("This should fail with 504 Gateway Timeout")
sys.stdout.flush()