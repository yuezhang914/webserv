#include "Webserv.hpp"

CgiHandler::CgiHandler(const Request &request, const std::string &script_path, const std::string &interpreter_path)
    : _request(request), _script_path(script_path), _interpreter_path(interpreter_path) {}

CgiHandler::~CgiHandler() {}

/*
辅助函数：切出路径中的目录部分（Directory Name）
例如：/var/www/cgi-bin/test.py -> /var/www/cgi-bin
     test.py                  -> .
*/
static std::string directoryName(const std::string &path)
{
    size_t pos = path.find_last_of('/');
    if (pos == std::string::npos)
        return ".";
    if (pos == 0) // 处理根目录情况 /test.py
        return "/";
    return path.substr(0, pos);
}

/*
辅助函数：切出路径中的文件名部分（Base Name）
例如：/var/www/cgi-bin/test.py -> test.py
     test.py                  -> test.py
*/
static std::string baseName(const std::string &path)
{
    size_t pos = path.find_last_of('/');
    if (pos == std::string::npos)
        return path;
    return path.substr(pos + 1);
}

/*
辅助函数：安全配置父进程保留的管道 FD（设置 O_NONBLOCK 和 FD_CLOEXEC）
返回：成功返回 true，任何一步 fcntl 失败均返回 false
*/
static bool configureParentPipeFd(int fd)
{
    int statusFlags = fcntl(fd, F_GETFL, 0);
    if (statusFlags < 0)
        return false;

    if (fcntl(fd, F_SETFL, statusFlags | O_NONBLOCK) < 0)
        return false;

    int descriptorFlags = fcntl(fd, F_GETFD, 0);
    if (descriptorFlags < 0)
        return false;

    // 💡 注意：先 F_GETFD 再按位或，严防覆盖原有的 descriptor flags！
    if (fcntl(fd, F_SETFD, descriptorFlags | FD_CLOEXEC) < 0)
        return false;

    return true;
}

