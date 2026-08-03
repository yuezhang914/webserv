# Webserv 仍需手动完成的评测清单

自动脚本只能验证外部行为和部分静态特征。以下评分点必须人工展示。

## 一、源码与 poll

### 1. 只使用一个核心 I/O multiplexing 系统

运行：

```bash
grep -RInE '\b(poll|select|epoll_wait|kevent)[[:space:]]*\(' srcs includes
```

你需要从源码指出：

```text
主循环
→ poll()
→ listener POLLIN → accept
→ client POLLIN → recv
→ client POLLOUT → send
→ CGI stdout POLLIN → read
→ CGI stdin POLLOUT → write
```

### 2. 检查所有 I/O 调用

```bash
grep -RInE '\b(read|write|recv|send|accept)[[:space:]]*\(' srcs includes
```

逐处确认：

- socket/pipe 只在对应 readiness 出现后操作；
- `recv/read < 0`、`== 0`、`> 0` 都有处理；
- `send/write` 只推进实际返回的字节数；
- 客户端断开或错误后会从 poll、connection map 和 CGI map 清理；
- 普通磁盘文件是 Subject 允许的例外。

### 3. errno

```bash
grep -RIn '\berrno\b' srcs includes
```

必须确认没有在 `read/recv/write/send` 后通过 `errno == EAGAIN/EWOULDBLOCK` 驱动服务器控制流。

### 4. 非阻塞

```bash
grep -RInE 'fcntl|O_NONBLOCK|FD_CLOEXEC' srcs includes
```

确认：

- listener socket；
- accepted client socket；
- CGI parent read pipe；
- CGI parent write pipe；

都符合当前 Subject 的非阻塞要求。

## 二、浏览器

使用团队选择的浏览器：

```text
F12 → Network → Disable cache → 刷新
```

手动展示：

- `/`：完整静态网站；
- 不存在 URL：404；
- autoindex on；
- autoindex off；
- redirect：先 301/302，再 200；
- CSS、JS、图片加载；
- Request headers；
- Response headers；
- 请求不能长期 Pending。

## 三、Valgrind / leaks

Linux：

```bash
valgrind \
  --leak-check=full \
  --show-leak-kinds=all \
  --track-fds=yes \
  --trace-children=no \
  ./webserv tests/evaluation/.runtime/evaluation.conf
```

另一个终端发送：

```bash
curl http://127.0.0.1:18080/hello.txt
curl -X POST -H 'Content-Type: text/plain' --data-binary 'abc' \
  http://127.0.0.1:18080/upload/manual.txt
curl http://127.0.0.1:18080/cgi/echo.py
curl http://127.0.0.1:18080/session/counter
```

然后 `Ctrl+C`。重点目标：

```text
definitely lost: 0 bytes
indirectly lost: 0 bytes
```

同时检查未关闭的 client socket、CGI pipe 和文件 fd。

> 需要先用 `--keep` 生成并保留 evaluation.conf：
>
> ```bash
> bash tests/evaluation/run_eval.sh --quick --keep
> ```

## 四、长期 Siege

自动脚本只跑有限压力。评测前再手动运行：

```bash
siege -b -c 50 -t 5M http://127.0.0.1:18080/empty.html
```

要求：

```text
Availability > 99.5%
服务器不重启
RSS 不无限增长
FD 数量测试后回落
没有持续增加的 CLOSE-WAIT
```

Linux 监控：

```bash
PID=$(pgrep -n webserv)
watch -n 1 "ps -o pid,rss,vsz,%cpu,stat,cmd -p $PID; \
printf 'FD count: '; ls /proc/$PID/fd | wc -l"
```

## 五、学校 tester

学校 tester 的配置和路径约定比较特殊，建议单独运行，不要混入综合脚本：

```bash
./webserv config/school_tester.conf
```

另一个终端：

```bash
cd tester_data
./tester http://127.0.0.1:8080
```

必须在 Linux x86-64 环境运行你上传的 tester 二进制。

## 六、状态码口头解释

准备解释：

```text
200 GET/CGI 成功
201 POST 创建新上传资源
204 DELETE 成功且无 body
400 请求语法错误
403 禁止访问或禁止目录操作
404 资源不存在
405 方法已实现，但当前 route 不允许；带 Allow
411 POST 缺少明确 framing
413 body 超限
415 media type 不支持
500 服务器内部错误
501 服务器未实现该 method
502 CGI 输出无效或上游 CGI 失败
504 CGI 超时
505 HTTP 版本不支持
```

HEAD 当前选择：

```text
未实现 HEAD → 501
但 HEAD 响应本身不发送 body
```

## 七、现场问答和小修改

每位成员都应能解释并快速定位：

- Config 解析与 location 最长匹配；
- Request line、headers、Content-Length、chunked；
- EffectiveRoute 和真实文件路径；
- Response 状态码、Content-Length 和错误页；
- poll 主循环；
- CGI pipes、environment、working directory、timeout、waitpid；
- Cookie 与 SessionStore；
- 上传唯一文件名和 DELETE 安全限制。
