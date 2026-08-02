#include "Webserv.hpp"
#include "SessionStore.hpp"
#include "Signal.hpp"

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
函数用途：开始
参数与变量：
- setupSockets (类内部核心函数)：专职读取配置文件、循环创建 Socket、绑定端口（bind）并将其切入被动监听状态（listen）的基建车间。
实现逻辑：
1. 向控制台抛出初始化日志
2. 调用 setupSockets 核心，将配置文件里规划的所有虚拟端口，全量化为操作系统的 ListenFD（监听套接字），
   并手工将FD 编入全局轮询名册（_poll_fds），为后续 run()使用
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

/**
 * 函数：ServerManager::acceptNewConnection
 * 用途：当某个主监听端口触发读事件（POLLIN）时，调用该函数从内核的全连接队列中捞取并诞生成立一个新的客户端 TCP 会话连接。
 * 参数来源：listenFd 来自 run() 大循环中判定通过的当前活跃监听套接字（即 activeFd）。
 * 变量解释：
 *     - listenFd：正在被浏览器疯狂敲门的主监听套接字文件描述符。
 *     - clientFd：呼叫 accept 后，由内核为其分配的代表该次具体客户端会话的专属文件描述符。
 *     - client_addr：sockaddr_in 结构体，用来物理承接、记录新客户的 IP 地址和端口来源。
 *     - client_len：socklen_t 类型，存 sockaddr_in 结构体的物理大小，作为 accept 的入口与出口长度参数。
 *     - pfd：新组装的 pollfd 结构体，用于将新生的客户通道安插上树。
 * 实现逻辑：
 *     1. 初始化 client_addr 内存，并调用 accept(listenFd, ...) 顺藤摸瓜捞出全新的客户端 clientFd。
 *     2. 检查返回值，若 clientFd 小于 0 且 errno 为 EAGAIN 或 EWOULDBLOCK，说明连接已被抢夺或不存在，属于正常非阻塞抖动，优雅退出；若为其他致命系统错误，打印警告并折返。
 *     3. 打印迎宾日志，展示新客户的物理文件描述符（clientFd）。
 *     4. **【铁律洗礼】**：将新诞生的 clientFd 送入本类成员 setNonBlocking()，强行剥夺其阻塞特权，全站异步保全。
 *     5. **【资产挂牌】**：通过 _listen_socket_map[listenFd] 捞出这个端口对应的 server 配置，将其无缝过户转录到 _client_to_srv_map[clientFd] 中。
 *     6. **【大阵列上树】**：组装标准的 pollfd，加入对读事件（POLLIN）的战略关注，然后 push_back 挂载进底层的全局核心监视大阵列 _poll_fds 中。由于大循环采取倒序扫描，尾部的 push_back 动作对左侧未完成的遍历节点无任何内存或下标冲击，无需修正下标。
 * 后续影响：底层 poll 阵列规模动态扩大。此后，这个专属浏览器只要发来哪怕一个字节的 HTTP 裸请求文本，
 *           主大循环就会在下一个大死循环的滴答里精准捕获到，并完美流向专门处理客户端业务的 handleClientRead() 分支。
 */
void ServerManager::acceptNewConnection(int listenFd)
{
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    // 1. 物理 accept 提取新连接
    int clientFd = ::accept(listenFd, (struct sockaddr *)&client_addr, &client_len);
    if (clientFd < 0)
    {
        return;
    }
    ClientSocket *p_socket = NULL;
    Connection *conn = NULL;
    // 2.  RAII 异常安全防护：防止 new 失败导致的 FD / 堆内存物理泄漏
    try
    {
        // 创建 ClientSocket 实体
        p_socket = new ClientSocket(clientFd);
        // 创建 Connection 业务实体
        conn = new Connection();
        conn->socket = p_socket;
        // 3. 安全抽取虚拟主机配置实体
        std::map<int, ServerConfig>::iterator config_it = this->_listen_socket_map.find(listenFd);
        if (config_it != this->_listen_socket_map.end())
        {
            conn->config = config_it->second;
        }
        // 4. 彻底锁进大户籍 Map 账本
        this->_connections[clientFd] = conn;

        // 5. 挂载到 poll 雷达网上监听读事件 (POLLIN)
        this->registerFdToPoll(clientFd, POLLIN);
        std::cout << "[ServerManager] Accepted new connection -> Allocated Client FD: "
                  << clientFd << " (SUCCESSFULLY SET O_NONBLOCK!)" << std::endl;
    }
    catch (const std::exception &e)
    {
        std::cerr << "[Acceptor] Critical allocation error: " << e.what() << std::endl;
        // 如果有任何对象建出来了一半，一律安全的释放 + 物理 close(clientFd)
        if (p_socket != NULL)
            delete p_socket; // ClientSocket 析构会自动 ::close(clientFd)
        else
            ::close(clientFd);
        if (conn != NULL)
            delete conn;
    }
}

