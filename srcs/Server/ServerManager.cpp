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
    // 1. 【顺藤摸瓜】：反查因果契约
    std::map<int, int>::iterator it = this->_cgi_fd_to_client_map.find(cgiReadFd);
    if (it == this->_cgi_fd_to_client_map.end())
    {
        ::close(cgiReadFd);
        this->_poll_fds.erase(this->_poll_fds.begin() + poll_idx);
        return;
    }
    int clientFd = it->second;
    Connection &conn = this->_connections[clientFd];

    char buffer[4096];

    // 🚀 2. 【核心升级：龙卷风抽水循环】
    while (true)
    {
        ssize_t bytesRead = ::read(cgiReadFd, buffer, sizeof(buffer));

        if (bytesRead > 0)
        {
            // 🟢 完美缝合原生报文到客户端的暂存箱中（这里保持安全追加）
            conn.write_buffer.append(buffer, bytesRead);
            std::cout << "[CGI Reader] Sucked " << bytesRead << " bytes from pipe fd " << cgiReadFd << " to client " << clientFd << std::endl;
            continue;
        }
        else if (bytesRead == 0)
        {
            // 🏁 【大功告成：子进程吐货完毕（EOF）】
            std::cout << "[CGI Reader] Reached EOF for pipe fd " << cgiReadFd << "." << std::endl;

            // ============================================================
            // ✨ ✨ ✨ 🚀 黄金清洗并网安全车间 🚀 ✨ ✨ ✨
            // ============================================================
            // 1. 先把当前完全抽干的原生 CGI 脏报文提取出来
            std::string cgi_raw_data = conn.write_buffer;

            // 2. 扔进整形清洗车间，让它去剥离 Status、自动丈量 Content-Length
            this->parseAndFormatCgiResponse(cgi_raw_data);

            // 3. 🎯 【物理净化隔离】：清空发件箱，把整形完的高级满血报文一次性安全灌入！
            std::string().swap(conn.write_buffer); // 强力清空旧内存
            conn.write_buffer = cgi_raw_data;      // 换装新资产

            // 解除多路复用雷达网上对该管道的监听，并将管道物理关闭
            ::close(cgiReadFd);
            this->_poll_fds.erase(this->_poll_fds.begin() + poll_idx);
            this->_cgi_fd_to_client_map.erase(it);

            // 🚀 货齐了，拉起客户端写事件，下一轮大循环完美发货！
            this->enableClientWriteEvent(clientFd);

            // 清理 POST 写端管道的残留资产
            if (conn.cgi_write_fd != -1)
            {
                for (size_t j = 0; j < this->_poll_fds.size(); ++j)
                {
                    if (this->_poll_fds[j].fd == conn.cgi_write_fd)
                    {
                        this->_poll_fds.erase(this->_poll_fds.begin() + j);
                        break;
                    }
                }
                ::close(conn.cgi_write_fd);
                conn.cgi_write_fd = -1;
            }

            // 彻底回收子进程，防止僵尸进程
            if (conn.cgi_pid > 0)
            {
                int status;
                ::waitpid(conn.cgi_pid, &status, 0);
                conn.cgi_pid = -1;
            }
            conn.is_cgi = false;
            return; // 🎯 清场完毕，优雅退出
        }
        else
        {
            // 🚨 bytesRead == -1：遇到了底层的分水岭
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                break; // 内核缓冲区抽干，正常退出，等下一次数据来
            }
            if (errno == EINTR)
            {
                continue; // 被信号打断，继续抽
            }

            // ❌ 发生真正的系统级读取错误，切入 500 熔断
            std::cerr << "[CGI Reader] System read error on fd " << cgiReadFd << ", breaking conduit." << std::endl;

            conn.response.createResponse(500, "CGI Read Error", conn.config.error_pages);

            // 💡 降维清除脏缓存，确保 500 报错报文可以干净地下发
            std::string().swap(conn.write_buffer);
            conn.write_buffer = conn.response.responseToString();

            this->enableClientWriteEvent(clientFd);

            ::close(cgiReadFd);
            this->_poll_fds.erase(this->_poll_fds.begin() + poll_idx);
            this->_cgi_fd_to_client_map.erase(it);
            if (conn.cgi_pid > 0)
            {
                ::kill(conn.cgi_pid, SIGKILL);
                ::waitpid(conn.cgi_pid, NULL, 0);
                conn.cgi_pid = -1;
            }
            conn.is_cgi = false;
            return;
        }
    }
}

