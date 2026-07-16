#!/usr/bin/python3
import os
import sys

# 1. 物理读取大管家帮我们塞进来的环境变量
method = os.environ.get("REQUEST_METHOD", "GET")
query_string = os.environ.get("QUERY_STRING", "")

# 2. 区分物理对流：如果是 POST，数据在标准输入里，需要手动读取
body = ""
if method == "POST":
    # 物理读取父进程 ::write 进来的每一滴数据
    body = sys.stdin.read()

# 3. 智能包装输出（故意不带 \r\n\r\n 头部，测试大管家的“纯肉流派”智能兜底包装能力）
print("<html>")
print("<head><title>CGI Test Result</title></head>")
print("<body>")
print(f"<h1>Hello 未央学姐! CGI Response Success!</h1>")
print(f"<p><b>Request Method:</b> {method}</p>")
print(f"<p><b>Query String (GET Params):</b> {query_string}</p>")
print(f"<p><b>Body Recieved (POST Data):</b> {body}</p>")
print("</body>")
print("</html>")