/*
函数：ServerManager::readSocketDataToBuffer
用途：从客户端非阻塞 Socket 中读取所有就绪的数据字节，追加至 Connection 的 read_buffer 蓄水池。
参数：
    - Connection *conn: 目标客户端连接对象指针。
    - int clientFd    : 客户端 Socket 文件描述符。
    - size_t pollIndex: 在 poll_fds 中的监听下标。
返回值：
    - bool: 成功读取或读空返回 true；若遇到 EOF (0) 或物理崩溃 (-2) 导致连接关闭则返回 false。
实现逻辑或说明：
    1. 使用 while(true) 循环调用 conn->socket->read 读取数据。
    2. 设置死循环计数器 loop_counter 防卫，防止底层 Socket 挂起触发死循环。
    3. 状态分支判定：
       - bytes_read == 0  : 客户端主动关闭连接 (EOF)，调用 closeConnection 并返回 false；
       - bytes_read == -1 : 正常的非阻塞缓冲区读空，安全 break 跳出循环并返回 true；
       - bytes_read == -2 : 物理链路崩溃，调用 closeConnection 并返回 false；
       - bytes_read > 0  : 追加数据至 conn->read_buffer。
*/
bool ServerManager::readSocketDataToBuffer(Connection *conn, int clientFd, size_t pollIndex)
{
    char buffer[BUFFER_SIZE];
    int loop_counter = 0;

    while (true)
    {
        if (++loop_counter > 1000)
        {
            std::cerr << "[ServerManager] DEAD LOOP DETECTED IN READ VALVE! Force breaking..." << std::endl;
            break;
        }
        ssize_t bytes_read = conn->socket->read(buffer, BUFFER_SIZE - 1);
        if (bytes_read == 0) // EOF
        {
            std::cout << "[ServerManager] Client FD " << clientFd << " closed connection (EOF)." << std::endl;
            this->closeConnection(clientFd, pollIndex);
            return false;
        }
        if (bytes_read == -1) // 非阻塞读空
        {
            break;
        }
        if (bytes_read == -2) // 物理崩溃
        {
            this->closeConnection(clientFd, pollIndex);
            return false;
        }
        if (bytes_read > 0)
        {
            buffer[bytes_read] = '\0';
            conn->read_buffer.append(buffer, bytes_read);
        }
    }
    return true;
}

