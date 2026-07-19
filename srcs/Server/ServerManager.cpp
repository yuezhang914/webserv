#include "ServerManager.hpp"
#include <iostream>

// 🎯 【高光守则】：构造函数前面绝对没有任何 void 或者是返回值类型！
ServerManager::ServerManager(const std::vector<ServerConfig> &configs)
    : _server_configs(configs) // 优雅落盘：初始化列表完美注入物理配置资产
{
    // 💡 这里可以用初始化的资产做点温馨的冷启动日志
    std::cout << "[ServerManager] WebServ engine pre-loaded with "
              << _server_configs.size() << " virtual servers." << std::endl;
}

// 🎯 【析构车间】：斩断所有堆上开辟的服务器物理套接字指针
ServerManager::~ServerManager()
{
    for (size_t i = 0; i < this->_listen_sockets.size(); ++i)
    {
        if (this->_listen_sockets[i] != NULL)
        {
            delete this->_listen_sockets[i];
        }
    }
    this->_listen_sockets.clear();
    std::cout << "[ServerManager] Engine completely shutdown and memory released." << std::endl;
}
/**
 * @brief 服务器冷启动一键初始化入口
 *
 * @note 作为 setupSockets() 的对外包装接口，起到语义隔离作用。
 */
void ServerManager::init()
{
    std::cout << "[ServerManager] Initializing network sockets..." << std::endl;
    this->setupSockets();
}

/**
 * @brief 网络套接字物理装配车间
 *
 * @details
 * 1. 遍历所有传入的虚拟服务器配置，执行【同端口去重防御机制】。
 *    防止多个具有不同 server_name 但端口相同的虚拟主机重复绑定（bind）同一端口而导致崩溃。
 * 2. 工厂化动态创建 ServerSocket 实例，并调用其 setup() 开启内核监听。
 * 3. 获取物理监听 listenFd，在大管家的 _listen_socket_map 字典中做好账本登记。
 * 4. 物理组装 struct pollfd 结构体，注册 POLLIN 读事件，并压入 _poll_fds 监听阵列。
 */
void ServerManager::setupSockets()
{
    std::vector<int> handled_ports;

    for (size_t i = 0; i < _server_configs.size(); ++i)
    {
        int port = _server_configs[i].port;
        std::string host = _server_configs[i].host;

        // 物理去重探测
        bool port_duplicate = false;
        for (size_t p = 0; p < handled_ports.size(); ++p)
        {
            if (handled_ports[p] == port)
            {
                port_duplicate = true;
                break;
            }
        }
        if (port_duplicate)
        {
            std::cout << "[ServerManager] Multi-server configuration detected for port " << port << " (Skipping duplicate bind)" << std::endl;
            continue;
        }

        // 工厂实例化
        ServerSocket *srv_sock = new ServerSocket(host, port);
        srv_sock->setup();
        int listenFd = srv_sock->getFd();
        std::cout << "[ServerManager] Successfully listening on " << host << ":" << port << " (FD: " << listenFd << ")" << std::endl;

        this->_listen_sockets.push_back(srv_sock);
        handled_ports.push_back(port);
        _listen_socket_map[listenFd] = _server_configs[i];

        // 挂载 poll 哨兵
        struct pollfd pfd;
        pfd.fd = listenFd;
        pfd.events = POLLIN;
        pfd.revents = 0;
        _poll_fds.push_back(pfd);
    }
}

/**
 * @brief 垃圾回收车间：在执行 poll 等待前，物理清洗已被标记死亡的 FD
 *
 * @details
 * 在事件派发阶段，由于客户端挂断或异常，部分连接对应的 fd 可能会被直接置为安全哨兵值 -1。
 * 为了防止 poll 监听阵列不断膨胀以及无效扫描，此函数采用原地移位删除，
 * 彻底清除所有已经被标记为 -1 的无效节点，保持核心事件阵列的高效与清爽。
 */
void ServerManager::prePollCleanup()
{
    for (size_t i = 0; i < this->_poll_fds.size();)
    {
        if (this->_poll_fds[i].fd == -1)
        {
            this->_poll_fds.erase(this->_poll_fds.begin() + i);
        }
        else
        {
            ++i;
        }
    }
}

