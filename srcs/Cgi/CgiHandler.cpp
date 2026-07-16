

#include "Webserv.hpp"

CgiHandler::CgiHandler(const Request &request, const std::string &script_path)
    : _request(request), _script_path(script_path)
{
}

CgiHandler::~CgiHandler()
{
}

// 在 CgiHandler.cpp 内部初始化环境变量账本
void CgiHandler::_buildEnv()
{
    // 1. Method ──► 映射为 REQUEST_METHOD
    this->_env["REQUEST_METHOD"] = this->_request.getMethod(); // "GET" 或 "POST"

    // 2. Path (带参数的 URL) ──► 映射为 QUERY_STRING
    // 如果 URL 是 /cgi-bin/test.py?name=wy
    // 那么我们要提取出 "?" 后面的 "name=wy" 塞进 QUERY_STRING 中
    this->_env["QUERY_STRING"] = this->_request.getQuery();

    // 3. File Name (具体执行的脚本物理路径) ──► 映射为 SCRIPT_FILENAME
    this->_env["SCRIPT_FILENAME"] = this->_script_path; // 比如 "/var/www/cgi-bin/test.py"

    // 4. 追加：对于 POST 请求，脚本需要知道拿多少数据
    if (this->_request.getMethod() == "POST")
    {
        // 1. 直接用你写好的 getHeader 去查 "Content-Length" 字符串
        std::string len_str;
        if (this->_request.getHeader("Content-Length", len_str))
        {
            this->_env["CONTENT_LENGTH"] = len_str; // 找到了，直接装箱
        }
        else
        {
            this->_env["CONTENT_LENGTH"] = "0"; // 找不到默认为 0
        }

        // 2. 同样，直接用 getHeader 去查 "Content-Type"
        std::string content_type;
        if (this->_request.getHeader("Content-Type", content_type))
        {
            this->_env["CONTENT_TYPE"] = content_type;
        }
        else
        {
            this->_env["CONTENT_TYPE"] = "";
        }
    }
}

char **CgiHandler::_initEnv() const
{
    // 1. 申请外层指针阵列 (大小为 N + 1)
    char **envp = new char *[this->_env.size() + 1];

    size_t i = 0;
    // 2. 将 C++ 的 map 物理拼接并复制为 "KEY=VALUE" 格式的 C 字符串
    for (std::map<std::string, std::string>::const_iterator it = this->_env.begin();
         it != this->_env.end(); ++it)
    {
        std::string env_entry = it->first + "=" + it->second;

        // 使用 C 库函数 strdup 物理分配并复制这串字符
        envp[i] = strdup(env_entry.c_str());
        i++;
    }

    // 3. 压入黄金哨兵
    envp[i] = NULL;

    return envp;
}

void CgiHandler::_clearEnv(char **envp) const
{
    if (envp == NULL)
        return;

    // 1. 顺着指针阵列一路往下摸，直到撞到我们布下的黄金哨兵 NULL
    for (size_t i = 0; envp[i] != NULL; ++i)
    {
        // 物理释放当年用 strdup (底层是 malloc) 强行开辟的独立 C 风格字符串
        free(envp[i]);
    }

    // 2. 当里面的所有字串都被物理蒸发后，最后释放最外层的 char* 指针大阵列
    // 对应我们当年 new char*[size + 1] 出来的那个大盘子
    delete[] envp;
}

