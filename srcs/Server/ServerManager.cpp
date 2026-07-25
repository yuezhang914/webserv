#include "Webserv.hpp"

#include <iostream>

// 构造函数前面绝对没有任何 void 或者是返回值类型！
ServerManager::ServerManager(const std::vector<ServerConfig> &configs)
    : _server_configs(configs) // 优雅落盘：初始化列表完美注入物理配置资产
{
    // 💡 这里可以用初始化的资产做点温馨的冷启动日志
    std::cout << "[ServerManager] WebServ engine pre-loaded with "
              << _server_configs.size() << " virtual servers." << std::endl;
}

// 斩断所有堆上开辟的服务器物理套接字指针
ServerManager::~ServerManager()
{
    // 1. 物理释放所有监听套接字（ServerSocket*）
    for (size_t i = 0; i < this->_listen_sockets.size(); ++i)
    {
        if (this->_listen_sockets[i] != NULL)
        {
            delete this->_listen_sockets[i];
        }
    }
    this->_listen_sockets.clear();

    // 2. 💡 物理释放所有残留的客户端连接（Connection*）
    for (std::map<int, Connection *>::iterator it = this->_connections.begin();
         it != this->_connections.end(); ++it)
    {
        if (it->second != NULL)
        {
            delete it->second; // 触发 Connection 析构，清理内部 Socket 与残存管道
        }
    }
    this->_connections.clear();

    std::cout << "[ServerManager] Engine completely shutdown and memory released." << std::endl;
}

/*
函数用途：物理拉起 Webserv 网络的奠基点火仪式（巢穴孵化），全量启动底层服务器级网络套接字的物理实例化。
参数与变量：
- setupSockets (类内部核心函数)：专职读取配置文件、循环创建 Socket、绑定端口（bind）并将其切入被动监听状态（listen）的基建车间。
实现逻辑：
1. 宣告主权：向控制台抛出高亮初始化日志，宣告大管家网络骨架正式破土动工。
2. 物理孵化：果断向下调用 setupSockets 核心基建车间，将配置文件里规划的所有虚拟端口，全量具象化为操作系统的 ListenFD（监听套接字），
   并手工将这些元老级 FD 编入全局轮询名册（_poll_fds），为后续 run() 引擎进入多路复用矩阵焊死第一块钢铁基石！
*/
void ServerManager::init()
{
    std::cout << "[ServerManager] Initializing network sockets..." << std::endl;
    this->setupSockets();
    // 💡 物理告诉内核：所有子进程死后请直接自动销毁，不要留 Zombie！
    ::signal(SIGCHLD, SIG_IGN);
}