/**
 * @brief 执行真正的内核多路复用 poll 等待
 *
 * @param retries poll 临时中断时的重试计数器引用
 * @return int
 *         - 正数：代表当前已就绪的活跃通道个数；
 *         - 0：由于 EINTR 信号等导致的中断，可允许外层安全 continue 重新唤醒；
 *         - -1：连续三次系统报错彻底失败，宣告主循环发生崩溃性熔断。
 *
 * @note
 * 最后一个参数传入 -1，表示采取无限阻塞挂起（Blocking wait），直至网线上有电信号波动。
 */
int ServerManager::executePoll(int &retries)
{
    int ret = poll(&this->_poll_fds[0], this->_poll_fds.size(), -1);

    if (ret < 0)
    {
        if (retries < 3)
        {
            retries++;
            return 0;
        }
        return -1;
    }

    retries = 0; // 成功捕获事件，物理复位防崩溃计数器
    return ret;
}

/*
函数用途：动态调整多路复用雷达网，一键拉起指定客户端套接字的写事件（POLLOUT）监听。
参数与变量：
- clientFd (传入参数)：目标执行升级动作的客户端物理套接字描述符。
- _poll_fds (类内部常驻容器)：存放所有交由系统 poll 监听的结构体阵列（std::vector<struct pollfd>）。
- i (局部变量)：探查推进进度数字标定。
实现逻辑：
1. 循环走访多路复用大厅当前挂载的所有 pollfd 结构体。
2. 逆向定位，查找到对应当前 clientFd 的那个物理雷达节点。
3. 🎯 【精确附着】：使用按位或运算符（|=），在保持原有读事件（POLLIN）等其他监听状态完好无损的前提下，强行将 POLLOUT 标志位并网通电！
4. 一旦匹配成功并改写，立刻斩断循环（break）优雅撤离，防止无谓的后续空跑内耗。
*/
void ServerManager::enableClientWriteEvent(int clientFd)
{
    size_t i = 0;
    while (i < this->_poll_fds.size())
    {
        if (this->_poll_fds[i].fd == clientFd)
        {
            // 🚀 【拉下大闸】：按位或追加写监听标志，通知操作系统：“这家伙有货要发了，能写时叫我！”
            this->_poll_fds[i].events |= POLLOUT;
            break;
        }
        ++i;
    }
}

/*
函数用途：判定当前就绪的 fd 是否属于 CGI 子进程与主进程通信的异步物理管道读/写端。
参数与变量：
- fd (传入参数)：多路复用大循环当前弹回、亟待辨别身份的就绪文件描述符。
- _cgi_fd_to_client_map (类内部常驻容器)：std::map<int, int>，记录“管道 fd -> 客户端 fd”的雷达映射。
实现逻辑：
1. 在 _cgi_fd_to_client_map 账本里执行红黑树查找。
2. 如果查到了末尾（end()），说明这不是 CGI 管道，返回 false。
3. 如果中途截获，说明该 fd 身上背着因果契约，返回 true！
*/
bool ServerManager::isCgiPipeFd(int fd)
{
    // 🎯 直接利用红黑树雷达一枪锁死，效率 O(log N) 极高！
    return this->_cgi_fd_to_client_map.find(fd) != this->_cgi_fd_to_client_map.end();
}


