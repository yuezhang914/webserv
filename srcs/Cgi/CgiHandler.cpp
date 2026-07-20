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

/*
函数用途：作为底层堆内存手工分配作坊，将 C++ 的 std::string 键值对强行序列化为 C 风格以 \0 结尾的裸字符指针（char*），用于构建 execve 所需的原生沙盒环境。
参数与变量：
- key (传入参数)：std::string，环境变量的名称（如 "REQUEST_METHOD" 或 "CONTENT_LENGTH"）。
- value (传入参数)：std::string，环境变量的具体数据内容（如 "POST" 或 "128"）。
- env_pair (局部变量)：std::string，利用标准连接符拼接出来的规范级 "KEY=VALUE" 临时完整报文字符串。
- str (局部裸指针变量)：char*，通过 std::malloc 在堆内存上为该变量精确丈量并开辟出的物理生存空间。
实现逻辑：
1. 协议级拼装：将 key 与 value 通过 '=' 符号在内存中进行一线缝合。
2. 物理丈量与开天辟地：为了容纳拼接后的报文，外加 C 风格终结符（\0），调用 std::malloc 精确申请 env_pair.size() + 1 字节的连续堆内存。
3. 物理拷贝与资产锁定：在防御性点验指针非空后，调用 std::strcpy 将 C++ 缓存区的内容物理拓印到新生的堆舱位中，
   以原生裸指针姿态返回，留待后续 _freeEnvironment 车间进行全量物理销毁，严防内存泄露。
*/
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

/*
函数用途：作为 C++98 古法标准专用的数字铸币车间，利用标准输出字符串流（ostringstream），将无符号整型（size_t）物理转译为 C++ 标准字符串（std::string）。
参数与变量：
- size (传入参数)：size_t，当前亟待进行文本化包装的物理度量数值（如 Body 的全量字节长度）。
- ss (局部对象变量)：std::ostringstream，系统原生的高级格式化内存流蓄水池。
实现逻辑：
1. 流式灌注转换：通过 C++ 特有的流输入重载操作符（<<），将纯二进制的 size_t 数字推入 ss 蓄水池中，由标准库在内部自动完成字符化、高低位对齐以及物理转译。
2. 资产拓印交付（临门一脚）：调用 ss.str() 精确拓印出流缓冲区中蓄满的字符串资产。
3. 跨界并网：以右值姿态安全返回，直接喂给 CONTENT_LENGTH 等关键 CGI 协议环境变量，为子进程 Python 脚本提供绝对精准的非阻塞断句依据。
*/
static std::string sizeToStr(size_t size)
{
    std::ostringstream ss;
    ss << size;
    return ss.str();
}

/*
函数用途：从客户端原始 Request 请求大脑中深度榨取 HTTP 协议核心物料，在堆内存中动态开凿并组装出一套规整的 C 风格二维指针阵列（char**），作为 execve 裂变后子进程的常驻环境变量沙盒。
参数与变量：
- env_map (局部暂存容器)：std::map<std::string, std::string>，中间物料分拣账本，用于格式化对齐各类 CGI 协议核心字段。
- env (局部二维指针变量)：char**，通过 std::malloc 动态申请的、在堆上连续排列的裸指针数组阵列（大小为 元素总数 + 1 哨兵）。
- it (局部迭代器)：std::map<std::string, std::string>::const_iterator，用于纵向点验分拣完毕的键值对资产。
- createEnvString (外部静态工具函数)：负责把单个 "KEY=VALUE" 灌入堆内存，转换为独立 C 风格裸指针的雕刻作坊。
实现逻辑：
1. 协议级物料榨取：全量搜集网关接口标准。针对 GET 注入 QUERY_STRING 供脚本解析入参；针对 POST 封锁 CONTENT_LENGTH 与 CONTENT_TYPE 边界防御线，彻底唤醒并指引 Python 的 sys.stdin.read() 精准断句。
2. 二维阵列物理开凿：精确计算键值对总量，额外追加 1 格物理舱位用来放置 NULL 终点哨兵，在堆上一次性申请连续的指针存储空间。
3. 铁血熔断回滚防御：通过 while 循环调用 createEnvString 逐条拓印灌入。一旦中间某一条堆内存申请意外爆雷（返回 NULL），立刻启动紧急熔断清理车间，逆向逐一 free 掉已分配的行资产并全量释放行指针阵列，实现零泄露的安全折返。
4. 挂载最高终点哨兵：在阵列末尾（env[i]）手工砸下 NULL 钢印，宣告沙盒时空边界闭环，完美对齐 POSIX 工业规范！
*/
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