/*
函数用途：全量解析服务器配置名册，物理孵化各端口的非阻塞监听套接字（ListenFD），并作为元老级哨兵首批编入多路复用大循环。
参数与变量：
- _server_configs (类内部常驻容器)：std::vector<ServerConfig>，由配置文件加载而来的全量虚拟主机配置蓝图。
- handled_ports (局部暂存容器)：std::vector<int>，运行时动态端口物理去重探测账本，严防重复 bind 导致系统血崩。
- srv_sock (局部指针变量)：ServerSocket*，Socket 封装工厂实体，负责底层的 socket()、setsockopt()、bind() 和 listen() 铁血四部曲。
- _listen_sockets (类内部常驻容器)：std::vector<ServerSocket*>，大管家物理持有的基础套接字资产舱。
- _listen_socket_map (类内部常驻容器)：std::map<int, ServerConfig>，专职记录“监听 FD -> 专属配置”的因果反查账本。
- _poll_fds (类内部常驻容器)：std::vector<struct pollfd>，大管家赖以生存的多路复用全局核心轮询监视名册。
实现逻辑：
1. 物理去重防线：纵向遍历配置名册，提取端口与 host。拿着当前端口突袭 handled_ports 账本，若发现多主机共享同一端口，则优雅跳过重复绑定，实现多虚拟主机并网。
2. 工厂实例化：未命中的新端口交由 ServerSocket 工厂物理孵化，一枪执行内部 setup()。将其切入非阻塞被动监听状态，宣告本地网络主权。
3. 账本入籍与反查绑定：将生成的 ListenFD 塞进资产舱，同时在 _listen_socket_map 账本里留下因果烙印，确保新进线客户端能精准识别自己归属于哪台虚拟主机。
4. 挂载元老级哨兵：实例化 struct pollfd，注入 POLLIN 读雷达方向，物理清空回执层，将其作为地基骨架 push_back 灌入核心名册，正式拉开帝国防御网序幕！
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

/*
函数用途：在拉起多路复用大闸前执行前置洗舱，全量剔除名册中已被打上 -1 熔断标记的废弃 FD，完成名册物理瘦身。
参数与变量：
- _poll_fds (类内部常驻容器)：std::vector<struct pollfd>，大管家赖以生存的多路复用全局核心轮询监视名册。
实现逻辑：
1. 动态变长正序扫描：纵向遍历全局轮询名册 _poll_fds。为了完美应对 vector 执行 erase 擦除导致的元素整体物理前移，
   必须在指针平移上采用“不进则删”的变长控制策略。
2. 物理销户与名册瘦身：若发现当前节点的 fd 已经沦为 -1（说明它已在之前的 handle 业务或熔断车间中宣告破产并关闭），
   则果断将其从 vector 名册中物理擦除，且索引 i 保持原地不动（用来精准迎击前移上来的下一个新节点）；否则，i 自增推进。
3. 筑坝防线：此举确保下一轮 executePoll 调用 ::poll 时，喂给 Linux 内核的名册全部由百分之百健康的活跃 FD 组成，
   彻底封杀因为带入脏 FD 导致内核疯狂报错、CPU 空转忙轮询的性能隐患。
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

/*
函数用途：物理拉起多路复用核心轮询大闸（::poll），并用分级弹性防线死守信号中断带来的系统震荡。
参数与变量：
- retries (传入引用参数)：外部主循环托付的防御性崩溃计数器，用来记录连续发生系统中断的次数。
- _poll_fds (类内部常驻容器)：std::vector<struct pollfd>，大管家赖以生存的全局多路复用核心轮询监视名册。
- ret (局部变量)：Linux 内核本次弹回的就绪事件总数，或代表负面危机的系统级错误代码。
实现逻辑：
1. 传入 -1 物理阻塞参数，强行命令主线程让出 CPU，陷入非阻塞全天候休眠，直到雷达名册中任意 FD 触发就绪。
2. 熔断危机拦截：若 poll 返回负数报错，启动分级弹性防线。在 3 次以内视作常规信号（如 EINTR）引发的轻微震荡，
   计数器自增并安全返回 0，迫使主循环无感刷新；一旦跨过 3 次红线则触发彻底熔断，断开危机大闸返回 -1。
3. 黄金复位并网：若成功捕捉到哪怕一字节的有效网络事件，立刻将崩溃计数器强行物理复位归零，重新开启帝国防御！
*/
int ServerManager::executePoll(int &retries)
{
    int ret = ::poll(&this->_poll_fds[0], this->_poll_fds.size(), 1000);

    if (ret < 0)
    {
        // 💡 1. 显式捕获 EINTR 信号打断：直接算作正常暂态，重置/不计入致命错误 retry
        if (errno == EINTR)
        {
            return 0; // 信号打断，安全跳过，下一轮继续 poll
        }

        // 💡 2. 真正的其他致命底层 poll 错误（如 EFAULT, EINVAL），触发弹性重试
        if (retries < 3)
        {
            std::cerr << "[executePoll] Warning: poll() system call failed with errno "
                      << errno << ", retrying (" << retries + 1 << "/3)..." << std::endl;
            retries++;
            return 0;
        }

        std::cerr << "[executePoll] Fatal: poll() failed consecutively over limit! Terminating main loop." << std::endl;
        return -1; // 连续 3 次致命错误，彻底熔断
    }

    retries = 0; // 成功捕获事件或超时，物理复位防崩溃计数器
    return ret;
}