/*
函数用途：全量异步吞噬 CGI 管道读端弹回的就绪报文资产，并顺藤摸瓜将其缝合至目标客户端的写缓冲区。
参数与变量：
- cgiReadFd (传入参数)：当前触发 POLLIN、正在源源不断吐数据的 CGI 管道读端描述符。
- poll_idx (传入参数)：当前管道 fd 在 _poll_fds 阵列中的下标位置，方便收网后当场物理移除。
- clientFd (局部变量)：通过因果契约映射表反查出来的、眼巴巴等着回包的客户端底层套接字。
*/
void ServerManager::handleCgiPipeRead(int cgiReadFd, size_t poll_idx)
{
    // 1. 【顺藤摸瓜】：一枪反查因果契约，必须明确知道这管道是伺候哪个客户端的
    std::map<int, int>::iterator it = this->_cgi_fd_to_client_map.find(cgiReadFd);
    if (it == this->_cgi_fd_to_client_map.end())
    {
        // 孤儿管道，防卫性关闭
        ::close(cgiReadFd);
        this->_poll_fds.erase(this->_poll_fds.begin() + poll_idx);
        return;
    }
    int clientFd = it->second;

    // 2. 【物理开辟临时蓄水池】：每次非阻塞抽水 4096 字节
    char buffer[4096];
    ssize_t bytesRead = ::read(cgiReadFd, buffer, sizeof(buffer));

    if (bytesRead > 0)
    {
        // 🚀 【完美缝合】：把子进程吐出来的原生报文（含 CGI 响应头和 Body），直冲目标客户端的 write_buffer 尾部！
        this->_connections[clientFd].write_buffer.append(buffer, bytesRead);
        std::cout << "[CGI Reader] Sucked " << bytesRead << " bytes from pipe fd " << cgiReadFd << " to client " << clientFd << std::endl;
    }
    else if (bytesRead == 0)
    {
        // 🏁 【大功告成：读到 EOF】 说明子进程已经把货全部吐完了！开始清场交接：
        std::cout << "[CGI Reader] Reached EOF for pipe fd " << cgiReadFd << ". Transitioning client " << clientFd << " to write." << std::endl;

        // A. 解除多路复用雷达网上对该管道的监听，并将管道物理关闭（斩断物理内耗）
        ::close(cgiReadFd);
        this->_poll_fds.erase(this->_poll_fds.begin() + poll_idx);
        this->_cgi_fd_to_client_map.erase(it);

        // B. 🚀 【拉下客户端写大闸】：既然货已经全部在 write_buffer 里躺着了，立刻拉起写事件，下一轮 poll 自动发货！
        this->enableClientWriteEvent(clientFd);

        // C. 如果 POST 的写端管道还没被清理，顺手也关闭并清理（确保安全无泄漏）
        if (this->_connections[clientFd].cgi_write_fd != -1)
        {
            // 在实际处理中，写端通常在数据喂完时就已经关闭。这里做一层防卫
            for (size_t j = 0; j < this->_poll_fds.size(); ++j)
            {
                if (this->_poll_fds[j].fd == this->_connections[clientFd].cgi_write_fd)
                {
                    this->_poll_fds.erase(this->_poll_fds.begin() + j);
                    break;
                }
            }
            ::close(this->_connections[clientFd].cgi_write_fd);
            this->_connections[clientFd].cgi_write_fd = -1;
        }

        // D. 彻底释放子进程资源，防止僵尸进程（Zombie Process）霸占系统进程表
        if (this->_connections[clientFd].cgi_pid > 0)
        {
            int status;
            // 使用 WNOHANG 非阻塞回收，或者子进程既然 EOF 了，直接 waitpid 瞬间回收
            ::waitpid(this->_connections[clientFd].cgi_pid, &status, 0);
            this->_connections[clientFd].cgi_pid = -1;
        }
        this->_connections[clientFd].is_cgi = false; // 清除 CGI 运行态印记
    }
    else
    {
        // 🚨 遇到底层读取错误（如 EINTR 以外的异常）
        if (errno != EAGAIN && errno != EWOULDBLOCK)
        {
            std::cerr << "[CGI Reader] System read error on fd " << cgiReadFd << ", breaking conduit." << std::endl;
            // 降维回执 500 熔断
            this->_connections[clientFd].response.createResponse(500, "CGI Read Error", this->_connections[clientFd].config.error_pages);
            this->enableClientWriteEvent(clientFd);

            // 清理管道与进程
            ::close(cgiReadFd);
            this->_poll_fds.erase(this->_poll_fds.begin() + poll_idx);
            this->_cgi_fd_to_client_map.erase(it);
            if (this->_connections[clientFd].cgi_pid > 0)
            {
                ::kill(this->_connections[clientFd].cgi_pid, SIGKILL); // 强行超度
                ::waitpid(this->_connections[clientFd].cgi_pid, NULL, 0);
            }
        }
    }
}

