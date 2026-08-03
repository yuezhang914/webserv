#include "Webserv.hpp"

/*
函数：CgiHandler::CgiHandler
用途：CgiHandler 类的带参构造函数，用于将处理 CGI 请求所需的全部 HTTP 上下文与脚本元数据灌入类成员变量中。
参数：
    - const std::string &script_path     : CGI 脚本在服务器磁盘上的物理绝对/相对路径（如 "cgi-bin/test.py"）。
    - const std::string &interpreter_path: CGI 脚本解释器可执行文件路径（如 "/usr/bin/python3"，若为空则直接执行二进制）。
    - const std::string &method          : HTTP 请求方法（如 "GET"、"POST"）。
    - const std::string &query           : URL 中的 Query 字符串（如 "user=admin&id=1"）。
    - const std::string &path            : 客户端请求的 URI 路径（如 "/cgi-bin/test.py"）。
    - const std::map<std::string, std::string> &headers: 客户端发来的 HTTP Header 键值对账本。
    - const std::string &host            : 服务器 Host 主机名或 IP。
    - const std::string &port            : 服务器监听的端口号字符串（如 "8080"）。
    - const std::string &root            : 匹配路由的 Web 根目录路径（DOCUMENT_ROOT）。
返回值：
    - 构造函数无返回值。
实现逻辑或说明：
    1. 使用 C++ 初始化列表（Initialization List）高效地将传入的各个参数深拷贝/赋值给类的私有成员变量。
    2. 避免在构造函数体内部二次赋值，提升对象的内存分配与初始化性能。
    3. 构造完成后的 CgiHandler 实例即具备了后续调用 async_launch() 裂变子进程和构建 CGI 环境变量所需的全部上下文信息。
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
用途：CgiHandler 类的析构函数，用于在实例生命周期结束时安全销毁对象。
参数：
    - 无。
返回值：
    - 析构函数无返回值。
实现逻辑或说明：
    1. 函数体为空，未执行任何显式的资源清理操作。
    2. 架构设计说明：在彻底解耦的架构中，CgiHandler 仅仅作为一个轻量级的“启动器/配置构建器（Launcher）”。
    3. 资源安全交接：
       - 它裂变出的双向管道 FD 和子进程 PID 已经打包成 CgiFds 结构体，交接给了 CgiManager 进行统一生命周期管理。
       - 动态申请的环境变量堆内存（_buildEnvironment）在 _executeChildProcess 失败时或子进程结束时已被独立释放。
    4. RAII 机制：类内部持有的 _script_path、_headers 等 std::string 与 std::map 成员对象，将由 C++98 的 RAII 机制在对象析构时自动回收内存，无需在此手动释放。
*/
CgiHandler::~CgiHandler() {}