/*
函数用途：将新生的物理文件描述符（Socket 或 Pipe）正式编入多路复用大循环，开启全天候事件监控。
参数与变量：
- fd (传入参数)：刚破壳而出（通过 accept 或 pipe 物理孵化）且亟待编入帝国防御网的底层文件描述符。
- events (传入参数)：预设的监控雷达事件方向，如代表接收数据的 POLLIN，或代表高效发货的 POLLOUT。
- pfd (局部变量)：系统原生 struct pollfd 结构体，作为大管家与 Linux 内核进行主权沟通的物理载体。
- _poll_fds (类内部常驻容器)：std::vector<struct pollfd>，大管家赖以生存的全局多路复用核心轮询监视名册。
实现逻辑：
1. 实例化一个物理 pollfd 节点，绑定目标文件描述符，锁定数据对流通道。
2. 灌入指定的监听事件类型（events），并手工强行将内核回执层（revents）洗白清零，彻底封杀幽灵事件的误触触发。
3. 执行 push_back 操作让该资产在 _poll_fds 名册里物理入籍，在下一轮 poll 轮询中正式享受多路复用全量护航！
*/
void ServerManager::registerFdToPoll(int fd, short events)
{
    if (fd < 0)
    {
        std::cerr << "[ServerManager] Error: Attempted to register invalid negative FD: " << fd << std::endl;
        return;
    }

    // 1. 防重复注册防线：检查该 FD 是否已经在 _poll_fds 名册中
    for (size_t i = 0; i < this->_poll_fds.size(); ++i)
    {
        if (this->_poll_fds[i].fd == fd)
        {
            std::cout << "[ServerManager] Notice: FD " << fd << " already registered in poll tree. Updating events instead." << std::endl;
            this->_poll_fds[i].events = events; // 存在则直接覆写事件掩码
            this->_poll_fds[i].revents = 0;
            return;
        }
    }

    // 2. 正常入籍
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = events; // 传入 POLLIN / POLLOUT
    pfd.revents = 0;     // 清空内核回执，彻底杜绝幽灵触发

    this->_poll_fds.push_back(pfd); // 正式入籍大循环名册

    std::cout << "[ServerManager] FD " << fd << " successfully registered to poll tree with events: " << events << std::endl;
}

bool ServerManager::isCgiReadFd(int fd) const
{
    return this->_cgi_read_fd_to_client_map.find(fd) != this->_cgi_read_fd_to_client_map.end();
}

bool ServerManager::isCgiWriteFd(int fd) const
{
    return this->_cgi_write_fd_to_client_map.find(fd) != this->_cgi_write_fd_to_client_map.end();
}

