#!/usr/bin/env python3
import sys

# 故意抛出未捕获异常并以非 0 状态码退出
sys.stderr.write("Fatal error in CGI script!\n")
sys.exit(1)