#!/usr/bin/python3
import os
import sys

print("Content-Type: text/html")
print("") # 黄金空行
print("<html><body>")
print("<h1>🔥 恭喜学姐！根目录 CGI 动态破壁成功！</h1>")
print(f"<p><b>当前执行的脚本路径:</b> {os.path.abspath(__file__)}</p>")
print(f"<p><b>请求方法:</b> {os.environ.get('REQUEST_METHOD', 'GET')}</p>")
print(f"<p><b>GET 参数:</b> {os.environ.get('QUERY_STRING', '无')}</p>")
print("</body></html>")