/*
函数用途：作为多路复用核心事件分发中枢（Radar 引擎），铁血调度倒序卡尺，将内核弹回的物理事件精准切片并安全分流至各核心车间。
参数与变量：
- activeFd (局部变量)：当前正在接受审查、从全局轮询名册中提取出来的活跃物理文件描述符。
- revents (局部变量)：Linux 内核本次轮询后如实回执的物理就绪事件位掩码（如 POLLIN、POLLOUT、POLLHUP）。
- idx (局部变量)：当前处理的 FD 在 _poll_fds 名册中的物理倒序索引。
- _poll_fds (类内部常驻容器)：std::vector<struct pollfd>，大管家赖以生存的多路复用全局核心轮询监视名册。
实现逻辑：
1. 铁血倒序卡尺防线：采用自后向前的 i-- 倒序遍历机制。完美规避了在分流车间（如 closeConnection）执行动态擦除 vector 元素时，因数据整体前移导致的索引塌陷与 FD 跳过天坑；并就地挂载“物理销户拦截”防御线。
2. 异常/挂断收网分流 (ERR/HUP/NVAL)：
   - 若属于 CGI 管道描述符，即便挂断也必须坚决扭送 handleCgiPipeRead 车间，启动“落幕清仓阶段”，捞干内核缓冲区最后一瓢残渣。
   - 若属于监听套接字（ListenFD）则触发 CRITICAL 警告并安全标记；普通客户端套接字则果断打入 closeConnection 执行销户。
3. 读事件就绪分流 (POLLIN)：
   - CGI 管道读端就绪：分流至 handleCgiPipeRead 启动异步抽水循环。
   - 监听套接字就绪：分流至 acceptNewConnection 物理孵化新进线连接。
   - 普通客户端就绪：分流至 handleClientRead 强攻非阻塞 Socket 捞干内核接收蓄水池。
4. 写事件就绪分流 (POLLOUT)：
   - 在跨过 POLLIN 动作后，原地挂载“生存卡尺防线”，重新校验 idx 与 activeFd 的一致性，严防前置步骤导致的名册动态置换带来的越界血崩。
   - 校验通过后，将 CGI 写端管道精准分流至 handleCgiPipeWrite（卡尺切片喂食 POST Body）；普通客户端分流至 handleClientWrite 执行发件箱一枪弹射发货。
*/
void ServerManager::handleCgiRead(int cgiReadFd)
{
    CgiEventResult res = this->_cgiManager.handlePipeRead(cgiReadFd);

    if (res.status == CGI_FINISHED)
    {
        // 1. 擦除雷达映射与 pollfd 挂载
        this->_cgi_read_fd_to_client_map.erase(cgiReadFd);
        this->eraseFdFromPoll(cgiReadFd);

        // 2. 找到对应的 Client Connection 渲染 Response 并恢复发送
        Connection *conn = this->_connections[res.clientFd];
        if (conn)
        {
            // 💡 解析 CGI 吐出的 rawOutput (含 Content-type header) 并构造 Response
            Response cgiResponse = buildCgiResponse(conn->request, res.rawOutput);
            conn->response = cgiResponse;
            conn->write_buffer = cgiResponse.responseToString();
            conn->close_after_write = true;

            // 恢复客户端 Socket 的 POLLOUT 监听，准备把数据发给客户端！
            this->setClientEvents(res.clientFd, POLLOUT);
        }
    }
    else if (res.status == CGI_ERROR)
    {
        this->_cgi_read_fd_to_client_map.erase(cgiReadFd);
        this->eraseFdFromPoll(cgiReadFd);

        Connection *conn = this->_connections[res.clientFd];
        if (conn)
        {
            conn->response.createResponse(res.statusCode, "CGI Output Error", conn->config.error_pages);
            conn->write_buffer = conn->response.responseToString();
            conn->close_after_write = true;
            this->setClientEvents(res.clientFd, POLLOUT);
        }
    }
}

void ServerManager::handleCgiWrite(int cgiWriteFd)
{
    CgiEventResult res = this->_cgiManager.handlePipeWrite(cgiWriteFd);

    if (res.status == CGI_ERROR)
    {
        this->_cgi_write_fd_to_client_map.erase(cgiWriteFd);
        this->eraseFdFromPoll(cgiWriteFd);

        Connection *conn = this->_connections[res.clientFd];
        if (conn)
        {
            conn->response.createResponse(res.statusCode, "CGI Input Write Error", conn->config.error_pages);
            conn->write_buffer = conn->response.responseToString();
            conn->close_after_write = true;
            this->setClientEvents(res.clientFd, POLLOUT);
        }
    }
    else
    {
        // 🟢 如果 Body 写完，CgiManager 内部已经关闭了 cgiWriteFd，我们只需从 Reactor 注销这个 FD
        if (this->_cgi_write_fd_to_client_map.find(cgiWriteFd) != this->_cgi_write_fd_to_client_map.end())
        {
            // 检查如果底层 CgiManager 的 writeFd 已经置为 -1，说明写管道已优雅关闭
            this->_cgi_write_fd_to_client_map.erase(cgiWriteFd);
            this->eraseFdFromPoll(cgiWriteFd);
        }
    }
}

