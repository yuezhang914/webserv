# 学校tester

make

./webserv config/school\_tester.conf



另一个窗口

./tester\_data/tester http\://localhost:8080





# cgi 

### 终端 1

进入项目：cd 你的webserv项目

确认文件：

ls tester\_data/cgi\_tester

ls tester\_data/YoupiBanane/youpi.bla

给权限：

chmod +x tester\_data/cgi\_tester

确认类型：

file tester\_data/cgi\_tester

然后：make

启动：./webserv config/school\_tester.conf

**这个终端不动。**

---

### 终端 2

先普通静态文件：

curl -i http\://127.0.0.1:8080/youpi.bad\_extension

这应该是普通静态响应。

然后 CGI：

curl -i http\://127.0.0.1:8080/youpi.bla

然后带 query：

curl -i 'http\://127.0.0.1:8080/youpi.bla?name=Alice'

然后 POST：

curl -i \\

 -X POST \\

 -H "Content-Type: text/plain" \\

 --data-binary "HELLO\_CGI" \\

 http\://127.0.0.1:8080/youpi.bla

最后 POST + query：

curl -i \\

 -X POST \\

 -H "Content-Type: text/plain" \\

 --data-binary "BODY\_TEST" \\

 'http\://127.0.0.1:8080/youpi.bla?foo=bar'

---

## 你看结果时按这个表判断

| **结果**               | **意义**                        |
| -------------------- | ----------------------------- |
| connection refused   | Webserv 没启动/端口不对              |
| 404                  | Webserv 没找到 `.bla` target     |
| `.bla` 内容被原样返回       | CGI route 没识别成功               |
| 500 + PATH\_INFO ... | CGI 已执行，但 CGI environment 有问题 |
| 502                  | Webserv 认为 CGI 输出/执行失败        |
| 504                  | CGI 超时                        |
| 正常 CGI 输出            | CGI 执行、env、pipe、response 基本正确 |
| Webserv crash        | 严重错误                          |



# eval cmds 手动测试



---

 一、准备：两个终端

后面默认：

终端 1 = 启动 webserv

终端 2 = curl / nc / siege 测试

都先：

cd 你的项目根目录

---

二、Mandatory 手动测试

 1. 编译 + Makefile｜2 分钟

终端 1：

make fclean

make

正确：

无 error

无 warning

生成 ./webserv

再执行：

make

正确：

Nothing to be done for 'all'

也就是第二次不能重新链接。

再确认 C++98：

grep -n "CXXFLAGS" Makefile

应该能看到：

-Wall -Wextra -Werror -std=c++98

---

## 2. 单一 poll / non-blocking｜3 分钟

这部分主要是**给 evaluator 看源码证明**，不是靠 HTTP 请求证明。

### 2.1 只有一个 poll

grep -RInE '\bpoll[[:space:]]\*\\(' srcs includes

正确：

真正调用 `poll()` 的地方只有 **1 个**。

Subject 对这一项非常严格：socket 和 CGI pipe 都必须由同一个 poll/equivalent 驱动，而且同时监控读和写。

 webserv.pdf

### 2.2 看 non-blocking

grep -RIn "O\_NONBLOCK" srcs

正确：

listener、client socket、CGI pipe 的相关代码能看到 `O_NONBLOCK`。

### 2.3 fork 只能用于 CGI

grep -RInE '\bfork[[:space:]]\*\\(' srcs

正确：

`fork()` 只出现在 CGI 实现中。

### 2.4 evaluator 问 read/write 时怎么解释

直接打开：

srcs/Server/ServerManager.cpp

srcs/Server/ServerManagerUtils.cpp

srcs/Cgi/CgiManager.cpp

你们只要能指出：

poll

 ├─ server fd readable → accept

 ├─ client fd readable → recv

 ├─ client fd writable → send

 ├─ CGI stdout readable → read

 └─ CGI stdin writable → write

并说明：

socket/pipe 不会绕过 poll 直接 I/O；regular disk file 是 Subject 明确允许的例外。

 webserv.pdf

---

# 3. 启动正式展示服务器｜1 分钟

终端 1：

./webserv default.conf

不要关。

你这个 `default.conf` 一次会监听：

8080

8081

8083

后面大部分测试都不需要重新启动。

---

# 4. 静态网站 + 浏览器｜3 分钟

Chrome/Safari 打开：

http\://127.0.0.1:8080/

正确：

出现 **Webserv Test Panel**，CSS 正常，页面完整。

打开：

开发者工具 → Network

刷新页面。

至少看到：

index.html     200

style.css      200

终端 2 再简单验证：

curl -i http\://127.0.0.1:8080/style.css

正确：

HTTP/... 200

Content-Type: text/css...

