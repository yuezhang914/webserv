#include "Webserv.hpp"

/*
函数：CgiHandler::CgiHandler
用途：CgiHandler 带参构造函数，将 CGI 请求的 HTTP 上下文与脚本元数据初始化到类成员变量中。
参数：
    - script_path      : CGI 脚本在磁盘上的路径。
    - interpreter_path : 解释器可执行文件路径（若为空则直接执行脚本）。
    - method           : HTTP 请求方法。
    - query            : URL 查询字符串。
    - path             : 客户端请求的 URI 路径。
    - headers          : 客户端 HTTP Header 键值对。
    - host / port      : 服务器主机名/IP 及端口号。
    - root             : Web 根目录路径（DOCUMENT_ROOT）。
实现逻辑：
1. 使用 C++ 初始化列表高效将传入参数赋值给私有成员变量。
2. 构造完成后实例即具备生成环境变量及裂变子进程所需的全部上下文信息。
*/
CgiHandler::CgiHandler(const std::string &script_path,
                       const std::string &interpreter_path,
                       const std::string &method,
                       const std::string &query,
                       const std::string &path,
                       const std::map<std::string, std::string> &headers,
                       const std::string &host,
                       const std::string &port,
                       const std::string &root)
    : _script_path(script_path),
      _interpreter_path(interpreter_path),
      _method(method),
      _query(query),
      _path(path),
      _headers(headers),
      _host(host),
      _port(port),
      _root(root) {}

/*
函数：CgiHandler::~CgiHandler
用途：CgiHandler 析构函数，回收其实例生命周期结束时的资源。
实现逻辑：
1. 析构体留空：CgiHandler 作为轻量级配置构建器，不直接持有长生命周期的系统底层资源。
2. 资源安全交接：创建的双向管道 FD 和子进程 PID 已封装并交接给 CgiManager 统一管理。
3. RAII 机制：类内部持有的 std::string 与 std::map 成员通过 RAII 自动释放内存。
*/
CgiHandler::~CgiHandler() {}