/*
函数：ServerManager::dispatchCgiTask
用途：解包内部 CGI 标头，拉起 CGI 子进程任务并将管道 FD 挂载至 Reactor 事件雷达。
参数：
    - Connection *conn     : 目标客户端连接对象指针。
    - int clientFd        : 客户端 Socket 文件描述符。
    - const Response &res : 内部构建好的 Header 响应模板对象。
返回值：
    - void（无返回值）。
实现逻辑或说明：
    1. 从 res 中解包提取 X-Internal-CGI-Path 与 X-Internal-CGI-Interpreter 标头。
    2. 调用 _cgiManager.launchTask 裂变子进程，获得双向管道的 outReadFd 与 outWriteFd。
    3. 熔断处理：若 launchTask 失败，生成 500 响应并设置 setClientEvents(clientFd, POLLOUT)。
    4. 雷达注册：
       - 若 outReadFd >= 0，绑定 _cgi_read_fd_to_client_map 并注册 registerFdToPoll(outReadFd, POLLIN)；
       - 若 outWriteFd >= 0，绑定 _cgi_write_fd_to_client_map 并注册 registerFdToPoll(outWriteFd, POLLOUT)。
    5. 暂停客户端 Socket 读监听（setClientEvents(clientFd, 0)），防止后续数据打乱 CGI 管道状态。
*/
void ServerManager::dispatchCgiTask(Connection *conn, int clientFd, const Response &res)
{
    std::string script_path;
    std::string interpreter_path;

    res.getHeader("X-Internal-CGI-Path", script_path);
    res.getHeader("X-Internal-CGI-Interpreter", interpreter_path);

    // 💡 1. 拷贝 Request 的 Headers，并把 res 里的内部 CGI 标头合并进去
    std::map<std::string, std::string> cgiHeaders = conn->request.getHeaders();

    std::string scriptNameVal, pathInfoVal;
    if (res.getHeader("X-Internal-CGI-Script-Name", scriptNameVal))
        cgiHeaders["X-Internal-CGI-Script-Name"] = scriptNameVal;
    if (res.getHeader("X-Internal-CGI-Path-Info", pathInfoVal))
        cgiHeaders["X-Internal-CGI-Path-Info"] = pathInfoVal;

    std::string root;

    if (res.getHeader("X-Internal-CGI-Document-Root", root))
    {
        cgiHeaders["X-Internal-CGI-Document-Root"] = root;
    }
    else
    {
        std::cerr << "[CGI] Missing document root\n";
        return;
    }

    int outReadFd = -1;
    int outWriteFd = -1;

    std::string host = "localhost";
    std::string port = "8080";

    std::map<std::string, std::string>::const_iterator it =
        conn->request.getHeaders().find("Host");

    if (it != conn->request.getHeaders().end())
    {
        std::string hostHeader = it->second;

        size_t pos = hostHeader.find(':');

        if (pos != std::string::npos)
        {
            host = hostHeader.substr(0, pos);
            port = hostHeader.substr(pos + 1);
        }
        else
        {
            host = hostHeader;
        }
    }

    // 💡 2. 传入合并后的 cgiHeaders
    bool launched = this->_cgiManager.launchTask(
        clientFd,
        script_path,
        interpreter_path,
        conn->request.getMethod(),
        conn->request.getQuery(),
        conn->request.getPath(),
        cgiHeaders,
        conn->request.getBody(),
        host,
        port,
        root,
        outReadFd,
        outWriteFd);

    if (!launched)
    {
        std::cerr << "[CGI] Error: Failed to spawn CGI process for client " << clientFd << std::endl;
        conn->response.createResponse(500, "CGI Spawn Failed", conn->config.error_pages);
        conn->write_buffer = conn->response.responseToString();
        conn->close_after_write = true;
        this->setClientEvents(clientFd, POLLOUT);
        return;
    }

    if (outReadFd != -1)
    {
        this->_cgi_read_fd_to_client_map[outReadFd] = clientFd;
        this->registerFdToPoll(outReadFd, POLLIN);
    }

    if (outWriteFd != -1)
    {
        this->_cgi_write_fd_to_client_map[outWriteFd] = clientFd;
        this->registerFdToPoll(outWriteFd, POLLOUT);
    }

    // 暂停客户端 Socket 监听
    this->setClientEvents(clientFd, 0);
    std::cout << "[⚡ WebServ Core] Client " << clientFd << " successfully split into CGI pipeline, client read paused." << std::endl;
}