这证明完整 static website + browser compatibility。Subject 明确要求能服务完整静态网站和标准浏览器。

 webserv.pdf

---

# 5. GET + 404 + 自定义错误页｜2 分钟

### 正常 GET

curl -i http\://127.0.0.1:8080/index2.html

正确：

HTTP/... 200

body 有：

Webserv Test Panel

### 不存在文件

curl -i http\://127.0.0.1:8080/this-file-does-not-exist

正确：

HTTP/... 404

而且 body 是你们：

srv/www/customErrorPages/404.html

能看到类似：

404 — Not Found

这同时证明：

正确状态码

\+

配置的 error\_page

---

# 6. 方法权限 405 + UNKNOWN 501｜2 分钟

### 路由只允许 GET

curl -i -X POST -H "Content-Type: text/plain" --data "hello" http\://127.0.0.1:8080/index.html

正确：

HTTP/... 405

Allow: GET

### 未实现方法

curl -i -X BREW http\://127.0.0.1:8080/index.html

正确：

HTTP/... 501

然后马上：

curl -I http\://127.0.0.1:8080/



`-I` 的意思是：

curl 发送 HEAD request

也就是实际上请求大概是：

HEAD / HTTP/1.1

你们现在 `/` 配置：

allow\_methods GET;

所以 HEAD 不被允许，于是：

405 Method Not Allowed

Allow: GET



---

# 7. POST 上传 + GET 回读 + DELETE｜4 分钟

这组一次证明：

POST

upload

GET

DELETE

先清理旧测试文件：

rm -f srv/www/upload/manual\_eval.txt srv/www/upload/manual\_eval\_\*.txt

创建测试文件：

printf 'MANUAL\_UPLOAD\_OK\n' > /tmp/manual\_eval.txt



`/tmp` 是**系统根目录下的临时目录**，不是你项目里的 `tmp` 文件夹

直接这样看：ls /tmp

或者更精准：ls -l /tmp/manual\_eval.txt



### POST

curl -i -X POST -H "Content-Type: text/plain" --data-binary @/tmp/manual\_eval.txt http\://127.0.0.1:8080/upload/manual\_eval.txt

正确：

200 或 201



pwd 进入项目根目录

查看磁盘：

cat srv/www/upload/manual\_eval.txt

正确：

MANUAL\_UPLOAD\_OK

### GET 上传文件

curl http\://127.0.0.1:8080/upload/manual\_eval.txt

正确：

MANUAL\_UPLOAD\_OK

### DELETE

curl -i -X DELETE http\://127.0.0.1:8080/upload/manual\_eval.txt

正确：

200 或 204

检查磁盘：

ls srv/www/upload/manual\_eval.txt

正确：

No such file or directory

再：

curl -i http\://127.0.0.1:8080/upload/manual\_eval.txt

正确：

404

Subject 明确要求至少 GET、POST、DELETE，同时客户端必须可以上传文件。

 webserv.pdf

---

# 8. max\_body\_size｜2 分钟

`default.conf` 的 `/upload`：

max\_body\_size 2M;

生成 3 MB：

dd if=/dev/zero of=/tmp/too\_big.bin bs=1M count=3

POST：

curl -i -X POST -H "Content-Type: application/octet-stream" --data-binary @/tmp/too\_big.bin http\://127.0.0.1:8080/upload/too\_big.bin

正确：

413 Payload Too Large

而且：

ls srv/www/upload/too\_big.bin

应该：

No such file or directory

证明 body limit **不仅解析了，而且真正生效**。

---

# 9. Autoindex｜1 分钟

`default.conf` 的：

/upload

配置了：

autoindex on;

执行：

curl http\://127.0.0.1:8080/upload/

正确：

返回目录页面，里面能看到已有的：

papillon.avif


---

# 10. Redirect｜1 分钟

内部 redirect：

curl -i http\://127.0.0.1:8080/redirect\_int/anything

正确：

302

Location: /index2.html

跟随 redirect：

curl -i -L http\://127.0.0.1:8080/redirect\_int/anything

最终：

200


这证明 location redirect 配置。Subject 明确要求 route 可以配置 HTTP redirection。

 webserv.pdf

---

# 11. Multiple ports｜2 分钟

**不用换配置。**

你当前 `default.conf` 已经同时启动三个 server。

### 8080

curl -s http\://127.0.0.1:8080/ | grep "Webserv Test Panel"

应该找到：

Webserv Test Panel

### 8081

curl http\://127.0.0.1:8081/hello.txt

正确：

HELLO\_WEBSERV

### 8083

curl http\://127.0.0.1:8083/

正确：

SITE\_B\_OK

所以：

同一个 webserv process

8080 → 网站 A

8081 → 网站 B