/*
函数：CgiHandler::_buildEnvironment
用途：根据 CGI/1.1 规范，将请求信息与 Header 解析并重组为 POSIX 标准的 C 风格环境变量矩阵（char**）。
参数：
    - 无（读取类成员变量）。
返回值：
    - char** : 动态分配的字符串指针数组，末尾以 NULL 结束；内存分配失败则返回 NULL。
实现逻辑：
1. 提取内部标头：遍历请求头获取脚本名、Path-Info 及根目录。
2. 填充标准变量：向 envMap 中注入 GATEWAY_INTERFACE、REQUEST_METHOD、QUERY_STRING、PATH 等 CGI 必备环境变量。
3. 转换 HTTP 标头：过滤内部控制头，将 Content-Type/Length 转换为大写规范变量，其余普通 Header 统一加上 "HTTP_" 前缀并将横杠转为下划线。
4. 矩阵内存分配：按 map 大小开辟 char** 数组空间，遍历 map 将 "KEY=VALUE" 字符串深拷贝写入，末尾设为 NULL 哨兵。
*/
char **CgiHandler::_buildEnvironment() const
{
    std::map<std::string, std::string> envMap;

    std::string scriptName = _path;
    std::string pathInfo = "";
    for (std::map<std::string, std::string>::const_iterator it = _headers.begin();
         it != _headers.end(); ++it)
    {
        std::string lowerKey = it->first;
        for (size_t k = 0; k < lowerKey.size(); ++k)
            lowerKey[k] = static_cast<char>(std::tolower(lowerKey[k]));
        if (lowerKey == "x-internal-cgi-script-name")
            scriptName = it->second;
        else if (lowerKey == "x-internal-cgi-path-info")
            pathInfo = it->second;
    }
    envMap["GATEWAY_INTERFACE"] = "CGI/1.1";
    envMap["SERVER_PROTOCOL"] = "HTTP/1.1";
    envMap["REQUEST_METHOD"] = _method;
    envMap["QUERY_STRING"] = _query;
    envMap["SCRIPT_NAME"] = scriptName;
    envMap["SCRIPT_FILENAME"] = _script_path;
    if (!pathInfo.empty())
    {
        envMap["PATH_INFO"] = pathInfo;
        envMap["PATH_TRANSLATED"] = _root + pathInfo;
    }
    else
    {
        envMap["PATH_INFO"] = _path;
        envMap["PATH_TRANSLATED"] = _script_path;
    }
    envMap["SERVER_NAME"] = _host;
    envMap["SERVER_PORT"] = _port;
    envMap["DOCUMENT_ROOT"] = _root;
    envMap["REQUEST_URI"] = _path;
    envMap["SCRIPT_URL"] = scriptName;
    const char *sysPath = std::getenv("PATH");
    if (sysPath != NULL)
        envMap["PATH"] = sysPath;
    else
        envMap["PATH"] = "/usr/local/bin:/usr/bin:/bin";
    for (std::map<std::string, std::string>::const_iterator it = _headers.begin();
         it != _headers.end(); ++it)
    {
        std::string key = it->first;
        std::string val = it->second;
        std::string lowerKey = key;
        for (size_t k = 0; k < lowerKey.size(); ++k)
            lowerKey[k] = static_cast<char>(std::tolower(lowerKey[k]));
        if (lowerKey == "x-internal-cgi-script-name" ||
            lowerKey == "x-internal-cgi-path-info" ||
            lowerKey == "x-internal-cgi-path" ||
            lowerKey == "x-internal-cgi-interpreter" ||
            lowerKey == "x-internal-cgi-document-root")
            continue;
        if (lowerKey == "content-type")
            envMap["CONTENT_TYPE"] = val;
        else if (lowerKey == "content-length")
            envMap["CONTENT_LENGTH"] = val;
        else
        {
            std::string envKey = "HTTP_";
            for (size_t k = 0; k < key.size(); ++k)
            {
                if (key[k] == '-')
                    envKey += '_';
                else
                    envKey += static_cast<char>(std::toupper(key[k]));
            }
            envMap[envKey] = val;
        }
    }
    char **env = static_cast<char **>(std::malloc(sizeof(char *) * (envMap.size() + 1)));
    if (!env)
        return NULL;
    size_t i = 0;
    for (std::map<std::string, std::string>::const_iterator it = envMap.begin();
         it != envMap.end(); ++it)
    {
        std::string envStr = it->first + "=" + it->second;
        env[i] = static_cast<char *>(std::malloc(envStr.size() + 1));
        if (env[i])
            std::strcpy(env[i], envStr.c_str());
        ++i;
    }
    env[i] = NULL;
    return env;
}

/*
函数：CgiHandler::_freeEnvironment
用途：安全释放由 _buildEnvironment() 动态开辟的环境变量矩阵内存。
参数：
    - char **env : 待释放的环境变量矩阵指针。
实现逻辑：
1. 判空防卫：若 env 为 NULL 则直接返回。
2. 逐层释放：遍历矩阵，依次释放每个 "KEY=VALUE" 字符串的堆内存。
3. 外层销毁：子字符串释放完毕后，调用 std::free(env) 销毁外层指针数组，防止内存泄漏。
*/
void CgiHandler::_freeEnvironment(char **env) const
{
    if (env == NULL)
        return;
    size_t i = 0;
    while (env[i] != NULL)
    {
        std::free(env[i]);
        ++i;
    }
    std::free(env);
}