/*
函数：ServerManager::processParsedRequest
用途：处理解析完结的 HTTP Request，完成 Session 缝合、路由判定与 CGI/静态响应分发。
参数：
    - Connection *conn: 目标客户端连接对象指针。
    - int clientFd    : 客户端 Socket 文件描述符。
返回值：
    - void（无返回值）。
实现逻辑或说明：
    1. 实例化 SessionStore 机制并通过 buildResponse 构建 HTTP 响应模板。
    2. 检查响应中是否包含 X-Internal-CGI-Path 标头：
       - 若包含：说明是 CGI 请求，转交 dispatchCgiTask 处理；
       - 若不包含：说明是普通静态请求，生成 responseToString() 赋值给 conn->write_buffer，并将客户端事件重置为 POLLOUT 等待发货。
*/
void ServerManager::processParsedRequest(Connection *conn, int clientFd)
{
    static SessionStore sessionStore;
    Response res = buildResponse(conn->request, sessionStore);

    std::string script_path;
    if (res.getHeader("X-Internal-CGI-Path", script_path))
    {
        std::cout << "[DEBUG CGI Path] targetPath = " << script_path << std::endl;
        this->dispatchCgiTask(conn, clientFd, res);
    }
    else
    {
        conn->write_buffer = res.responseToString();
        this->setClientEvents(clientFd, POLLOUT);
    }
}

/*
函数：ServerManager::handleClientRead
用途：客户端 Socket 可读（POLLIN）事件入口函数。调度 Socket 缓冲区读取、协议解析与响应分发。
参数：
    - int clientFd    : 触发 POLLIN 事件的客户端 Socket 文件描述符。
    - size_t pollIndex: 该 FD 在 _poll_fds 中的当前下标。
返回值：
    - void（无返回值）。
实现逻辑或说明：
    1. 指针防卫：使用 find() 检查 clientFd 对应 Connection 指针的合法性，若非法则安全 closeConnection。
    2. Socket 捞取：调用 readSocketDataToBuffer 将内核缓冲区数据拉入 conn->read_buffer，若遇到关闭事件则提前结束。
    3. HTTP 协议解析：调用 RequestParser::parseBuffer 尝试解析：
       - REQUEST_OK         : 解析成功，擦除已消费字节，调用 processParsedRequest 路由处理；
       - REQUEST_INCOMPLETE : 数据未完结，静默等待下一个 poll 滴答继续接收；
       - 其它解析错误        : 构建 400 Bad Request 响应报文，将事件切换为 POLLOUT 准备回复错误。
*/
void ServerManager::handleClientRead(int clientFd, size_t pollIndex)
{
    // 防御 NULL 指针解引用
    std::map<int, Connection *>::iterator connIt = this->_connections.find(clientFd);
    if (connIt == this->_connections.end() || connIt->second == NULL)
    {
        std::cerr << "[ServerManager] Error: Client FD " << clientFd << " is NULL or unmapped in handleClientRead!" << std::endl;
        this->closeConnection(clientFd, pollIndex);
        return;
    }
    Connection *conn = connIt->second;

    // 抽取数据到 read_buffer 蓄水池
    if (!this->readSocketDataToBuffer(conn, clientFd, pollIndex))
        return;

    // 解析蓄水池里的 HTTP 数据
    size_t consumed = 0;
    int status = RequestParser::parseBuffer(conn->read_buffer, conn->request, &conn->config, consumed);

    if (status == REQUEST_OK)
    {
        std::cout << "[ServerManager] Request parsed successfully for FD " << clientFd << std::endl;
        conn->read_buffer.erase(0, consumed);
        this->processParsedRequest(conn, clientFd);
    }
    else if (status == REQUEST_INCOMPLETE)
    {
        std::cout << "[ServerManager] Request incomplete for FD " << clientFd << ". Waiting for more data..." << std::endl;
    }
    else if (status == REQUEST_BODY_TOO_LARGE)
    {
        std::cerr << "[ServerManager] Request body too large on FD " << clientFd << ". Pre-writing 413 response." << std::endl;
        conn->close_after_write = true;

        // 💡 优先使用你们的 ResponseBuilder，或者直接写 413 标头：
        std::string error_response = "HTTP/1.1 413 Payload Too Large\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        conn->write_buffer += error_response;
        this->setClientEvents(clientFd, POLLOUT);
    }
    else
    {
        std::cerr << "[ServerManager] Request error (" << status << ") on FD " << clientFd << ". Pre-writing 400 response." << std::endl;
        conn->close_after_write = true;
        std::string error_response = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        conn->write_buffer += error_response;
        this->setClientEvents(clientFd, POLLOUT);
    }
}