8083 → 网站 C

这正好证明 Mandatory 的：

listen to multiple ports to deliver different content。

**不用浪费时间做同端口 virtual host。新版 Subject 明确说 virtual host out of scope。** 

---

# 12. Python CGI GET + query｜2 分钟

仍然用 `default.conf`。

curl -i 'http\://127.0.0.1:8080/cgi/echo.py?name=Alice&value=42'

正确：

200

body 应看到：

CGI Execution Success

Request Method: GET

Query String: name=Alice&value=42

证明：

CGI 被真正执行

REQUEST\_METHOD 正确

QUERY\_STRING 正确

---

# 13. CGI POST + body｜2 分钟

curl -i -X POST -H "Content-Type: text/plain" --data-binary "HELLO\_CGI\_BODY" 'http\://127.0.0.1:8080/cgi/echo.py?source=manual'

正确：

200

body 中看到：

Request Method: POST

Query String: source=manual

HELLO\_CGI\_BODY

证明 CGI 收到了完整 POST body。

CGI 的完整 request、arguments、environment 必须能传给 CGI。

 webserv.pdf

---

# 14. CGI crash / 错误恢复｜1 分钟

curl -i http\://127.0.0.1:8080/cgi/bad.py

正确：

500 或 502

然后立即：

curl -i http\://127.0.0.1:8080/index.html

必须：

200

也就是：

一个 CGI 崩了，整个 Webserv 不能崩。

---

# 15. CGI timeout + 真正证明 non-blocking｜3 分钟

这是现场**非常值得演示**的一项。

终端 2：

curl -i http\://127.0.0.1:8080/cgi/slow\_timeout.py

它会卡着等待 CGI。

**不要等它结束。**

立刻开终端 3：

time curl -i http\://127.0.0.1:8080/index.html

正确：

马上得到 200

而第一个 slow CGI 最后应该得到：

504 Gateway Timeout

而不是整个服务器一起卡 15 秒。

这一个实验非常直观地证明：

slow CGI

       │

       ├── 没阻塞 Webserv

       └── 超时后被终止 → 504

Subject 明确要求 request 不得无限 hang，并且服务器始终 non-blocking。

 webserv.pdf

---

# 16. 切换到

# config/eval.conf

# ：测剩下几个配置特性｜4 分钟

终端 1：

Ctrl+C

重新：

./webserv config/eval.conf

---

### 16.1 Default index

curl http\://127.0.0.1:8080/default/

正确：

DEFAULT\_HOME\_OK

实际文件：

eval\_site\_a/default/home.html

证明 location 的：

index home.html;

生效。

---

### 16.2 Autoindex ON

curl http\://127.0.0.1:8080/list/

正确能看到：

a.txt

b.txt

实际目录已经存在：

eval\_site\_a/list/a.txt

eval\_site\_a/list/b.txt

---

### 16.3 Autoindex OFF

curl -i http\://127.0.0.1:8080/no-list/

按你们当前实现/学校测试策略，正确：

404

而且 body **不能泄露**：

secret.txt

---

### 16.4 Alias / 路径映射

curl http\://127.0.0.1:8080/mapped/file.txt

正确：

ROUTE\_ROOT\_OK

真正读取的是：

eval\_mapped/file.txt

证明 URL 路由可以映射到配置指定的文件目录。Subject 要求 route 可以指定实际文件目录。

 webserv.pdf

---

# 17. CGI working directory + EOF｜2 分钟

`config/eval.conf` 的 CGI 非常适合专门测试这一项。

curl -i 'http\://127.0.0.1:8080/cgi/echo.py?name=Alice'

正确：

200

METHOD=GET

QUERY=name=Alice

RELATIVE=RELATIVE\_OK

其中最重要的是：

RELATIVE=RELATIVE\_OK

因为 `echo.py` 内部只是：

open("relative.txt")

真实文件在：

eval\_site\_a/cgi/relative.txt

所以这直接证明：

CGI 确实 `chdir()` 到了正确脚本目录。

Subject 明确点名这一要求。

 webserv.pdf

这个 CGI **本身也没有输出 Content-Length**，还能正常拿到完整 body，所以同时证明：

CGI output 没 Content-Length 时，服务器能用 EOF 判断结束。

 webserv.pdf

---

# 18. Chunked CGI｜2 分钟

这个不要用你们脚本，直接手工造 HTTP。

Mac/Linux 有 `nc` 的话：

printf 'POST /cgi/echo.py HTTP/1.1\r\nHost: localhost\r\nTransfer-Encoding: chunked\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\n4\r\nWiki\r\n5\r\npedia\r\n0\r\n\r\n' | nc 127.0.0.1 8080

正确应该看到：

HTTP/... 200

