# Webserv 自动评测脚本使用说明

这套文件把能黑盒自动化的评测项放进一个入口：

```bash
bash tests/evaluation/run_eval.sh --full
```

脚本不会修改 `srcs/` 或 `includes/`。它只会：

1. 在 `tests/evaluation/.runtime/` 创建临时网站、CGI、错误页和配置；
2. 重新编译项目并运行现有模块测试；
3. 自动启动、停止 Webserv；
4. 测试配置、GET/POST/DELETE、上传、body limit、错误状态、原始 HTTP、CGI、Session、多端口；
5. 输出 `[PASS]`、`[FAIL]`、`[WARN]`、`[SKIP]` 和 `[MANUAL]`；
6. 默认结束时删除临时目录。

## 1. 安装

把压缩包中的 `tests/evaluation/` 整个复制到项目根目录：

```text
webserv/
├── Makefile
├── includes/
├── srcs/
└── tests/
    └── evaluation/
        ├── run_eval.sh
        ├── raw_http.py
        ├── shared_port_check.py
        ├── README.md
        └── MANUAL_CHECKLIST.md
```

赋予执行权限：

```bash
chmod +x tests/evaluation/run_eval.sh
chmod +x tests/evaluation/raw_http.py
chmod +x tests/evaluation/shared_port_check.py
```

也可以始终通过 `bash` 运行主脚本，不依赖执行权限：

```bash
bash tests/evaluation/run_eval.sh --full
```

## 2. 推荐运行顺序

### 第一次：快速检查

```bash
bash tests/evaluation/run_eval.sh --quick --keep
```

跳过：

- 慢 CGI 的 10 秒超时；
- 多端口和同端口策略；
- Siege。

保留 `.runtime/`，便于查看日志。

### 第二次：完整自动测试

```bash
bash tests/evaluation/run_eval.sh --full --keep
```

这是正式修复期间最常用的命令。

### 第三次：压力测试

```bash
bash tests/evaluation/run_eval.sh --stress --keep
```

如果安装了 Siege，脚本会要求：

```text
Availability >= 99.5%
```

没有 Siege 时会运行 Python 并发测试，但这不能替代正式评测要求的 Siege。

## 3. HEAD 预期如何设置

你的最新设计选择是：服务器没有实现 HEAD，因此：

```text
HEAD -> 501 Not Implemented
并且不能发送 response body
```

默认就是：

```bash
bash tests/evaluation/run_eval.sh --full --head-status 501
```

只有你明确决定把 HEAD 当成 route 不允许的方法时，才使用：

```bash
bash tests/evaluation/run_eval.sh --full --head-status 405
```

这只改变脚本的预期，不会修改项目代码。

## 4. 如何判断通过

脚本最后会输出：

```text
PASS   : ...
FAIL   : ...
WARN   : ...
SKIP   : ...
MANUAL : ...
AUTOMATIC RESULT: PASS/FAIL
```

### 自动测试通过

必须满足：

```text
FAIL : 0
AUTOMATIC RESULT: PASS
```

脚本退出码也会是 0：

```bash
echo $?
```

应得到：

```text
0
```

### 自动测试失败

只要有一个 `[FAIL]`：

```text
AUTOMATIC RESULT: FAIL
```

退出码为 1。使用 `--keep` 后查看：

```bash
ls tests/evaluation/.runtime/logs
```

主要日志：

```text
make.log
make_second.log
run_*_test_annotated.sh.log
main_server.log
multi_server.log
shared_server.log
siege.log
```

## 5. WARN、SKIP、MANUAL 怎么理解

- `[WARN]`：暂不能证明是错误，但需要继续检查。例如 RSS 压测后增长较多。
- `[SKIP]`：工具缺失或当前模式主动跳过。
- `[MANUAL]`：脚本无法可靠判断，必须照 `MANUAL_CHECKLIST.md` 手动完成。

即使自动结果是 PASS，也不代表正式评测必过。以下不能只靠脚本：

- 单一 `poll()` 的真实控制流；
- 每个 socket/pipe I/O 是否严格由 readiness 驱动；
- partial send 与断开清理是否完全正确；
- 浏览器 Network 展示；
- Valgrind/leaks；
- 无限 Siege 的长期稳定性；
- 现场源码解释和临时小修改。

## 6. 常用参数

```bash
# 不重新编译
bash tests/evaluation/run_eval.sh --full --no-build

# 不运行模块测试
bash tests/evaluation/run_eval.sh --full --skip-modules

# 保留所有临时文件和日志
bash tests/evaluation/run_eval.sh --full --keep

# 改用其他端口，避免本机 18080 被占用
EVAL_PORT=19080 bash tests/evaluation/run_eval.sh --full --keep
```

## 7. 端口被占用

出现：

```text
[FAIL] Port 18080 is already in use before server startup.
```

查找进程：

```bash
lsof -nP -iTCP:18080 -sTCP:LISTEN
```

或者换端口：

```bash
EVAL_PORT=19080 bash tests/evaluation/run_eval.sh --full
```

## 8. 清理

默认自动清理。使用 `--keep` 后手动删除：

```bash
rm -rf tests/evaluation/.runtime
```