void ServerManager::dispatchEvents()
{
    // 🚀 保持精妙的倒序遍历（防御 Vector erase 导致的索引失效）
    for (size_t i = this->_poll_fds.size(); i > 0; --i)
    {
        size_t idx = i - 1;

        // 极端防卫：防止在前面几轮循环中已经被物理注销或越界的 FD
        if (idx >= this->_poll_fds.size() || this->_poll_fds[idx].fd == -1 || this->_poll_fds[idx].revents == 0)
            continue;

        int activeFd = this->_poll_fds[idx].fd;
        short revents = this->_poll_fds[idx].revents;

        if (revents == 0)
            continue;

        // ==================== 1. CGI 读管道事件分发 (CGI stdout -> Webserv) ====================
        if (this->isCgiReadFd(activeFd))
        {
            // 🎯 POLLHUP 必须走 handleCgiRead：管道里可能还有最后一批未读完的数据！
            if (revents & (POLLIN | POLLHUP))
            {
                std::cout << "[Radar] -> Routing CGI Read FD " << activeFd << " to handleCgiRead (POLLIN/POLLHUP)" << std::endl;
                this->handleCgiRead(activeFd);
            }
            else if (revents & (POLLERR | POLLNVAL))
            {
                std::cout << "[Radar] -> CGI Read FD " << activeFd << " hardware broken (ERR/NVAL). Forcing cleanup." << std::endl;
                
                int clientFd = this->_cgi_read_fd_to_client_map[activeFd];
                this->_cgi_read_fd_to_client_map.erase(activeFd);
                this->_cgiManager.removeTaskByClientFd(clientFd);
                this->eraseFdFromPoll(activeFd);

                Connection *conn = this->_connections[clientFd];
                if (conn)
                {
                    conn->response.createResponse(500, "CGI Read Pipe Error", conn->config.error_pages);
                    conn->write_buffer = conn->response.responseToString();
                    conn->close_after_write = true;
                    this->setClientEvents(clientFd, POLLOUT);
                }
            }
            continue;
        }

        // ==================== 2. CGI 写管道事件分发 (Webserv -> CGI stdin) ====================
        if (this->isCgiWriteFd(activeFd))
        {
            if (revents & POLLOUT)
            {
                std::cout << "[Radar] -> Routing CGI Write FD " << activeFd << " to handleCgiWrite (POLLOUT)" << std::endl;
                this->handleCgiWrite(activeFd);
            }
            else if (revents & (POLLERR | POLLHUP | POLLNVAL))
            {
                std::cout << "[Radar] -> CGI Write FD " << activeFd << " write end broken (ERR/HUP/NVAL). Forcing cleanup." << std::endl;
                
                int clientFd = this->_cgi_write_fd_to_client_map[activeFd];
                this->_cgi_write_fd_to_client_map.erase(activeFd);
                this->_cgiManager.removeTaskByClientFd(clientFd);
                this->eraseFdFromPoll(activeFd);

                Connection *conn = this->_connections[clientFd];
                if (conn)
                {
                    conn->response.createResponse(500, "CGI Write Pipe Error", conn->config.error_pages);
                    conn->write_buffer = conn->response.responseToString();
                    conn->close_after_write = true;
                    this->setClientEvents(clientFd, POLLOUT);
                }
            }
            continue;
        }

        // ==================== 3. 普通 Socket 异常事件挂起 (POLLERR / POLLHUP / POLLNVAL) ====================
        if (revents & (POLLERR | POLLHUP | POLLNVAL))
        {
            if (this->isListenFd(activeFd))
            {
                std::cerr << "[ServerManager] CRITICAL: Fatal event (" << revents
                          << ") on Listen FD " << activeFd << "!" << std::endl;
                this->_poll_fds[idx].fd = -1;
            }
            else
            {
                std::cout << "[Radar] -> Routing Client Socket FD " << activeFd << " to closeConnection (ERR/HUP)" << std::endl;
                this->closeConnection(activeFd, idx);
            }
            continue;
        }

        // ==================== 4. 普通 Socket 读事件就绪 (POLLIN) ====================
        if (revents & POLLIN)
        {
            if (this->isListenFd(activeFd))
            {
                std::cout << "[Radar] -> Routing Listen FD " << activeFd << " to acceptNewConnection (POLLIN)" << std::endl;
                this->acceptNewConnection(activeFd);
            }
            else
            {
                std::cout << "[Radar] -> Routing Client FD " << activeFd << " to handleClientRead (POLLIN)" << std::endl;
                this->handleClientRead(activeFd, idx);
            }
        }

        // ==================== 5. 普通 Socket 写事件就绪 (POLLOUT) ====================
        if (revents & POLLOUT)
        {
            if (idx >= this->_poll_fds.size() || this->_poll_fds[idx].fd != activeFd)
            {
                std::cout << "[Radar] Notice: FD " << activeFd << " vanished or swapped during POLLIN processing. Safe break." << std::endl;
                continue;
            }

            std::cout << "[Radar] -> Routing Client FD " << activeFd << " to handleClientWrite (POLLOUT)" << std::endl;
            this->handleClientWrite(activeFd, idx);
        }

        std::cout << "[Radar] --- TICK END for FD " << activeFd << " ---" << std::endl
                  << std::endl;
    }
}