/**
 * 函数：ServerManager::handleClientWrite
 * 用途：当普通客户端（clientFd）触发写事件（POLLOUT）时，调用该函数通过非阻塞 send() 将高层业务产生的 HTTP 响应（Response）数据源源不断地安全喷吐给浏览器，并完美处理大文件分批发送和缓冲区满的情况。
 * 参数来源：来自 run() 大循环，其中 clientFd = _poll_fds[poll_index].fd，poll_index 是该节点在全局 _poll_fds 阵列中的实时下标。
 * 变量解释：
 *     - clientFd：准备接收响应数据的目标客户端套接字。
 *     - poll_index：该客户端在 poll 监视大阵列中的位置下标，用来在发送完毕或拔线时配合执行清理。
 *     - mock_response：本阶段临时模拟出的 HTTP 响应原始字符串。在后续与业务层对接时，它将直接替换为从高层路由组件拿来的、已经生成好的真实响应文本。
 *     - bytes_sent：单次 send() 调用后，内核写缓冲区实际成功吞入并准备发往网络的合法字节数。
 * 实现逻辑：
 *     1. 模拟或获取当前准备发送的 Response 字符串数据（后续会从对应的响应缓存中动态提取）。
 *     2. 呼叫 bytes_sent = send(clientFd, mock_response.c_str(), mock_response.size(), 0) 向内核缓冲区倾倒数据。
 *     3. **【判别抖动】**：若 bytes_sent 小于 0，深入核验 errno：
 *        - 若为 EAGAIN 或 EWOULDBLOCK，说明内核写缓冲区已经塞满了，属于正常的非阻塞暂缓信号，直接优雅退出，等待下一次 poll() 再次可写；
 *        - 若为 EINTR，属于被系统信号干扰，不气馁，立刻重新尝试发送；
 *        - 若为其他异常（如 EPIPE 浏览器提早无情关闭了标签页），打印警告并调用 closeConnection() 销毁通道。
 *     4. **【分批切片】**：若成功发出正数：
 *        - 如果一次性把全部数据发完了（bytes_sent == mock_response.size()），说明该连接的这轮请求已经寿终正寝。
 *        - **【功德圆满】**：如果 HTTP 协议中未配置 Keep-Alive 长连接，直接调用 closeConnection() 拔线清理；如果支持长连接，则清空缓冲抽屉，并将战略监控目标 180 度大转弯修改回 `_poll_fds[poll_index].events = POLLIN`，让它在下一次大循环中继续聆听新请求。
 *        - 如果只发了前半句（数据没发完），利用 erase/substr 裁切掉已经发送的头部，保留残余数据在小抽屉里，保持 POLLOUT 状态，出函数等待下一轮 poll 滴答继续续喷。
 * 后续影响：数据稳健喷吐。如果一轮发完，重新切回读状态接收下一次进攻；如果未完，则牢牢咬住可写状态继续倾倒，彻底保障了多路复用网络流在极端压力下的绝对完整性。
 */
void ServerManager::handleClientWrite(int clientFd, size_t pollIndex)
{
    // 用 find() 探查，防野指针与隐式插入
    std::map<int, Connection *>::iterator it = this->_connections.find(clientFd);
    if (it == this->_connections.end() || it->second == NULL)
        return;
    Connection *conn = it->second;
    // 缓冲区本来就是空的边界处理
    if (conn->write_buffer.empty())
    {
        if (conn->close_after_write)
        {
            this->closeConnection(clientFd, pollIndex);
        }
        else
        {
            // Keep-Alive 长连接复用：调用 clear() 彻底洗白 Connection 上下文
            conn->clear();
            this->setClientEvents(clientFd, POLLIN);
        }
        return;
    }

    // 物理切片非阻塞发送
    ssize_t bytes_sent = conn->socket->write(conn->write_buffer);

    if (bytes_sent > 0)
    {
        // 抹去已成功发货的切片
        conn->write_buffer.erase(0, bytes_sent);
        // 如果发货完毕
        if (conn->write_buffer.empty())
        {
            if (conn->close_after_write)
            {
                std::cout << "[ServerManager] Sent response completely to FD " << clientFd << ". Closing connection per policy." << std::endl;
                this->closeConnection(clientFd, pollIndex);
            }
            else
            {
                std::cout << "[ServerManager] Sent response completely to FD " << clientFd << ". Resetting event to POLLIN." << std::endl;
                // 核心重置：Keep-Alive 长连接复用，洗白 Request 和 Response 状态，准备迎接下一个 HTTP 请求！
                conn->clear();
                this->setClientEvents(clientFd, POLLIN);
            }
        }
    }
    else if (bytes_sent == -1)
    {
        // 内核缓冲区暂态满 (EAGAIN/EWOULDBLOCK)，不算错，保持 POLLOUT 等下一轮 poll 滴答
        return;
    }
    else if (bytes_sent == -2)
    {
        // 对端物理断连/管道破裂！直接斩断悬空 Socket
        std::cerr << "[ServerManager] Fatal send error (-2) on FD " << clientFd << "! Closing connection." << std::endl;
        this->closeConnection(clientFd, pollIndex);
    }
}