/*
函数：directoryName
用途：解析并提取文件路径中的父级目录部分（模仿 POSIX dirname 行为）。
参数：
    - const std::string &path : 传入的完整文件或脚本路径。
返回值：
    - std::string : 截取出的父级目录路径。
实现逻辑：
1. 查找分隔符：从后往前查找最后一个 '/' 的位置。
2. 边界处理：若无 '/' 则返回 "."；若 '/' 在首位则返回 "/"。
3. 截取返回：返回最后一个 '/' 之前的所有字符作为目录路径。
*/
static std::string directoryName(const std::string &path)
{
    size_t pos = path.find_last_of('/');
    if (pos == std::string::npos)
        return ".";
    if (pos == 0)
        return "/";
    return path.substr(0, pos);
}

/*
函数：baseName
用途：从完整路径中提取纯文件名部分（模仿 POSIX basename 行为）。
参数：
    - const std::string &path : 传入的完整路径字符串。
返回值：
    - std::string : 剥离路径前缀后的纯文件名。
实现逻辑：
1. 查找分隔符：从后往前查找最后一个 '/' 的位置。
2. 截取返回：若无 '/' 则原样返回路径；否则截取最后一个 '/' 之后的所有字符返回。
*/
static std::string baseName(const std::string &path)
{
    size_t pos = path.find_last_of('/');
    if (pos == std::string::npos)
        return path;
    return path.substr(pos + 1);
}

/*
函数：configureParentPipeFd
用途：为父进程保留的管道 FD 配置非阻塞属性（O_NONBLOCK），确保异步读写不卡死主线程。
参数：
    - int fd : 待配置的文件描述符。
返回值：
    - bool : 配置成功返回 true，失败返回 false。
实现逻辑：
1. 调用 fcntl 附加 O_NONBLOCK 标志，将管道设为非阻塞模式。
*/
static bool configureParentPipeFd(int fd)
{
    if (fcntl(fd, F_SETFL, O_NONBLOCK) < 0)
        return false;
    return true;
}

/*
函数：CgiHandler::_setupPipes
用途：创建父子进程双向通信管道，并配置父端管道 FD 为非阻塞模式。
参数：
    - int pipe_to_parent[2] : “子进程 -> 父进程”管道的读写端数组。
    - int pipe_to_child[2]  : “父进程 -> 子进程”管道的读写端数组。
返回值：
    - bool : 创建与配置成功返回 true，失败则清理已打开的 FD 并返回 false。
实现逻辑：
1. 创建管道：分别调用 ::pipe 创建父子双向通信管道，失败时安全关闭已分配的 FD。
2. 配置非阻塞：调用 configureParentPipeFd 将父进程持有的读端和写端设为非阻塞，防止卡死主循环。
*/
bool CgiHandler::_setupPipes(int pipe_to_parent[2], int pipe_to_child[2])
{
    if (pipe(pipe_to_parent) < 0)
    {
        std::cerr << "[CGI] Error: pipe_to_parent failed." << std::endl;
        return false;
    }
    if (pipe(pipe_to_child) < 0)
    {
        std::cerr << "[CGI] Error: pipe_to_child failed." << std::endl;
        close(pipe_to_parent[0]);
        close(pipe_to_parent[1]);
        return false;
    }
    if (!configureParentPipeFd(pipe_to_parent[0]) || !configureParentPipeFd(pipe_to_child[1]))
    {
        std::cerr << "[CGI] Error: configureParentPipeFd failed." << std::endl;
        close(pipe_to_parent[0]);
        close(pipe_to_parent[1]);
        close(pipe_to_child[0]);
        close(pipe_to_child[1]);
        return false;
    }
    return true;
}

