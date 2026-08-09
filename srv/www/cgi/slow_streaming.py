#!/usr/bin/env python3
import time
import sys

# 必须先输出 Header，以便服务器能够开始流式响应（如果你的 Webserv 支持 chunked/stream）
print("Content-Type: text/plain\r\n\r\n", end="")
sys.stdout.flush()

for i in range(1, 8):
    print(f"Chunk {i}: Keep-alive activity at {time.strftime('%H:%M:%S')}\n", end="")
    sys.stdout.flush()  # 必须强制 flush 刷新 stdout 缓冲区
    time.sleep(2)       # 每次只停顿 2 秒，远小于 10 秒超时门槛

print("Stream finished successfully!")
sys.stdout.flush()