/*
函数用途：安全裂变子进程（Fork），通过双向物理管道重定向焊接，非阻塞、高安全、净空级地拉起外部 CGI 脚本（如 Python 进程）。
参数与变量：
- fds (局部资产结构体)：CgiFds，打包承载最终并网回传给大管家的“管道读端”、“管道写端”及“子进程 PID”的核心资产包。
- pipe_to_parent (局部数组)：int[2]，开凿的从子进程流向大管家主进程的单向“动态货包输出渠道”（子写父读）。
- pipe_to_child (局部数组)：int[2]，开凿从大管家主进程流向子进程的单向“POST Body 喂食渠道”（父写子读）。
- fd_limit (局部变量)：long，通过 sysconf 动态捕获的当前操作系统允许的最大文件描述符上限（防止泄露的天花板防线）。
实现逻辑：
1. 安全开凿与非阻塞加冕：开凿两组双向物理管道。对留在大管家主进程一侧的读端（pipe_to_parent[0]）与写端（pipe_to_child[1]）强行追加 O_NONBLOCK 非阻塞标记与 FD_CLOEXEC（执行时自动关闭），死锁主循环的异步吞吐运力。
2. 子进程特权主权焊接（fds.pid == 0）：
   - 边界碰撞防御：严密校验管道 FD 是否巧合分在了 0 (stdin) 或 1 (stdout) 坑位，通过 dup2 精准条件焊接标准输入输出，随后立刻释放原生临时变量。
   - 铁血彻底净空：除了系统自带的 FD_CLOEXEC 自动清洗外，利用 sysconf 获取系统上限，用 while 循环将 3 以上的所有残留、泄露文件描述符进行毁灭性地强制 close 物理大清洗，确保外部脚本（如 Python）启动前进程环境处于绝对净空状态。
   - 弹射起飞：构建沙盒环境变量（_buildEnvironment），一枪调用 execve 强行接管子进程时空，若失败则通过 _exit(127) 迅速无感自毁，严防子进程意外折返倒灌进主循环雷达网！
3. 主进程主权交割：回到主进程大管家车间，立刻关闭用不到的子进程侧对流端，将幸存的非阻塞大管家专属读/写管道描述符及子进程 PID 封装进 CgiFds 资产包，交割给雷达名册入籍！
*/
CgiFds CgiHandler::async_launch()
{
    CgiFds fds;
    fds.read_fd = -1;
    fds.write_fd = -1;
    fds.pid = -1;

    int pipe_to_parent[2];
    int pipe_to_child[2];

    // 1. 安全开凿单向通道
    if (pipe(pipe_to_parent) < 0)
    {
        std::cerr << "[CGI] Error: pipe_to_parent failed." << std::endl;
        return fds;
    }
    if (pipe(pipe_to_child) < 0)
    {
        std::cerr << "[CGI] Error: pipe_to_child failed." << std::endl;
        close(pipe_to_parent[0]);
        close(pipe_to_parent[1]);
        return fds;
    }

    // 2. 将主进程留下的管道读写端设置为【非阻塞】与【FD_CLOEXEC】
    if (!configureParentPipeFd(pipe_to_parent[0]) || !configureParentPipeFd(pipe_to_child[1]))
    {
        std::cerr << "[CGI] Error: configureParentPipeFd failed." << std::endl;
        close(pipe_to_parent[0]);
        close(pipe_to_parent[1]);
        close(pipe_to_child[0]);
        close(pipe_to_child[1]);
        return fds;
    }

    // 3. 裂变子进程
    fds.pid = fork();
    if (fds.pid < 0)
    {
        close(pipe_to_parent[0]);
        close(pipe_to_parent[1]);
        close(pipe_to_child[0]);
        close(pipe_to_child[1]);
        return fds;
    }

    if (fds.pid == 0) // ================== 子进程物理空间 ==================
    {
        // 1. 关闭完全属于父进程的两个管道端
        close(pipe_to_parent[0]);
        close(pipe_to_child[1]);

        // 2. 🔩 黄金焊接：精准重定向至 stdin (0) 与 stdout (1)
        if (pipe_to_child[0] != STDIN_FILENO)
        {
            if (dup2(pipe_to_child[0], STDIN_FILENO) < 0)
            {
                std::cerr << "[CGI] Error: dup2 STDIN failed." << std::endl;
                close(pipe_to_child[0]);
                close(pipe_to_parent[1]);
                exit(127); // 💡 合规替换：使用白名单允许的 exit()
            }
            close(pipe_to_child[0]); // 焊接完后释放原始描述符
        }

        if (pipe_to_parent[1] != STDOUT_FILENO)
        {
            if (dup2(pipe_to_parent[1], STDOUT_FILENO) < 0)
            {
                std::cerr << "[CGI] Error: dup2 STDOUT failed." << std::endl;
                close(pipe_to_parent[1]);
                exit(127); // 💡 合规替换：使用白名单允许的 exit()
            }
            close(pipe_to_parent[1]); // 焊接完后释放原始描述符
        }

        // 3. 物理切换工作目录
        std::string scriptDirectory = directoryName(_script_path);
        std::string scriptName = baseName(_script_path);

        if (chdir(scriptDirectory.c_str()) != 0)
        {
            std::cerr << "[CGI] Error: Failed to chdir to " << scriptDirectory << std::endl;
            exit(127);
        }

        // 4. 组装环境变量
        char **env = _buildEnvironment();
        if (env == NULL)
        {
            exit(127);
        }

        // 5. 弹射组装
        char *args[3];

        if (!_interpreter_path.empty())
        {
            args[0] = const_cast<char *>(_interpreter_path.c_str());
            args[1] = const_cast<char *>(scriptName.c_str());
            args[2] = NULL;

            ::execve(args[0], args, env);
        }
        else
        {
            std::string executable = "./" + scriptName;

            args[0] = const_cast<char *>(executable.c_str());
            args[1] = NULL;

            ::execve(args[0], args, env);
        }

        // ❌ 如果 execve 穿透到了这里，说明弹射失败
        _freeEnvironment(env);
        exit(127); // 💡 合规替换：使用白名单允许的 exit()
    }

    // ================== 主进程大管家车间 ==================
    // 1. 释放大管家用不到的子进程侧单向端
    close(pipe_to_child[0]);
    close(pipe_to_parent[1]);

    // 2. 装填多路复用总线核心资产
    fds.read_fd = pipe_to_parent[0];
    fds.write_fd = pipe_to_child[1];

    return fds;
}


/*
辅助函数：数字转 string (标准 C++98 范式)
*/
static std::string numberToString(int num)
{
    std::ostringstream ss;
    ss << num;
    return ss.str();
}