body：

METHOD=POST

BODY=Wikipedia

关键不是：

4

Wiki

5

pedia

而是 CGI 最终收到：

Wikipedia

这就证明服务器在交给 CGI 前正确 **un-chunk** 了 body，这是 Subject 明确要求。

 webserv.pdf

---

# 19. 没配置自定义错误页时仍有默认错误页｜2 分钟

先暂时另外开一个 server。

终端 3：

./webserv config/first.conf

它监听：

8091

而且没有配置 `error_page`。

测试：

curl -i http\://127.0.0.1:8091/no-such-file

正确：

404

而且应该仍然有服务器生成的错误 body，不能：

空响应

崩溃

连接挂死

这证明：

没有自定义 error\_page 时，Webserv 仍有 default error page。

这是 Mandatory。

 webserv.pdf

关掉这个 8091 server。

---

# 20. Port conflict｜3 分钟

这个是 Eval 时很常见的手测。

### 情况 A：配置自己重复 port

确保 8090 空闲：

./webserv config/duplicate\_port.conf

正确：

Webserv 拒绝配置并退出。

不能继续监听 8090。

Mac：

lsof -nP -iTCP:8090 -sTCP\:LISTEN

正确：

无输出

---

### 情况 B：一个端口被另一个进程占用

终端 1：

./webserv config/first.conf

占用：

8091

终端 2：

./webserv config/second.conf

`second.conf` 想同时要：

8091  ← 已经被占

8092  ← 空闲

正确：

第二个 Webserv **整个启动失败**。

不能出现：

“8091 失败算了，但偷偷继续监听 8092”。

检查：

lsof -nP -iTCP:8092 -sTCP\:LISTEN

正确：

无输出

与此同时第一个 server 还应该活着：

curl http\://127.0.0.1:8091/hello.txt

正确：

HELLO\_WEBSERV

然后关掉它。

---

# 21. Siege 压力测试｜3–5 分钟

重新：

./webserv config/eval.conf

先：

curl http\://127.0.0.1:8080/empty.html

应该 200。

然后：

siege -b -c 20 -r 50 http\://127.0.0.1:8080/empty.html

这就是：

20 并发 × 每人 50 次 = 1000 requests

正确重点：

Availability >= 99.5%

最好：

100.00%

Failed transactions: 0

结束以后：

curl -i http\://127.0.0.1:8080/hello.txt

还必须：

200

HELLO\_WEBSERV

Subject 明确要求 stress test 后服务器仍保持 available。

 webserv.pdf

---

# 三、Bonus 手动测试

Mandatory 没问题以后再做。Subject 规定 Bonus 只有 Mandatory 全部通过才评分；Bonus 就是 **cookies/session + multiple CGI types**。

 webserv.pdf

先切回：

./webserv default.conf

---

# 22. Bonus 1：Cookie + Session｜2 分钟

先删除旧 cookie：

rm -f /tmp/webserv\_cookie.txt

第一次：

curl -i -c /tmp/webserv\_cookie.txt http\://127.0.0.1:8080/session/counter

正确：

200

Set-Cookie: WEBSERV\_SESSION=...

Visits: 1

第二次，把 cookie 发回去：

curl -i -b /tmp/webserv\_cookie.txt -c /tmp/webserv\_cookie.txt http\://127.0.0.1:8080/session/counter

正确：

200

Visits: 2

这就已经非常清楚证明了：

第一次请求

→ server 创建 session

→ Set-Cookie



第二次请求

→ client 带回 cookie

→ server 找到旧 session

→ Visits 1 → 2

Bonus 这一项就证明了。

---

# 23. Bonus 2：两种 CGI｜1 分钟
启动：

./webserv default.conf
### Python

curl -i 'http\://127.0.0.1:8080/cgi/echo.py?type=python'

正确：

200

CGI Execution Success

Query String: type=python

### Shell

curl -i 'http\://127.0.0.1:8080/cgi/echo.sh?type=shell'

正确：

200

Shell CGI Execution Test

REQUEST\_METHOD: GET

QUERY\_STRING: type=shell

也就是同一个 server 根据 extension：

.py → Python

.sh → Shell

满足 **multiple CGI types**。

 webserv.pdf

---

# 四、最后如果还有 3–5 分钟：Leaks

如果学校 Linux：

valgrind --leak-check=full --show-leak-kinds=all --track-fds=yes ./webserv config/eval.conf

另一个终端跑：

curl http\://127.0.0.1:8080/

curl http\://127.0.0.1:8080/cgi/echo.py

curl http\://127.0.0.1:8080/no-such-file

然后服务器：

Ctrl+C

重点：

definitely lost: 0 bytes

indirectly lost: 0 bytes

---