std::string CgiHandler::execute()
{
    // 1. 率先物理初始化环境变量账本
    this->_buildEnv();

    // 2. 声明两个管道的网线端点数组（[0]代表读，[1]代表写）
    int parent_to_child[2];
    int child_to_parent[2];

    // 3. 物理开辟第一根管道：父 -> 子
    if (pipe(parent_to_child) < 0)
    {
        std::cerr << "[CGI Error] Failed to create parent_to_child pipe." << std::endl;
        return ""; // 发生灾难，拉响警报，安全退回空字符串
    }

    // 4. 物理开辟第二根管道：子 -> 父
    if (pipe(child_to_parent) < 0)
    {
        std::cerr << "[CGI Error] Failed to create child_to_parent pipe." << std::endl;
        //  黄金防御：第二根管道失败了，必须把刚才已经成功打开的第一根管道关掉，防止 FD 泄漏！
        close(parent_to_child[0]);
        close(parent_to_child[1]);
        return "";
    }

    // 5. 准备工作就绪，进程开始
    pid_t pid = fork();
    if (pid < 0)
    {
        std::cerr << "[CGI Error] Fork failed." << std::endl;
        // fork 失败，全部管道当场注销
        close(parent_to_child[0]);
        close(parent_to_child[1]);
        close(child_to_parent[0]);
        close(child_to_parent[1]);
        return "";
    }

    if (pid == 0) // 2. 子进程车间（由 fork 繁衍出的独立平行世界）
    {
        // 1. 移花接木：把标准输入 (0) 绑到管道 A 的读端
        // 子进程从这里物理读取父进程喂进来的 POST Body 数据
        if (dup2(parent_to_child[0], 0) < 0)
        {
            _exit(1);
        }

        // 2. 移花接木：把标准输出 (1) 绑到管道 B 的写端
        // 子进程脚本 print 出来的网页数据，会源源不断通过这根网线流回父进程
        if (dup2(child_to_parent[1], 1) < 0)
        {
            _exit(1);
        }

        dup2(child_to_parent[1], STDERR_FILENO); // 🚀 把标准错误也绑到管道上！这样 Python 的崩溃报错我们

        // 3. 彻底卸载子进程手里原先拿着的 4 个原始管道端点
        // 它们已经被 dup2 成功复制到了 0 和 1，原先的 FD 编号必须关闭，否则会发生物理阻塞死锁！
        close(parent_to_child[0]);
        close(parent_to_child[1]);
        close(child_to_parent[0]);
        close(child_to_parent[1]);

        // 4. 精确构建学姐确认的 C 风格执行参数数组
        char *args[] = {
            const_cast<char *>(this->_script_path.c_str()), // args[0]: 脚本路径
            NULL                                            // 末尾黄金封口哨兵
        };

        // 5. 物理转化我们之前写好的、无懈可击的环境变量账本
        char **envp = this->_initEnv();

        // 6. 物理变身！子进程在此金蝉脱壳
        execve(args[0], args, envp);

        // 超级探针】：如果 execve 成功，绝对不会执行到这里！
        //  走到这里，说明变身失败。我们绕过管道，强行往操作系统的标准错误（终端控制台）上打印真相：
        std::cerr
            << "\n[🚨 CGI CHILD EMERGENCY] execve failed!" << std::endl;
        std::cerr << "-> Executable path tried: [" << args[0] << "]" << std::endl;
        std::cerr << "-> Arg[0]: [" << (args[0] ? args[0] : "NULL") << "]" << std::endl;
        std::cerr << "-> Arg[1]: [" << (args[1] ? args[1] : "NULL") << "]" << std::endl;
        std::cerr << "-> System Reason (errno): " << strerror(errno) << " (code: " << errno << ")\n"
                  << std::endl;

        // 7. 熔断：如果代码走到了这一行，100% 说明 execve 失败了（比如脚本无执行权限或路径打错）
        // 我们在外面把它的 envp 堆内存清掉，然后用 _exit(1) 当场冷酷抹杀，绝对防止它回头夺舍 Webserv 主进程！
        this->_clearEnv(envp); // 顺便调用我们之前提过的清理函数
        _exit(1);
    }
    else // 3. 父进程大管家车间（pid > 0）
    {
        // 【第一步：率先清场】关闭父进程绝对不用的两个多余端点（学姐刚才切得最准的一刀！）
        close(parent_to_child[0]);
        close(child_to_parent[1]);

        // 【第二步：喂饱子进程】如果是 POST 请求，把 Body 物理灌入管道
        if (this->_request.getMethod() == "POST" && !this->_request.getBody().empty())
        {
            // 工业级防御：如果 Body 极大，非阻塞下 write 可能无法一次写完，这里用循环写满它
            std::string body = this->_request.getBody();
            size_t total_written = 0;
            while (total_written < body.size())
            {
                ssize_t bytes = ::write(parent_to_child[1], body.data() + total_written, body.size() - total_written);
                if (bytes <= 0)
                {
                    break; // 写入失败或中断，及时跳出熔断
                }
                total_written += bytes;
            }
        }

        // 【第三步：打破死锁】
        // 数据喂完了，立刻掐断管道 A 的写端！向子进程发送物理 EOF 信号，放行子进程的 read()！
        close(parent_to_child[1]);

        // 【第四步：收割动态网页】从管道 B 的读端，悉数回收子进程脚本 print 出来的全部结晶
        std::string response_body;
        char buffer[4096];
        while (true)
        {
            ssize_t bytes_read = ::read(child_to_parent[0], buffer, sizeof(buffer));
            if (bytes_read > 0)
            {
                // 物理 append 拼接数据
                response_body.append(buffer, bytes_read);
            }
            else if (bytes_read == 0)
            {
                // 读到 0 说明子进程脚本执行完毕，且它的写端也正常关闭了，收工！
                break;
            }
            else
            {
                // 读报错（比如被信号中断），物理熔断
                break;
            }
        }

        // 【第五步：最后退场清洗】关掉剩下的最后一个读端网线
        close(child_to_parent[0]);

        // 【第六步：子进程灵魂超度】
        // 此时子进程大概率已经执行完毕。我们调用 waitpid 阻塞收尸，彻底释放系统的 PID 资源
        int status;
        waitpid(pid, &status, 0);

        // 可选调试判定：如果子进程是非正常退出的（比如脚本内部语法错误崩溃），可以打个日志
        if (WIFEXITED(status) && WEXITSTATUS(status) != 0)
        {
            std::cerr << "[CGI Warning] CGI script exited with error code: "
                      << WEXITSTATUS(status) << std::endl;
        }

        // 【第七步：最后退场清洗】
        this->_clearEnv(this->_initEnv());

        // 【第八步：凯旋归来】
        return response_body;
    }
}