/*
函数：ServerManager::handleCgiRead
用途：Reactor 事件回调函数。当 CGI 输出管道（pipe_from_child[0]）触发可读事件（POLLIN/POLLHUP）时，负责调度 CGI stdout 数据的非阻塞读取与客户端响应装配。
参数：
    - int cgiReadFd: 触发可读/挂断事件的 CGI 管道读端文件描述符。
返回值：
    - void（无返回值）。
实现逻辑或说明：
    1. 驱动读取：调用 _cgiManager.handlePipeRead(cgiReadFd) 非阻塞读取数据字节。
    2. 完工分支（res.status == CGI_FINISHED）：
       - 从 _cgi_read_fd_to_client_map 账本与 poll 监听列表中擦除 cgiReadFd；
       - 调用 cleanupClientWritePipe 安全清理该客户端可能残存的写管道；
       - 通过 res.clientFd 匹配对应的 Connection，解析 CGI 原始输出并调用 buildCgiResponse 装配正式响应；
       - 将响应挂载至 conn->write_buffer，重置事件为 POLLOUT 准备向客户端回派数据。
    3. 报错分支（res.status == CGI_ERROR）：
       - 同步注销 cgiReadFd 与残存的 cgiWriteFd；
       - 构建对应 res.statusCode（如 500 / 502）的错误 Response，切换事件为 POLLOUT 向客户端回复错误页。
    4. 过程分支（res.status == CGI_CONTINUE）：
       - 数据未吐完，不做任何注销与事件切换，保持 poll 监听等待下一个 Tick 继续读取。
*/
void ServerManager::handleCgiRead(int cgiReadFd)
{
    CgiEventResult res = this->_cgiManager.handlePipeRead(cgiReadFd);

    if (res.status == CGI_FINISHED)
    {
        std::cout
            << "========== RAW CGI OUTPUT =========="
            << std::endl
            << res.rawOutput
            << std::endl
            << "===================================="
            << std::endl;
        this->_cgi_read_fd_to_client_map.erase(cgiReadFd);
        this->eraseFdFromPoll(cgiReadFd);
        this->cleanupClientWritePipe(res.clientFd);

        Connection *conn = this->_connections[res.clientFd];
        if (conn)
        {
            Response cgiResponse = buildCgiResponse(conn->request, res.rawOutput);
            conn->response = cgiResponse;
            conn->write_buffer = cgiResponse.responseToString();
            // conn->close_after_write = true;
            this->setClientEvents(res.clientFd, POLLOUT);
        }
    }
    else if (res.status == CGI_ERROR)
    {
        this->_cgi_read_fd_to_client_map.erase(cgiReadFd);
        this->eraseFdFromPoll(cgiReadFd);
        this->cleanupClientWritePipe(res.clientFd);

        Connection *conn = this->_connections[res.clientFd];
        if (conn)
        {
            conn->response.createResponse(res.statusCode, "CGI Output Error", conn->config.error_pages);
            conn->write_buffer = conn->response.responseToString();
            // conn->close_after_write = true;
            this->setClientEvents(res.clientFd, POLLOUT);
        }
    }
    // 🟢 res.status == CGI_CONTINUE 时隐式不进任何 if，保持 poll_fds 中的 POLLIN 监听！
}