/*
函数用途：物理启动 Webserv 核心主循环大闸（The Matrix），作为永动机总动力引擎，全天候驱动雷达轮询与业务分流。
参数与变量：
- poll_error_retries (局部变量)：int，专门托管给 executePoll 引擎的弹性防线计数器，用于防御系统信号震荡引起的偶发性中断。
- ret (局部变量)：int，每一轮轮询完毕后内核回执的就绪事件总数，若遭遇连续致命崩溃则表现为负数。
- _poll_fds (类内部常驻容器)：std::vector<struct pollfd>，大管家赖以生存的多路复用全局核心轮询监视名册。
实现逻辑：
1. 空舱拦截防线：进线首先点验核心名册。若发现没有任何套接字入籍（_poll_fds 为空），则当场安全熔断折返，严防引发空转血崩。
2. 永动机全天候死循环（while (true)）：
   -第一步（prePollCleanup）：在前置位清理上一轮循环中被标记为脏、死、或已过期的僵尸资产与长连接，保持核心底盘绝对纯净。
   - 第二步（executePoll）：物理拉起多路复用大闸，主线程让出 CPU 陷入非阻塞全天候休眠，并向外部引入弹性防线计数器保护。若返回负数（即连续打断超过 3 次红线），说明遭遇毁灭性灾难，果断强行跳出循环（break）安全撤退。
   - 第三步（dispatchEvents）：若成功捕获有效网络事件，立刻开启倒序卡尺，将事件精准分
        std::cerr << "[ServerManager] Error: No listening sockets in poll tree. Aborting run()." << std::endl;
        return;流至对应的读/写/异常/CGI 业务车间进行最终的弹射交货！
*/
void ServerManager::run()
{
    if (this->_poll_fds.empty())
    {
        std::cerr << "[ServerManager] Error: No listening sockets in poll tree. Aborting run()." << std::endl;
        return;
    }

    std::cout << "[ServerManager] Main loop started. Entering the matrix..." << std::endl;

    int poll_error_retries = 0;
    while (true) // 💡 提示：若有全局信号标志位（如 g_server_running），也可替换 while(true) 方便 Ctrl+C 优雅退出
    {
        // 1. 🧹 轮询前的账本与死链清理车间（如追加 fds_to_add 到 poll_fds）
        this->prePollCleanup();

        // 2. 📡 执行 1000ms 物理超时/阻塞轮询
        int ret = this->executePoll(poll_error_retries);
        if (ret < 0)
        {
            std::cerr << "[ServerManager] Fatal poll failure limit reached. Breaking main loop." << std::endl;
            break;
        }

        // 3. 核心分水岭：只有真的有 FD 就绪 (ret > 0) 时，才拉响雷达分发事件！
        if (ret > 0)
        {
            this->dispatchEvents();
        }

        // 4. ⏱️ 盘点车间 A：巡检 CGI 超时任务，统一渲染 504 Gateway Timeout 报错
        std::vector<CgiEventResult> timeouts = this->_cgiManager.checkTimeouts();
        for (size_t i = 0; i < timeouts.size(); ++i)
        {
            int clientFd = timeouts[i].clientFd;
            int statusCode = timeouts[i].statusCode; // 504

            // 从 CgiManager 侧与 Reactor 侧注销关联的管道 FD
            // （管道 FD 已在 checkTimeouts 内部被 close 和 erase，这里清洗 ServerManager 侧的反查雷达）
            std::map<int, int>::iterator it = this->_cgi_read_fd_to_client_map.begin();
            while (it != this->_cgi_read_fd_to_client_map.end())
            {
                if (it->second == clientFd)
                {
                    this->eraseFdFromPoll(it->first);
                    this->_cgi_read_fd_to_client_map.erase(it++);
                }
                else
                {
                    ++it;
                }
            }

            // 找到客户端连接，生成 504 响应并准备发货
            Connection *conn = this->_connections[clientFd];
            if (conn)
            {
                conn->response.createResponse(statusCode, "Gateway Timeout", conn->config.error_pages);
                conn->write_buffer = conn->response.responseToString();
                conn->close_after_write = true;
                this->setClientEvents(clientFd, POLLOUT);
            }
        }

        // 5. 🪓 盘点车间 B：纯非阻塞 WNOHANG 回收 CGI 僵尸进程
        this->_cgiManager.reapChildren();
    }

    std::cout << "[ServerManager] Main loop safely terminated." << std::endl;
}