/*
函数用途：洗涤 CGI 子进程吐出来的原生不规范报文，提取 Status 头，全量拼装成浏览器能直接识别的满血 HTTP 报文。
参数：
- cgiOutput (传入并就地修改)：当前连接 write_buffer 里面躺着的 CGI 原生不规范字节流。
*/
void ServerManager::parseAndFormatCgiResponse(std::string &cgiOutput)
{
    // 1. 【寻找分水岭】：在原生报文中定位头部与 Body 的黄金边界线 (\r\n\r\n 或 \n\n)
    size_t header_end = cgiOutput.find("\r\n\r\n");
    size_t delimiter_len = 4;

    if (header_end == std::string::npos)
    {
        header_end = cgiOutput.find("\n\n");
        delimiter_len = 2;
    }

    // 🚨 极端安全防线：如果子进程吐出来的内容里连个空行都没有，说明是个完全畸形的输出
    if (header_end == std::string::npos)
    {
        // 强行把它当作纯 Body 或者是残缺报文，为其全量兜底打包 200
        std::stringstream ss;
        ss << "HTTP/1.1 200 OK\r\n"
           << "Server: Webserv/1.0\r\n"
           << "Content-Type: text/html\r\n"
           << "Content-Length: " << cgiOutput.size() << "\r\n"
           << "Connection: keep-alive\r\n\r\n"
           << cgiOutput;
        cgiOutput = ss.str();
        return;
    }

    // 2. 物理精准切割：把原始的 Headers 块和 Body 块在时空上割裂开来
    std::string raw_headers = cgiOutput.substr(0, header_end);
    std::string body = cgiOutput.substr(header_end + delimiter_len);

    // 3. 【状态雷达反查】：逐行扫描原始 Headers，揪出可能隐藏的 Status: 状态行
    std::string status_line = "HTTP/1.1 200 OK"; // 默认加冕 200 OK
    std::string clean_headers = "";

    std::stringstream header_stream(raw_headers);
    std::string line;
    while (std::getline(header_stream, line))
    {
        // 处理跨平台换行符残渣
        if (!line.empty() && line[line.size() - 1] == '\r')
            line.erase(line.size() - 1);

        if (line.empty())
            continue;

        // 🎯 降维打击拦截：如果发现了 "Status:" 特权头（大小写模糊匹配防线）
        if (line.size() >= 7 && (line.substr(0, 7) == "Status:" || line.substr(0, 7) == "status:"))
        {
            size_t value_start = line.find_first_not_of(" \t", 7);
            if (value_start != std::string::npos)
            {
                // 把脚本给的 "404 Not Found" 升级转换为 "HTTP/1.1 404 Not Found"
                status_line = "HTTP/1.1 " + line.substr(value_start);
            }
        }
        else
        {
            // 如果是普通的 Content-Type 或 Custom-Header，老老实实寄存在干净的头部箱子里
            clean_headers += line + "\r\n";
        }
    }

    // 4. 🚀 【满血回炉再造】：将所有资产按照国际航空级 HTTP 规范重新装订并网！
    std::stringstream final_packet;
    final_packet << status_line << "\r\n";     // 1. 标准 HTTP 状态首行
    final_packet << "Server: Webserv/1.0\r\n"; // 2. 大管家系统内政头
    final_packet << clean_headers;             // 3. 脚本自带的透传头（如 Content-Type）

    // 💡 很多写得粗糙的 CGI 脚本不会自己算长度，大管家在这里亲手帮它丈量并打上物理刚性防线：
    if (clean_headers.find("Content-Length:") == std::string::npos &&
        clean_headers.find("content-length:") == std::string::npos)
    {
        final_packet << "Content-Length: " << body.size() << "\r\n";
    }

    final_packet << "Connection: keep-alive\r\n\r\n"; // 4. 空行结束标志
    final_packet << body;                             // 5. 黄金 Body 数据挂载

    // 5. 满血资产安全覆盖，大功告成！
    cgiOutput = final_packet.str();
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

/*
函数用途：分批、非阻塞地将客户端 POST 请求的 Body 数据异步喂进 CGI 子进程的输入管道。
参数与变量：
- cgiWriteFd (传入参数)：当前触发 POLLOUT、代表内核缓冲区可写入的 CGI 管道写端描述符。
- poll_idx (传入参数)：当前管道在 _poll_fds 阵列中的倒序物理下标位置，方便喂饱后执行卸载清场。
*/
void ServerManager::handleCgiPipeWrite(int cgiWriteFd, size_t poll_idx)
{
    // 1. 【顺藤摸瓜】：一枪反查因果契约，明确知道这个写端管道是在伺候哪个客户端
    std::map<int, int>::iterator it = this->_cgi_fd_to_client_map.find(cgiWriteFd);
    if (it == this->_cgi_fd_to_client_map.end())
    {
        // 孤儿写端管道，防卫性物理关闭，从雷达网里擦除
        ::close(cgiWriteFd);
        this->_poll_fds.erase(this->_poll_fds.begin() + poll_idx);
        return;
    }
    int clientFd = it->second;
    Connection &conn = this->_connections[clientFd];
    const std::string &body = conn.request.getBody();

    // 2. 极端边界防御：如果进线时发现原本就已经喂饱了，直接走清场流程
    if (conn.cgi_body_bytes_sent >= body.size())
    {
        goto CLOSE_WRITE_Conduit;
    }

    // 3. 【非阻塞卡尺切片】：计算剩余运力，每次最多安全喂入 4096 字节
    {
        const char *data_ptr = body.data() + conn.cgi_body_bytes_sent;
        size_t remaining = body.size() - conn.cgi_body_bytes_sent;
        size_t chunk_size = (remaining > 4096) ? 4096 : remaining;

        // 强攻非阻塞物理管道写入
        ssize_t bytes_written = ::write(cgiWriteFd, data_ptr, chunk_size);

        if (bytes_written > 0)
        {
            conn.cgi_body_bytes_sent += bytes_written;
            std::cout << "[CGI Writer] Fed " << bytes_written << " bytes of body to CGI fd "
                      << cgiWriteFd << ". Total: " << conn.cgi_body_bytes_sent << "/" << body.size() << std::endl;
        }
        else if (bytes_written == -1)
        {
            // 如果遇到系统缓冲区满（EAGAIN），不算错，保留 POLLOUT 下一轮大循环继续进来喂
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                return;
            }
            if (errno == EINTR)
            {
                return; // 被系统信号打断，不算错，撤退等下一轮
            }

            // ❌ 发生真正的底层管道破裂（如 EPIPE，说明子进程提前崩了拒绝收货）
            std::cerr << "[CGI Writer] Fatal write error on pipe fd " << cgiWriteFd << ", breaking conduit." << std::endl;
            goto ERROR_FUSE;
        }
    }

    // 4. 【大功告成】：判断是否已经全量喂饱子进程？
    if (conn.cgi_body_bytes_sent >= body.size())
    {
    CLOSE_WRITE_Conduit:
        std::cout << "[CGI Writer] Finished feeding all POST body (" << body.size() << " bytes). Closing write pipe." << std::endl;

        // 🚀 【核心大闸】：物理关闭子进程的输入端管道！
        // 这样子进程的标准输入（stdin）就会读到一个完美的 EOF。
        // 绝大多数 CGI 脚本（如 Python 里的 sys.stdin.read()）只有读到 EOF 才会停止挂起、开始疯狂计算并返回结果！
        ::close(cgiWriteFd);
        this->_poll_fds.erase(this->_poll_fds.begin() + poll_idx);
        this->_cgi_fd_to_client_map.erase(it);
        conn.cgi_write_fd = -1;
    }
    return;

ERROR_FUSE:
    // 🚨 触发紧急安全熔断清理，并将客户端切入 500 阵地
    ::close(cgiWriteFd);
    this->_poll_fds.erase(this->_poll_fds.begin() + poll_idx);
    this->_cgi_fd_to_client_map.erase(it);
    conn.cgi_write_fd = -1;

    // 降维回执 500 熔断报错
    conn.response.createResponse(500, "CGI Write Pipe Broken", conn.config.error_pages);
    std::string().swap(conn.write_buffer);
    conn.write_buffer = conn.response.responseToString();

    // 调用我们写的特权函数（或者就地操作）拉起写事件发货 500 报错
    this->enableClientWriteEvent(clientFd);
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
    // 🚀 保持你精妙的倒序遍历，完美规避前端节点缩水引发的下标错位
    for (size_t i = this->_poll_fds.size(); i > 0; --i)
    {
        size_t idx = i - 1;

        // 极端防卫：防止在前面几轮循环中已经被物理注销或越界的 FD
        if (idx >= this->_poll_fds.size() || this->_poll_fds[idx].fd == -1 || this->_poll_fds[idx].revents == 0)
            continue;

        int activeFd = this->_poll_fds[idx].fd;
        short revents = this->_poll_fds[idx].revents;

        // ==================== 🔴 1. 异常事件挂起 (POLLERR / POLLHUP) ====================
        if (revents & (POLLERR | POLLHUP | POLLNVAL))
        {
            // 🚨 【CGI 异常雷达】：如果是 CGI 管道异常挂起，交由 handleCgiPipeRead
            // 它内部会自动触发 read 返回 -1 或 0 并就地完成 500 熔断和子进程超度
            if (this->isCgiPipeFd(activeFd))
            {
                this->handleCgiPipeRead(activeFd, idx);
            }
            else if (this->isListenFd(activeFd))
            {
                std::cerr << "[ServerManager] CRITICAL: Fatal event (" << revents
                          << ") on Listen FD " << activeFd << "!" << std::endl;
                this->_poll_fds[idx].fd = -1;
            }
            else
            {
                this->closeConnection(activeFd, idx);
            }
            continue; // 异常清理完毕，立刻切割，推进到下一个 FD
        }

        // ==================== 🟢 2. 读事件就绪 (POLLIN) ====================
        if (revents & POLLIN)
        {
            // 🚨 【CGI 黄金拦截网 A】：优先认出属于子进程吐数据的物理管道读端！
            if (this->isCgiPipeFd(activeFd))
            {
                this->handleCgiPipeRead(activeFd, idx); // 🎯 龙卷风抽干 -> 清洗装订 -> 开启客户端写发货大闸
                continue;                               // 🚀 【核心防线】：管道已被内部物理 erase，立刻过河拆桥，强制拦截本轮后续动作！
            }
            else if (this->isListenFd(activeFd))
            {
                this->acceptNewConnection(activeFd);
            }
            else
            {
                this->handleClientRead(activeFd, idx);
            }
        }

        // ==================== 🟢 3. 写事件就绪 (POLLOUT) ====================
        // 💡 避坑指南：把原先的 else if 独立拆解为 if 语句！
        // 确保当操作系统同时砸过来读写就绪时，大管家在一个周期的流水线上全量处理，绝不漏诊。
        if (revents & POLLOUT)
        {
            // 极端防卫：防止在上面的 POLLIN 处理中，这个 FD 已经顺便被关闭或擦除了
            if (idx >= this->_poll_fds.size() || this->_poll_fds[idx].fd != activeFd)
                continue;

            // 🚨 【CGI 黄金拦截网 B】：优先认出属于要喂数据的物理管道写端！
            if (this->isCgiPipeFd(activeFd))
            {
                this->handleCgiPipeWrite(activeFd, idx); // 🎯 非阻塞异步切片喂养 POST Body
                continue;                                // 🚀 【核心防线】：喂饱后管道可能已当场 close，立刻跳出安全撤离！
            }
            else
            {
                this->handleClientWrite(activeFd, idx);
            }
        }
    }
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