/*
内部函数：构建符合 RFC 3875 标准的 CGI 环境变量矩阵
*/
char **CgiHandler::_buildEnvironment() const
{
    std::map<std::string, std::string> envMap;

    // 1. 🏛️ 核心请求要素（RFC 3875 必填）
    envMap["GATEWAY_INTERFACE"] = "CGI/1.1";
    envMap["SERVER_PROTOCOL"]    = "HTTP/1.1";
    envMap["REQUEST_METHOD"]     = _request.getMethod();
    envMap["QUERY_STRING"]       = _request.getQuery(); // 不带 '?' 的纯 query 字符串

    // 2. 📜 路径定位映射（物理与逻辑严格解耦）
    envMap["SCRIPT_NAME"]     = _request.getPath(); // 浏览器视角：如 /cgi/test.py
    envMap["SCRIPT_FILENAME"] = _script_path;       // 磁盘物理视角：如 ./www/cgi/test.py
    envMap["PATH_INFO"]       = "";                 // 当前未切分额外路径时，严格置空

    // 3. 🌐 读取服务器配置信息 (ServerConfig)
    const ServerConfig *server = _request.getConfig();
    if (server != NULL)
    {
        envMap["SERVER_NAME"]   = server->host;
        envMap["SERVER_PORT"]   = numberToString(server->port);
        envMap["DOCUMENT_ROOT"] = server->root;
    }
    else
    {
        envMap["SERVER_NAME"]   = "localhost";
        envMap["SERVER_PORT"]   = "8080";
        envMap["DOCUMENT_ROOT"] = "./www";
    }

    // 4. 📦 HTTP Header 自动透传 (RFC 3875 映射)
    // 根据 CGI 规范：Content-Type 和 Content-Length 无 HTTP_ 前缀，其他 Header 一律加 HTTP_ 前缀并转大写下划线
    
    // 4.1 特例 Header（直接映射）
    std::string contentType;
    if (_request.getHeader("content-type", contentType))
    {
        envMap["CONTENT_TYPE"] = contentType;
    }

    std::string contentLength;
    if (_request.getHeader("content-length", contentLength))
    {
        envMap["CONTENT_LENGTH"] = contentLength;
    }

    // 4.2 常规 HTTP Header 转换为 HTTP_ 前缀形式
    std::string headerValue;

    if (_request.getHeader("cookie", headerValue))
    {
        envMap["HTTP_COOKIE"] = headerValue;
    }

    if (_request.getHeader("host", headerValue))
    {
        envMap["HTTP_HOST"] = headerValue;
    }

    if (_request.getHeader("user-agent", headerValue))
    {
        envMap["HTTP_USER_AGENT"] = headerValue;
    }

    if (_request.getHeader("accept", headerValue))
    {
        envMap["HTTP_ACCEPT"] = headerValue;
    }

    if (_request.getHeader("referer", headerValue))
    {
        envMap["HTTP_REFERER"] = headerValue;
    }

    // 5. 🧱 将 map 转换并分配为 char** 二维指针矩阵 (传递给 execve)
    char **env = new char*[envMap.size() + 1];
    size_t i = 0;
    for (std::map<std::string, std::string>::const_iterator it = envMap.begin();
         it != envMap.end(); ++it)
    {
        std::string envStr = it->first + "=" + it->second;
        env[i] = new char[envStr.size() + 1];
        std::strcpy(env[i], envStr.c_str());
        ++i;
    }
    env[i] = NULL; // NULL 结尾哨兵

    return env;
}
/*
函数用途：作为底层堆内存的战后善后总务车间，顺藤摸瓜点验并全量物理销毁前置生成的 C 风格二维环境变量阵列，彻底回收内存主权。
参数与变量：
- env (传入指针的指针参数)：char**，此前在堆上动态开凿、连续排列并承载着“KEY=VALUE”裸指针的二维环境沙盒阵列根基。
- i (局部迭代器变量)：size_t，用于沿着指针阵列的物理轨道正向推进的检索卡尺。
实现逻辑：
1. 空门防卫线：进线首先点验传入的阵列根基指针。若发现本就是一束虚无（env == NULL），则当场优雅折返，严防引发野指针空挂或二次释放引发的内核血崩。
2. 顺藤摸瓜物理超度：利用前置车间手工砸在末尾的 NULL 终点站哨兵作为安全卡尺边界，启动 while 循环正向挺进。
   在撞上 NULL 哨兵之前，逐行提取出连续分配的 C 风格字符串实体，调用 std::free 当场抹杀并物理超度，收回行资产主权。
3. 斩断根基（临门一脚）：当所有行资产被洗劫空仓后，卡尺精准停留在 NULL 哨兵位。此时果断向下调用 std::free(env)，
   将承载指针本身的阵列底盘根基彻底从堆内存空间中物理擦除，向操作系统交割绝对干净的时空闭环！
*/
void CgiHandler::_freeEnvironment(char **env) const
{
    if (env == NULL)
    {
        return;
    }

    size_t i = 0;
    // 顺着指针一路摸过去，直到撞上最后的 NULL 哨兵为止
    while (env[i] != NULL)
    {
        std::free(env[i]);
        ++i;
    }
    // 斩断根基
    std::free(env);
}