void ServerManager::closeConnection(int clientFd, size_t pollIndex)
{
    // 💡 1. 物理强杀与清理该 clientFd 对应的 CGI 任务（CgiManager 内部自动完成 kill + close + erase）
    this->_cgiManager.removeTaskByClientFd(clientFd);

    // 💡 2. 擦除 ServerManager 侧的反查雷达映射（读端 & 写端）
    std::map<int, int>::iterator readIt = this->_cgi_read_fd_to_client_map.begin();
    while (readIt != this->_cgi_read_fd_to_client_map.end())
    {
        if (readIt->second == clientFd)
        {
            this->eraseFdFromPoll(readIt->first); // 顺便把读管道 FD 从 _poll_fds 中擦除
            this->_cgi_read_fd_to_client_map.erase(readIt++);
        }
        else
            ++readIt;
    }

    std::map<int, int>::iterator writeIt = this->_cgi_write_fd_to_client_map.begin();
    while (writeIt != this->_cgi_write_fd_to_client_map.end())
    {
        if (writeIt->second == clientFd)
        {
            this->eraseFdFromPoll(writeIt->first); // 顺便把写管道 FD 从 _poll_fds 中擦除
            this->_cgi_write_fd_to_client_map.erase(writeIt++);
        }
        else
            ++writeIt;
    }

    // 💡 3. 销毁 Connection 实体（RAII 析构触发 ::close(clientFd)）
    std::map<int, Connection *>::iterator it = this->_connections.find(clientFd);
    if (it != this->_connections.end())
    {
        Connection *connection = it->second;
        delete connection;
        this->_connections.erase(it);
    }

    // 💡 4. 抹去 poll 雷达网槽位（软置 -1 防索引抖动）
    if (pollIndex < this->_poll_fds.size() && this->_poll_fds[pollIndex].fd == clientFd)
    {
        this->_poll_fds[pollIndex].fd = -1;
        this->_poll_fds[pollIndex].events = 0;
        this->_poll_fds[pollIndex].revents = 0;
    }
    else
    {
        this->eraseFdFromPoll(clientFd);
    }

    std::cout << "[ServerManager] Client FD " << clientFd << " successfully closed and cleaned up." << std::endl;
}