/**
 * @brief 网络事件多路精准派发车间
 *
 * @details
 * 💡【核心防错法设计 ──── 逆向倒序扫描安全算法】：
 * 传统的正序扫描 (0 -> size-1) 在事件处理中极为危险。如果在处理某个 FD 读写时触发了断连
 * 并将其从 vector 中 erase 抹除，正序扫描会导致后续所有元素的物理索引瞬间前移，
 * 从而直接发生“越界踩空崩溃 (Segment Fault)”或“无端漏掉下一个就绪事件”。
 *
 * 此处采用【从 size 递减至 1】的逆向扫描判定，无论前面的元素如何销毁重组，
 * 均不影响当前扫描位置（idx）左侧的元素稳定性。
 *
 * 1. 黄金拦截：跳过被其他子函数置为 -1 的死线，或无任何就绪信号（revents == 0）的闲置通道。
 * 2. 分支 A (异常拦截)：若捕获 POLLERR/POLLHUP/POLLNVAL 等物理断裂信号，
 *    根据套接字类型，优雅注销监听端或调用 closeConnection 销毁客户端。
 * 3. 分支 B (读事件分流)：若有 POLLIN，通过 isListenFd 判定：
 *    - 是监听端口 ──► “迎宾通道”，调用 acceptNewConnection 物理接纳新客。
 *    - 是普通客户端 ──► “数据通道”，调用 handleClientRead 接管网线读取。
 * 4. 分支 C (写事件派发)：若捕获 POLLOUT ──► “发送通道”，调用 handleClientWrite 倾倒缓存区响应数据。
 */
void ServerManager::dispatchEvents()
{
    for (size_t i = this->_poll_fds.size(); i > 0; --i)
    {
        size_t idx = i - 1;

        if (this->_poll_fds[idx].fd == -1 || this->_poll_fds[idx].revents == 0)
            continue;

        int activeFd = this->_poll_fds[idx].fd;

        if (this->_poll_fds[idx].revents & (POLLERR | POLLHUP | POLLNVAL))
        {
            if (this->isListenFd(activeFd))
            {
                std::cerr << "[ServerManager] CRITICAL: Fatal event (" << this->_poll_fds[idx].revents
                          << ") on Listen FD " << activeFd << "!" << std::endl;
                this->_poll_fds[idx].fd = -1;
            }
            else
            {
                this->closeConnection(activeFd, idx);
            }
            continue;
        }

        if (this->_poll_fds[idx].revents & POLLIN)
        {
            if (this->isListenFd(activeFd))
                this->acceptNewConnection(activeFd);
            else
                this->handleClientRead(activeFd, idx);
        }
        else if (this->_poll_fds[idx].revents & POLLOUT)
        {
            this->handleClientWrite(activeFd, idx);
        }
    }
}

void ServerManager::registerFdToPoll(int fd, short events)
{
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = events; // 传入 POLLIN
    pfd.revents = 0;     // 清空内核回执，防止幽灵触发

    this->_poll_fds.push_back(pfd); // 正式入籍大循环名册

    std::cout << "[ServerManager] FD " << fd << " successfully registered to poll tree." << std::endl;
}

/**
 * @brief 开启 Web 服务的时空奇点 ─── 运行大管家主循环
 *
 * @details
 * 启动后，程序将彻底进入单线程下的多路复用死循环（Entering the matrix...），
 * 依靠 prePollCleanup() -> executePoll() -> dispatchEvents()
 * 三个子车间的环环相扣，提供极其高效、低延迟的高并发 Web 静态、动态服务。
 */
void ServerManager::run()
{
    if (this->_poll_fds.empty())
        return;
    std::cout << "[ServerManager] Main loop started. Entering the matrix..." << std::endl;

    int poll_error_retries = 0;
    while (true)
    {
        this->prePollCleanup();

        int ret = this->executePoll(poll_error_retries);
        if (ret < 0)
            break;

        this->dispatchEvents();
    }
}