/*
函数：ServerManager::handleCgiWrite
用途：Reactor 事件回调函数。当 CGI 输入管道（pipe_to_child[1]）触发可写事件（POLLOUT）时，负责调度 POST Body 的非阻塞切片写入与写管道的生命周期管理。
参数：
    - int cgiWriteFd: 触发 POLLOUT 可写事件的 CGI 管道写端文件描述符。
返回值：
    - void（无返回值）。
实现逻辑或说明：
    1. 驱动写入：调用 _cgiManager.handlePipeWrite(cgiWriteFd) 尝试向 CGI stdin 管道非阻塞写入 Body 数据切片。
    2. 错误处理分支（res.status == CGI_ERROR）：
       - 说明管道写入发生物理破裂（如 EPIPE 或子进程挂掉）；
       - 从 _cgi_write_fd_to_client_map 账本与 poll 监听列表中抹除该 cgiWriteFd；
       - 通过 res.clientFd 匹配对应的 Connection，构建 500 状态码的错误 Response 并挂载客户端 Socket 的 POLLOUT 事件，准备回复错误页面。
    3. 正常传输与完工分支（res.status == CGI_CONTINUE）：
       - 完工判定：调用 _cgiManager.hasWriteTask(cgiWriteFd) 检查该写管道是否已被 CgiManager 在内部关闭并注销；
       - 若 hasWriteTask 返回 false（ Body 已完整发完并发送 EOF）：从 Server 侧映射表擦除并调用 eraseFdFromPoll 从 poll 监听队列中注销该写 FD；
       - 若 hasWriteTask 返回 true（ Body 尚未写完）：不做任何注销操作，保持其在 poll_fds 中的 POLLOUT 监听状态，等待下一个事件 Tick 继续写入剩余数据切片。
*/
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
        if (!this->_cgiManager.hasWriteTask(cgiWriteFd))
        {
            this->_cgi_write_fd_to_client_map.erase(cgiWriteFd);
            this->eraseFdFromPoll(cgiWriteFd);
        }
    }
}

