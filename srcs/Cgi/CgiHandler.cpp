#include "Webserv.hpp"

CgiHandler::CgiHandler(const Request &request, const std::string &script_path)
    : _request(request), _script_path(script_path) {}

CgiHandler::~CgiHandler() {}

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
    int pipe_to_parent[2];
    int pipe_to_child[2];

    // 1. 安全开凿单向通道（防止短路求值引起的未决句柄泄露）
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

    // 将主进程留下的管道读写端追加为【非阻塞】与【执行时关闭】
    int fl1 = fcntl(pipe_to_parent[0], F_GETFL, 0);
    fcntl(pipe_to_parent[0], F_SETFL, fl1 | O_NONBLOCK);
    fcntl(pipe_to_parent[0], F_SETFD, FD_CLOEXEC);

    int fl2 = fcntl(pipe_to_child[1], F_GETFL, 0);
    fcntl(pipe_to_child[1], F_SETFL, fl2 | O_NONBLOCK);
    fcntl(pipe_to_child[1], F_SETFD, FD_CLOEXEC);

    // 2.子进程
    fds.pid = fork();
    if (fds.pid < 0)
    {
        close(pipe_to_parent[0]);
        close(pipe_to_parent[1]);
        close(pipe_to_child[0]);
        close(pipe_to_child[1]);
        return fds;
    }

    if (fds.pid == 0) // ================== 子进程==================
    {
        // 1. 精准条件焊接与原生临时变量释放，预防管道正好分配到 0 或 1 时的bug
        if (pipe_to_child[0] != STDIN_FILENO)
        {
            dup2(pipe_to_child[0], STDIN_FILENO);
            close(pipe_to_child[0]);
        }
        if (pipe_to_parent[1] != STDOUT_FILENO)
        {
            dup2(pipe_to_parent[1], STDOUT_FILENO);
            close(pipe_to_parent[1]);
        }

        // 关掉完全属于父进程的两个对流方向原生端
        if (pipe_to_parent[0] > STDERR_FILENO)
            close(pipe_to_parent[0]);
        if (pipe_to_child[1] > STDERR_FILENO)
            close(pipe_to_child[1]);

        // 在内核自带的 FD_CLOEXEC 自动清洗之外，
        // 循环 3 以上的所有遗漏 FD，确保子进程去跑 Python 前绝对净空
        long fd_limit = sysconf(_SC_OPEN_MAX);
        if (fd_limit < 0)
        {
            fd_limit = 1024;
        }
        int current_fd = 3;
        while (current_fd < fd_limit)
        {
            close(current_fd);
            ++current_fd;
        }

        // 3. 环境
        char **env = _buildEnvironment();
        if (env == NULL)
        {
            _exit(127);
        }

        char *args[2];
        args[0] = const_cast<char *>(_script_path.c_str());
        args[1] = NULL;

        execve(args[0], args, env);

        _freeEnvironment(env);
        _exit(127);
    }

    // ================== 主进程大管家车间 ==================
    // 释放大管家用不到的子进程侧单向原生端
    close(pipe_to_child[0]);
    close(pipe_to_parent[1]);

    // 装填多路复用总线核心资产
    fds.read_fd = pipe_to_parent[0];
    fds.write_fd = pipe_to_child[1];

    return fds;
}

// 辅助工具：快速把 "KEY=VALUE" 灌入堆内存，转换为 C 风格的 char*
static char *createEnvString(const std::string &key, const std::string &value)
{
    std::string env_pair = key + "=" + value;
    char *str = static_cast<char *>(std::malloc(env_pair.size() + 1));
    if (str != NULL)
    {
        std::strcpy(str, env_pair.c_str());
    }
    return str;
}

// 辅助工具：C++98 专用的 size_t 转 string 车间
static std::string sizeToStr(size_t size)
{
    std::ostringstream ss;
    ss << size;
    return ss.str();
}

char **CgiHandler::_buildEnvironment() const
{
    // 1. 搜集物料：从当前的 _request 智囊大脑里榨取核心协议字段
    std::map<std::string, std::string> env_map;

    env_map["GATEWAY_INTERFACE"] = "CGI/1.1";
    env_map["SERVER_PROTOCOL"] = "HTTP/1.1";
    env_map["SERVER_SOFTWARE"] = "Webserv/1.0";
    env_map["REQUEST_METHOD"] = _request.getMethod(); // "GET" 或 "POST"

    // PATH_INFO: 脚本名后面的额外路由路径（例如 /cgi-bin/test.py/user -> "/user"）
    // 这里如果还没写剥离逻辑，先给个安全的空值，或者直接给整个 path
    env_map["PATH_INFO"] = _request.getPath();

    // SCRIPT_NAME: 脚本在服务器上的真实物理路径
    env_map["SCRIPT_NAME"] = _script_path;

    // QUERY_STRING: GET 请求问号后面的核心参数（例如 "user=admin&token=42"）
    // Python 脚本里的 cgi.FieldStorage() 全靠这个字段来解析入参！
    env_map["QUERY_STRING"] = _request.getQuery();

    // 🚨 针对 POST 上传的关键防线：
    // 如果客户端发了 Body，必须把长度和物料类型同步传给子进程，否则 Python 的 sys.stdin.read() 会不知道读多少字节
    std::string content_type;
    if (_request.getHeader("content-type", content_type))
    {
        env_map["CONTENT_TYPE"] = content_type;
    }
    env_map["CONTENT_LENGTH"] = sizeToStr(_request.getBody().size());

    // 🚀 工业级彩蛋：把客户端传来的所有自定义 HTTP Headers 也全量并网（转换为 HTTP_X 格式）
    // 比如客户端传了 "X-User-Id: 42"，Python 里就能通过 os.environ['HTTP_X_USER_ID'] 跨界读取！
    // 注：此处可根据你的 Request 类是否有迭代 Headers 的接口来决定是否遍历。

    // 2. 物理开凿：在堆上开辟二维指针阵列
    // 大小为 env_map.size() + 1，多出来的那一个格用来放 NULL 作为终点站哨兵
    char **env = static_cast<char **>(std::malloc((env_map.size() + 1) * sizeof(char *)));
    if (env == NULL)
    {
        return NULL;
    }

    // 3. 逐条拓印灌入
    size_t i = 0;
    std::map<std::string, std::string>::const_iterator it = env_map.begin();
    while (it != env_map.end())
    {
        env[i] = createEnvString(it->first, it->second);
        // 如果中间某一条物理内存申请爆雷，需要启动紧急熔断清理（防止半闭环泄露）
        if (env[i] == NULL)
        {
            while (i > 0)
            {
                std::free(env[--i]);
            }
            std::free(env);
            return NULL;
        }
        ++it;
        ++i;
    }

    // 🎯 放置终点站最高防线哨兵
    env[i] = NULL;

    return env;
}

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