std::string CgiHandler::buildHttpResponse(const std::string &cgi_output) const
{
    std::string http_response;

    // 1. 物理检查：子进程是不是偷懒，完全没吐出任何数据？
    if (cgi_output.empty())
    {
        // 灾难响应：脚本内部彻底崩了或者没输出，直接包装成 502 Bad Gateway 丢过去
        http_response = "HTTP/1.1 502 Bad Gateway\r\n"
                        "Server: Webserv/1.0\r\n"
                        "Content-Length: 0\r\n"
                        "Connection: close\r\n\r\n";
        return http_response;
    }

    // 2. 测谎判定：检查子进程吐出来的数据里，有没有自带 "\r\n\r\n" 或 "\n\n"（说明它自己写了 Header）
    bool has_cgi_header = (cgi_output.find("\r\n\r\n") != std::string::npos ||
                           cgi_output.find("\n\n") != std::string::npos);

    if (has_cgi_header)
    {
        // 情况 A：脚本自己带了头（比如带了 Content-Type: text/html）
        // 我们只需要在最前面贴上 HTTP/1.1 200 OK 状态行和 Server 标签，然后和原产物严丝合缝拼起来！
        http_response = "HTTP/1.1 200 OK\r\n"
                        "Server: Webserv/1.0\r\n";
        http_response += cgi_output; // 脚本自带的 Content-Type 和 \r\n\r\n 正好成了天然的分割线
    }
    else
    {
        // 情况 B：脚本只吐了纯肉（纯 HTML 正文）
        // 大管家必须亲自出马，替它把所有的衣服（Header）穿戴整齐：

        // C++98 稳健计算正文物理长度
        std::stringstream ss;
        ss << cgi_output.size();

        http_response = "HTTP/1.1 200 OK\r\n"
                        "Server: Webserv/1.0\r\n"
                        "Content-Type: text/html\r\n" // 默认当做 html 网页处理
                        "Content-Length: " +
                        ss.str() + "\r\n"
                                   "\r\n" // 铁打不动的黄金空行分隔符！
                        + cgi_output;     // 塞入正文
    }

    return http_response;
}