/*
函数：ServerManager::dispatchEvents
用途：Reactor 事件分发核心引擎。负责轮询 _poll_fds 队列中捕获到的就绪事件（I/O 读写与异常），并精确路由至相对应的处理函数。
参数：
    - 无（直接读取类成员变量 _poll_fds 以及各项映射账本）。
返回值：
    - void（无返回值）。
实现逻辑或说明：
    1. 倒序安全遍历（Reverse Iteration）：
       - 使用 for (size_t i = _poll_fds.size(); i > 0; --i) 进行倒序遍历；
       - 结合防卫检查（idx >= size() || fd == -1 || revents == 0），彻底消除在分发过程中因 eraseFdFromPoll 或 closeConnection 导致 vector 元素前移引发的索引越界与迭代器失效问题。
    2. 分支一：CGI 读管道事件（isCgiReadFd）：
       - 若触发 POLLIN 或 POLLHUP，路由至 handleCgiRead（POLLHUP 必须走 handleCgiRead 读取管道内核缓冲区中残存的最后一批数据）；
       - 若触发 POLLERR 或 POLLNVAL，判定为管道物理破坏，解绑关联的 clientFd，强杀 CGI 任务并给客户端回派 500 响应。
    3. 分支二：CGI 写管道事件（isCgiWriteFd）：
       - 若触发 POLLOUT，路由至 handleCgiWrite 向 CGI stdin 异步切片写入 POST Body；
       - 若触发 POLLERR、POLLHUP 或 POLLNVAL，说明 CGI 提前关闭了 stdin，强杀任务并给客户端回派 500 响应。
    4. 分支三：普通 Socket 异常事件（POLLERR / POLLHUP / POLLNVAL）：
       - 若为监听 Socket（Listen FD）物理故障，打印 CRITICAL 级日志并将 fd 置为 -1 挂起；
       - 若为客户端 Socket，路由至 closeConnection 清理客户端连接与上下文。
    5. 分支四：普通 Socket 可读事件（POLLIN）：
       - 若为 Listen FD，路由至 acceptNewConnection 接收新客户端连接；
       - 若为客户端 Socket，路由至 handleClientRead 读取 HTTP 请求数据。
    6. 分支五：普通 Socket 可写事件（POLLOUT）：
       - 再次防卫检查，防止在前面 POLLIN 处理中客户端已提前断开或 FD 被 swap 置换；
       - 若 FD 依然有效且匹配，路由至 handleClientWrite 向客户端发送响应 Body。
*/
void ServerManager::dispatchEvents()
{
    // 倒序遍历（防御 Vector erase 导致的索引失效）
    for (size_t i = this->_poll_fds.size(); i > 0; --i)
    {
        size_t idx = i - 1;
        // 防止在前面几轮循环中已经被物理注销或越界的 FD
        if (idx >= this->_poll_fds.size() || this->_poll_fds[idx].fd == -1 || this->_poll_fds[idx].revents == 0)
            continue;
        int activeFd = this->_poll_fds[idx].fd;
        short revents = this->_poll_fds[idx].revents;

        if (revents == 0)
            continue;
        // ==================== 1. CGI 读管道事件分发 (CGI stdout -> Webserv) ====================
        if (this->isCgiReadFd(activeFd))
        {
            // POLLHUP 必须走 handleCgiRead：管道里可能还有最后一批未读完的数据！
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

void ServerManager::stop()
{
    std::cout << "\n[Server] Shutting down gracefully..." << std::endl;

    // 1. 回收所有运行中的 CGI 子进程
    this->_cgiManager.stopAllTasks();

    // 2. 关闭所有的监听 Socket 和客户端 Socket
    for (size_t i = 0; i < _poll_fds.size(); ++i)
    {
        if (_poll_fds[i].fd >= 0)
            close(_poll_fds[i].fd);
    }
    _poll_fds.clear();

    std::cout << "[Server] Cleaned up all sockets and CGI processes. Bye!" << std::endl;
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
   - 第一步（prePollCleanup）：在前置位清理上一轮循环中被标记为脏、死、或已过期的僵尸资产与长连接，保持核心底盘绝对纯净。
   - 第二步（executePoll）：物理拉起多路复用大闸，主线程让出 CPU 陷入非阻塞全天候休眠，并向外部引入弹性防线计数器保护。若返回负数（即连续打断超过 3 次红线），说明遭遇毁灭性灾难，果断强行跳出循环（break）安全撤退。
   - 第三步（dispatchEvents）：若成功捕获有效网络事件，立刻开启倒序卡尺，将事件精准分
        std::cerr << "[ServerManager] Error: No listening sockets in poll tree. Aborting run()." << std::endl;
        return;流至对应的读/写/异常/CGI 业务车间进行最终的弹射交货！
*/
void ServerManager::run()
{
    g_loop_running = true;
    if (this->_poll_fds.empty())
    {
        std::cerr << "[ServerManager] Error: No listening sockets in poll tree. Aborting run()." << std::endl;
        return;
    }
    std::cout << "[ServerManager] Main loop started. Entering the matrix..." << std::endl;
    int poll_error_retries = 0;
    setupSignalHandlers();
    while (g_loop_running) // 提示：若有全局信号标志位（如 g_server_running），也可替换 while(true) 方便 Ctrl+C 退出
    {
        // 1. 轮询前的账本与死链清理车间（如追加 fds_to_add 到 poll_fds）
        this->prePollCleanup();
        // 2. 执行 1000ms 物理超时/阻塞轮询
        int ret = this->executePoll(poll_error_retries);
        if (ret < 0)
        {
            std::cerr << "[ServerManager] Fatal poll failure limit reached. Breaking main loop." << std::endl;
            break;
        }
        // 3. 只有真的有 FD 就绪 (ret > 0) 时，才分发事件
        if (ret > 0)
        {
            this->dispatchEvents();
        }

        // 4. 巡检 CGI 超时任务，统一渲染 504 Gateway Timeout 报错
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
        this->_cgiManager.reapChildren();
    }
    // 💡 退出循环后：执行优雅清理 (Graceful Cleanup)
    this->stop();
    std::cout << "[ServerManager] Main loop safely terminated." << std::endl;
}