/*
函数：CgiHandler::_executeChildProcess
用途：在 fork 出来的子进程独立空间中，完成 I/O 重定向、工作目录切换、环境变量重组与 CGI 镜像替换。
参数：
    - int childReadFd : 父进程写入 Body 数据的管道读端，用于重定向为子进程的 STDIN_FILENO。
    - int parentWriteFd: 子进程吐出脚本输出的管道写端，用于重定向为子进程的 STDOUT_FILENO。
返回值：
    - void（若 execve 调用成功，该函数永远不返回；若中途失败，通过 ::exit(127) 物理结束子进程）。
实现逻辑或说明：
    1. 使用 dup2() 将 childReadFd 复制并重定向为 STDIN，随后关闭原 FD。
    2. 使用 dup2() 将 parentWriteFd 复制并重定向为 STDOUT，随后关闭原 FD。
    3. 解析脚本所在目录并调用 chdir() 切换工作目录，确保 CGI 脚本能正确读取同目录下的相对路径资产。
    4. 构建标准 CGI 环境变量数组（env），若构建失败则退出。
    5. 根据 _interpreter_path 是否为空，选择通过解释器（如 Python/PHP）或作为二进制可执行文件构建 execve 的 args 参数。
    6. 调用 ::execve 执行脚本。若 execve 失败，释放 env 内存并调用 ::exit(127) 强制物理终止子进程，绝不让子进程倒流回父进程的代码逻辑。
*/
void CgiHandler::_executeChildProcess(int childReadFd, int parentWriteFd)
{
    if (childReadFd != STDIN_FILENO)
    {
        if (dup2(childReadFd, STDIN_FILENO) < 0)
        {
            close(childReadFd);
            close(parentWriteFd);
            ::exit(127);
        }
        close(childReadFd);
    }
    if (parentWriteFd != STDOUT_FILENO)
    {
        if (dup2(parentWriteFd, STDOUT_FILENO) < 0)
        {
            ::exit(127);
        }
        close(parentWriteFd);
    }
    std::string scriptDirectory = directoryName(_script_path);
    std::string scriptName = baseName(_script_path);
    if (chdir(scriptDirectory.c_str()) != 0)
        ::exit(127);
    char **env = _buildEnvironment();
    if (env == NULL)
        ::exit(127);
    for (int i = 3; i < FD_SETSIZE; ++i)
        ::close(i);
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
    std::cerr << "[CGI] execve failed: " << strerror(errno) << std::endl;
    _freeEnvironment(env);
    ::exit(127);
}

/*
函数：CgiHandler::async_launch
用途：异步启动 CGI 任务的主入口入口函数。负责调度管道创建、裂变子进程以及交付父进程读写 FD。
参数：
    - 无（使用类成员变量 _script_path, _interpreter_path 等）。
返回值：
    - CgiFds: 结构体，包含子进程 pid、父进程监听 CGI 输出的 read_fd 以及父进程向 CGI 写入 Body 的 write_fd。
      （若启动失败，结构体内部字段值默认初始化为 -1）。
实现逻辑或说明：
    1. 调用 _setupPipes 完成双向管道创建与父进程端非阻塞 FD 设置。
    2. 调用 ::fork() 裂变子进程：
       - 若 pid < 0：创建失败，关闭所有 4 个管道 FD 并返回默认空 fds；
       - 若 pid == 0：进入子进程分支，关闭不必要的管道端，交由 _executeChildProcess() 去重定向并 execve；
       - 若 pid > 0：进入父进程分支，关闭子进程占用的读/写管道端，将保留的父端 read_fd 与 write_fd 填入 CgiFds 并返回。
*/
CgiFds CgiHandler::async_launch()
{
    CgiFds fds;
    int pipe_to_parent[2];
    int pipe_to_child[2];

    if (!_setupPipes(pipe_to_parent, pipe_to_child))
        return fds;
    fds.pid = fork();
    if (fds.pid < 0)
    {
        close(pipe_to_parent[0]);
        close(pipe_to_parent[1]);
        close(pipe_to_child[0]);
        close(pipe_to_child[1]);
        return fds;
    }
    if (fds.pid == 0)
    {
        close(pipe_to_parent[0]);
        close(pipe_to_child[1]);
        this->_executeChildProcess(pipe_to_child[0], pipe_to_parent[1]);
    }
    close(pipe_to_child[0]);
    close(pipe_to_parent[1]);
    fds.read_fd = pipe_to_parent[0];
    fds.write_fd = pipe_to_child[1];
    return fds;
}