/*
函数：CgiHandler::_buildEnvironment
用途：根据 RFC 3875 (CGI/1.1) 规范，将 HTTP 请求信息与 Header 头解析并重组为符合 POSIX 标准的 C 风格环境变量矩阵（char**）。
参数：
    - 无（以 const 方式读取类成员变量 _method, _query, _path, _script_path, _host, _port, _root, _headers）。
返回值：
    - char**: 动态分配的字符串指针数组，末尾以 NULL 作为哨兵标记。若内存分配失败则返回 NULL。
实现逻辑或说明：
    1. 实例化临时 C++ std::map<std::string, std::string> envMap，填入 CGI/1.1 必须的基础环境变量（如 GATEWAY_INTERFACE、REQUEST_METHOD、QUERY_STRING 等）。
    2. 遍历请求头账本 _headers，按 RFC 规范单独抽取 CONTENT_TYPE 与 CONTENT_LENGTH。
    3. 将其余 HTTP Header 标头统一加上 "HTTP_" 前缀，并将其中的横杠 '-' 替换为下划线 '_'，字符全部转换为大写（例如 "User-Agent" -> "HTTP_USER_AGENT"）。
    4. 调用 std::malloc 为 char** 矩阵开辟 sizeof(char*) * (envMap.size() + 1) 的堆内存空间。
    5. 循环遍历 envMap，将每一对 Key-Value 拼接为 "KEY=VALUE" 格式，并为其开辟空间及使用 std::strcpy 完成深拷贝。
    6. 将数组末尾元素设为 NULL（哨兵），供 execve() 准确识别环境变量边界。
    *(注：调用者或子进程需使用 _freeEnvironment 函数释放该动态矩阵)*
*/
char **CgiHandler::_buildEnvironment() const
{
    std::map<std::string, std::string> envMap;

    std::string scriptName = _path; // fallback 默认值
    std::string pathInfo = "";

    // 💡 1. 大小写无关查找：先遍历 _headers 匹配内部标头
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

    // 💡 2. 注入标准 CGI 环境变量
    envMap["GATEWAY_INTERFACE"] = "CGI/1.1";
    envMap["SERVER_PROTOCOL"] = "HTTP/1.1";
    envMap["REQUEST_METHOD"] = _method;
    envMap["QUERY_STRING"] = _query;
    envMap["SCRIPT_NAME"] = scriptName;
    envMap["SCRIPT_FILENAME"] = _script_path;

    // 💡 3. 核心修复：针对 42 cgi_tester 的 PATH_INFO 与 PATH_TRANSLATED 规则
    if (!pathInfo.empty())
    {
        envMap["PATH_INFO"] = pathInfo;
        envMap["PATH_TRANSLATED"] = _root + pathInfo;
    }
    else
    {
        // 当没有额外的 path_info 时（例如 /directory/youpi.bla）：
        // cgi_tester 要求 PATH_INFO 不能留空，需设为请求路径 _path
        // PATH_TRANSLATED 需设为物理脚本路径 _script_path
        envMap["PATH_INFO"] = _path; 
        envMap["PATH_TRANSLATED"] = _script_path;
    }

    envMap["SERVER_NAME"] = _host;
    envMap["SERVER_PORT"] = _port;
    envMap["DOCUMENT_ROOT"] = _root;

    envMap["REQUEST_URI"] = _path;
    envMap["SCRIPT_URL"] = scriptName;

    // 继承系统 PATH
    const char *sysPath = std::getenv("PATH");
    if (sysPath != NULL)
        envMap["PATH"] = sysPath;
    else
        envMap["PATH"] = "/usr/local/bin:/usr/bin:/bin";

    // 💡 4. 处理普通 HTTP 请求头并忽略内部标头
    for (std::map<std::string, std::string>::const_iterator it = _headers.begin();
         it != _headers.end(); ++it)
    {
        std::string key = it->first;
        std::string val = it->second;

        std::string lowerKey = key;
        for (size_t k = 0; k < lowerKey.size(); ++k)
            lowerKey[k] = static_cast<char>(std::tolower(lowerKey[k]));

        // 过滤内部 CGI 标头，不暴露给 CGI 环境变量
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
用途：安全释放由 _buildEnvironment() 函数在堆上动态开辟的 C 风格环境变量矩阵（char** 内存空间）。
参数：
    - char **env: 待释放的环境变量矩阵首地址指针。
返回值：
    - void（无返回值）。
Implementation logic or description:
    1. 判空防卫：若传入的 env 为 NULL，直接 return 返回，防止空指针解引用崩溃。
    2. 循环遍历：从索引 0 开始逐个读取矩阵元素，利用哨兵标记（env[i] != NULL）判定边界。
    3. 逐层释放：针对每个由 std::malloc 申请的单条环境变量字符串（"KEY=VALUE"），依次调用 std::free(env[i]) 进行释放。
    4. 外层释放：当子字符串全部释放完毕后，最后调用 std::free(env) 彻底销毁外层指针数组本身的堆空间，达成 100% 零内存泄露。
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
用途：解析并提取文件路径中的父级目录部分（模仿 POSIX dirname 工具函数的行为）。
参数：
    - const std::string &path: 传入的文件或脚本完整路径字符串（如 "cgi-bin/test.py" 或 "/var/www/script.cgi"）。
返回值：
    - std::string: 截取出的父级目录路径。
实现逻辑或说明：
    1. 调用 find_last_of('/') 从后往前查找最后一个路径分隔符 '/' 的位置 pos。
    2. 若未找到 '/'（pos == npos），说明传入的是当前目录下的文件名，返回 "." 表示当前工作目录。
    3. 若最后一个 '/' 位于索引 0（pos == 0），说明该文件位于系统根目录下，返回 "/"。
    4. 其它情况使用 substr(0, pos) 截取最后一个 '/' 之前的所有字符作为物理目录路径。
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
用途：从完整路径中剥离目录前缀，仅提取纯文件名部分（模仿 POSIX basename 工具函数的行为）。
参数：
    - const std::string &path: 传入的文件或脚本完整路径字符串（如 "cgi-bin/test.py"）。
返回值：
    - std::string: 剥离路径前缀后的纯文件名（如 "test.py"）。
实现逻辑或说明：
    1. 调用 find_last_of('/') 从后往前查找最后一个路径分隔符 '/' 的位置 pos。
    2. 若未找到 '/'（pos == npos），说明本身就是纯文件名，直接原样返回 path。
    3. 否则使用 substr(pos + 1) 截取最后一个 '/' 之后的所有字符，返回纯文件名部分。
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
用途：针对父进程保留的管道 FD 进行 UNIX 系统调用配置，设置 O_NONBLOCK（非阻塞）与 FD_CLOEXEC（执行时关闭）属性。
参数：
    - int fd: 待配置属性的管道文件描述符。
返回值：
    - bool: 配置成功返回 true；若任一 fcntl 系统调用失败则返回 false。
实现逻辑或说明：
    1. 调用 fcntl(fd, F_GETFL, 0) 获取当前 FD 的状态标志（File Status Flags）。
    2. 通过 fcntl(fd, F_SETFL, statusFlags | O_NONBLOCK) 追加非阻塞标志，确保后续 Reactor 对该管道读写时绝不卡死主线程。
    3. 调用 fcntl(fd, F_GETFD, 0) 获取当前 FD 的描述符标志（File Descriptor Flags）。
    4. 通过 fcntl(fd, F_SETFD, descriptorFlags | FD_CLOEXEC) 追加 Close-on-exec 标志，确保子进程 execve 时自动关闭此父端管道 FD，彻底杜绝敏感 FD 在进程间泄露。
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

    if (fcntl(fd, F_SETFD, descriptorFlags | FD_CLOEXEC) < 0)
        return false;

    return true;
}

/*
函数：CgiHandler::_setupPipes
用途：创建父子进程双向通信管道，并配置父端管道 FD 为非阻塞等安全属性。
参数：
    - int pipe_to_parent[2]: 用于暂存“子进程 -> 父进程”管道的读写端 FD 数组。
    - int pipe_to_child[2] : 用于暂存“父进程 -> 子进程”管道的读写端 FD 数组。
返回值：
    - bool: 管道创建与属性配置成功返回 true；若任一步骤失败，则安全释放已申请的 FD 并返回 false。
实现逻辑或说明：
    1. 调用 ::pipe() 初始化 pipe_to_parent 与 pipe_to_child 两个物理管道。
    2. 若任一 pipe() 失败，关闭先前已打开的管道 FD，防止 FD 泄露并返回 false。
    3. 调用 configureParentPipeFd 设置 pipe_to_parent[0]（读端）和 pipe_to_child[1]（写端）为非阻塞模式（O_NONBLOCK）。
    4. 确保父进程后续交由 Reactor 轮询时，读写管道数据绝对不会阻塞主事件循环。
*/
/*
函数：CgiHandler::_executeChildProcess
用途：在 fork 出来的子进程独立空间中，完成 I/O 重定向、工作目录切换、环境变量重组与 CGI 镜像替换。
参数：
    - int childReadFd : 父进程写入 Body 数据的管道读端，用于重定向为子进程的 STDIN_FILENO。
    - int parentWriteFd: 子进程吐出脚本输出的管道写端，用于重定向为子进程的 STDOUT_FILENO。
返回值：
    - void（若 execve 调用成功，该函数永远不返回；若中途失败，通过 ::_exit(127) 物理结束子进程）。
实现逻辑或说明：
    1. 使用 dup2() 将 childReadFd 复制并重定向为 STDIN，随后关闭原 FD。
    2. 使用 dup2() 将 parentWriteFd 复制并重定向为 STDOUT，随后关闭原 FD。
    3. 解析脚本所在目录并调用 chdir() 切换工作目录，确保 CGI 脚本能正确读取同目录下的相对路径资产。
    4. 构建标准 CGI 环境变量数组（env），若构建失败则退出。
    5. 根据 _interpreter_path 是否为空，选择通过解释器（如 Python/PHP）或作为二进制可执行文件构建 execve 的 args 参数。
    6. 调用 ::execve 执行脚本。若 execve 失败，释放 env 内存并调用 ::_exit(127) 强制物理终止子进程，绝不让子进程倒流回父进程的代码逻辑。
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
    dprintf(2, "========== MY CGI CODE ==========\n");
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
            close(parentWriteFd);
            ::exit(127);
        }
        close(parentWriteFd);
    }
    std::string scriptDirectory = directoryName(_script_path);
    std::string scriptName = baseName(_script_path);
    if (chdir(scriptDirectory.c_str()) != 0)
    {
        ::exit(127);
    }
    char **env = _buildEnvironment();

    for (int i = 0; env[i]; ++i)
        dprintf(STDERR_FILENO, "%s\n", env[i]);
    // for (int i = 0; env[i]; i++)
    // {
    //    std::cout << env[i] << std::endl;
    //     // printf(env[i]);
    // }
    if (env == NULL)
    {
        ::exit(127);
    }
    char *args[3];
    if (!_interpreter_path.empty())
    {
        args[0] = const_cast<char *>(_interpreter_path.c_str());
        args[1] = const_cast<char *>(scriptName.c_str());
        args[2] = NULL;
        dprintf(STDERR_FILENO,
                "INTERPRETER=[%s]\n",
                args[0]);

        dprintf(STDERR_FILENO,
                "SCRIPT=[%s]\n",
                args[1]);
        std::cerr
            << "exists interpreter="
            << access(_interpreter_path.c_str(), X_OK)
            << std::endl;

        std::cerr
            << "errno="
            << strerror(errno)
            << std::endl;
        std::cerr << "args[0]=" << args[0] << std::endl;
        std::cerr << "args[1]=" << args[1] << std::endl;

        for (int i = 0; env[i]; i++)
        {
            std::cerr << "ENV[" << i << "]="
                      << env[i]
                      << std::endl;
        }
        ::execve(args[0], args, env);
    }
    else
    {
        std::string executable = "./" + scriptName;
        args[0] = const_cast<char *>(executable.c_str());
        args[1] = NULL;
        dprintf(STDERR_FILENO,
                "INTERPRETER=[%s]\n",
                args[0]);

        dprintf(STDERR_FILENO,
                "SCRIPT=[%s]\n",
                args[1]);
        ::execve(args[0], args, env);
    }
    perror("execve failed");
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
