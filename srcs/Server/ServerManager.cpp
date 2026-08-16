#include "Webserv.hpp"
#include "SessionStore.hpp"
#include "Signal.hpp"
#include "ConfigRouteUtils.hpp"

/*
函数：ServerManager::ServerManager
用途：保存服务器配置，并把所有依赖明确初始值的运行时计数器初始化为确定状态。
参数来源：configs 来自 main() 完成解析和校验后的全部 ServerConfig。
实现逻辑或说明：
    1. _server_configs 保存配置副本，供监听 Socket 和虚拟主机路由使用。
    2. _active_buffered_large_cgi 必须从 0 开始；若保持未初始化的垃圾值，首个大型 CGI
       可能被错误判断为“槽位已满”并永久暂停，因为当时没有任何真实任务能够释放槽位。
    3. 其余 STL 容器和 CgiManager 使用各自默认构造函数建立空状态。
*/
ServerManager::ServerManager(const std::vector<ServerConfig> &configs)
    : _server_configs(configs), _active_buffered_large_cgi(0)
{

    std::cout << "[ServerManager] WebServ engine pre-loaded with "
              << _server_configs.size() << " virtual servers." << std::endl;
}

// 斩断所有堆上开辟的服务器物理套接字指针
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

    for (std::map<int, Connection *>::iterator it = this->_connections.begin();
         it != this->_connections.end(); ++it)
    {
        if (it->second != NULL)
        {
            delete it->second;
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
    ::signal(SIGPIPE, SIG_IGN);
}

/*
函数用途：解析配置并创建所有虚拟主机的非阻塞监听套接字（ListenFD），将其加入 poll 轮询。
参数与变量：
- _server_configs：全量虚拟主机配置蓝图。
- handled_endpoints：已处理的 host:port 列表，用于去重，避免重复 bind。
- srv_sock：ServerSocket 封装对象，负责 socket()、setsockopt()、bind() 和 listen()。
- _listen_sockets：监听套接字指针容器。
- _listen_socket_map：监听 FD 到 ServerConfig 的映射账本。
- _poll_fds：多路复用全局核心轮询监视名册。
实现逻辑：
1. 去重防线：遍历配置，通过 host 和 port 联合去重，若端口已绑定则跳过。
2. 实例化监听：未绑定的端口由 ServerSocket 创建并初始化为非阻塞监听状态。
3. 建立映射：保存 ListenFd，并将其与对应的虚拟主机配置进行关联。
4. 挂载轮询：将 ListenFd 构造为 pollfd 结构体，注册 POLLIN 事件并压入监视名册。
*/
void ServerManager::setupSockets()
{
    std::vector<std::pair<std::string, int>> handled_endpoints;

    for (size_t i = 0; i < _server_configs.size(); ++i)
    {
        int port = _server_configs[i].port;
        std::string host = _server_configs[i].host;
        bool is_duplicate = false;
        for (size_t p = 0; p < handled_endpoints.size(); ++p)
        {
            if (handled_endpoints[p].first == host && handled_endpoints[p].second == port)
            {
                is_duplicate = true;
                break;
            }
        }
        if (is_duplicate)
        {
            std::cout << "[ServerManager] Multi-server configuration detected for "
                      << host << ":" << port << " (Skipping duplicate bind)" << std::endl;
            continue;
        }
        ServerSocket *srv_sock = new ServerSocket(host, port);
        srv_sock->setup();
        int listenFd = srv_sock->getFd();
        std::cout << "[ServerManager] Successfully listening on "
                  << host << ":" << port << " (FD: " << listenFd << ")" << std::endl;

        this->_listen_sockets.push_back(srv_sock);
        handled_endpoints.push_back(std::make_pair(host, port));
        _listen_socket_map[listenFd] = _server_configs[i];
        struct pollfd pfd;
        pfd.fd = listenFd;
        pfd.events = POLLIN;
        pfd.revents = 0;
        _poll_fds.push_back(pfd);
    }
}

/*
函数用途：当主监听端口触发读事件（POLLIN）时，从内核全连接队列中接收新的客户端连接。
参数与变量：
- listenFd：当前活跃的主监听套接字文件描述符。
- clientFd：内核为新客户端会话分配的专属文件描述符。
- client_addr / client_len：记录客户端 IP 地址和端口来源的结构体及其大小。
- p_socket：封装客户端套接字的指针。
- conn：管理该客户端会话状态的 Connection 实体。
实现逻辑：
1. 接受连接：调用 accept 获取新的 clientFd，若失败则安全退出。
2. 资源分配：通过 try-catch 动态实例化 ClientSocket 和 Connection，建立异常安全保障。
3. 配置绑定：通过 _listen_socket_map 查找该监听端口对应的虚拟主机配置，并赋给 Connection。
4. 状态注册：将 clientFd 存入连接管理容器，并通过 registerFdToPoll 注册进 poll 轮询监视名册（关注 POLLIN）。
5. 异常回滚：若分配或注册过程中抛出异常，捕获错误，清理已分配的资源并关闭 FD，防止内存/资源泄漏。
*/
void ServerManager::acceptNewConnection(int listenFd)
{
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    int clientFd = ::accept(listenFd, (struct sockaddr *)&client_addr, &client_len);
    if (clientFd < 0)
        return;
    ClientSocket *p_socket = NULL;
    Connection *conn = NULL;
    try
    {
        p_socket = new ClientSocket(clientFd);
        conn = new Connection();

        conn->socket = p_socket;
        std::map<int, ServerConfig>::iterator config_it = this->_listen_socket_map.find(listenFd);
        if (config_it != this->_listen_socket_map.end())
            conn->config = config_it->second;
        this->_connections[clientFd] = conn;
        this->registerFdToPoll(clientFd, POLLIN);
        std::cout << "[ServerManager] Accepted new connection -> Allocated Client FD: "
                  << clientFd << " (SUCCESSFULLY SET O_NONBLOCK!)" << std::endl;
    }
    catch (const std::exception &e)
    {
        std::cerr << "[Acceptor] Critical allocation error: " << e.what() << std::endl;
        this->_connections.erase(clientFd);
        if (conn != NULL)
            delete conn;
        else if (p_socket != NULL)
            delete p_socket;
        else
            ::close(clientFd);
    }
}

/*
函数：ServerManager::readSocketDataToBuffer
用途：在客户端已经由 poll() 报告 POLLIN 后，只执行一次 socket 读取，并把成功读取的数据追加到 Connection::read_buffer。
参数：conn/clientFd/pollIndex 来自 handleClientRead()。
返回值：成功读取正数字节返回 true；EOF 或 recv 失败时关闭连接并返回 false。
实现逻辑或说明：
    1. 每次 POLLIN 对该客户端只调用一次 ClientSocket::read()，符合评估要求的一次事件一次 client read。
    2. bytes_read > 0 时追加到 read_buffer；HTTP 请求不完整由 parser 返回 REQUEST_INCOMPLETE，等待下一轮 POLLIN。
    3. bytes_read == 0 表示对端 EOF；bytes_read < 0 表示本次 recv 失败。两者都统一清理客户端。
    4. 不在 recv 后检查 errno；错误分类依赖 poll 的事件门控和系统调用返回值。
*/
bool ServerManager::readSocketDataToBuffer(Connection *conn, int clientFd, size_t pollIndex)
{
    char buffer[BUFFER_SIZE];

    ssize_t bytes_read = conn->socket->read(buffer, BUFFER_SIZE - 1);
    if (bytes_read > 0)
    {
        conn->read_buffer.append(buffer, static_cast<size_t>(bytes_read));
        return true;
    }
    if (bytes_read == 0)
        DEBUG_LOG("[ServerManager] Client FD " << clientFd << " closed connection (EOF).");
    else
        DEBUG_LOG("[ServerManager] recv failed on ready Client FD " << clientFd << ". Closing connection.");
    this->closeConnection(clientFd, pollIndex);
    return false;
}

/*
函数：ServerManager::dispatchCgiTask
用途：解包内部 CGI 标头，拉起 CGI 子进程任务并将管道 FD 挂载至 poll 多路复用轮询。
参数：
    - Connection *conn     : 目标客户端连接对象指针。
    - int clientFd        : 客户端 Socket 文件描述符。
    - const Response &res : 内部构建好的 Header 响应模板对象。
返回值：
    - void（无返回值）。
实现逻辑：
1. 标头解包：从 Response 中提取 CGI 路径、解释器路径、脚本名、Path-Info 以及 Document-Root。若缺少根目录则熔断返回 500。
2. Host 解析：从请求头中提取 Host 和 Port，用于组装 CGI 环境变量。
3. 进程拉起：调用 _cgiManager.launchTask 孵化 CGI 子进程，并获取双向管道的读写 FD（outReadFd, outWriteFd）。
4. 错误熔断：若启动失败，生成 500 响应并唤醒客户端写事件（POLLOUT）以向其下发错误。
5. 管道注册：将合法的管道 FD 绑定对应的 clientFd 映射，并通过 registerFdToPoll 挂载进 poll 监视名册。
6. 状态冻结：暂停客户端 Socket 的读监听（setClientEvents(clientFd, 0)），防止后续数据打乱 CGI 异步管道状态。
*/
void ServerManager::dispatchCgiTask(Connection *conn, int clientFd, const Response &res)
{
    std::string script_path;
    std::string interpreter_path;
    std::string root;
    int outReadFd = -1;
    int outWriteFd = -1;

    res.getHeader("X-Internal-CGI-Path", script_path);
    res.getHeader("X-Internal-CGI-Interpreter", interpreter_path);
    std::map<std::string, std::string> cgiHeaders = conn->request.getHeaders();
    std::string scriptNameVal, pathInfoVal;
    if (res.getHeader("X-Internal-CGI-Script-Name", scriptNameVal))
        cgiHeaders["X-Internal-CGI-Script-Name"] = scriptNameVal;
    if (res.getHeader("X-Internal-CGI-Path-Info", pathInfoVal))
        cgiHeaders["X-Internal-CGI-Path-Info"] = pathInfoVal;
    if (res.getHeader("X-Internal-CGI-Document-Root", root))
        cgiHeaders["X-Internal-CGI-Document-Root"] = root;
    else
    {
        DEBUG_LOG("[CGI] Error: Missing document root for client " << clientFd);
        conn->response.createResponse(500, "CGI Missing Document Root", conn->config.error_pages);
        conn->write_buffer = conn->response.responseToString();
        conn->close_after_write = true;
        this->setClientEvents(clientFd, POLLOUT);
        return;
    }
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
            host = hostHeader;
    }
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
        DEBUG_LOG("[CGI] Error: Failed to spawn CGI process for client " << clientFd);
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
    this->setClientEvents(clientFd, 0);
    DEBUG_LOG("[⚡ WebServ Core] Client " << clientFd << " successfully split into CGI pipeline, client read paused.");
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
    std::string contentLength;
    std::string script_path;

    DEBUG_LOG("\n================ [DEBUG REQUEST] ================");
    DEBUG_LOG("[REQ] Method: " << conn->request.getMethod()
                               << " | URI: " << conn->request.getPath()
                               << " | Body Size in Request: " << conn->request.getBody().size() << " bytes");
    Response res = buildResponse(conn->request, sessionStore);
    res.getHeader("Content-Length", contentLength);
    DEBUG_LOG("[RES] Status: " << res.getStatusCode()
                               << " | Header Content-Length: " << contentLength);
    if (res.getHeader("X-Internal-CGI-Path", script_path))
    {
        DEBUG_LOG("[DEBUG CGI Task] Dispatching CGI -> scriptPath = " << script_path);
        DEBUG_LOG("=================================================\n");
        this->dispatchCgiTask(conn, clientFd, res);
    }
    else
    {
        DEBUG_LOG("[DEBUG Normal Task] No CGI header, sending direct response.");
        DEBUG_LOG("=================================================\n");
        conn->write_buffer = res.responseToString();
        std::string connHeader;
        if (res.getHeader("Connection", connHeader))
        {
            std::string temp = connHeader;
            for (size_t i = 0; i < temp.length(); ++i)
                temp[i] = std::tolower(temp[i]);
            if (temp == "close")
                conn->close_after_write = true;
        }
        this->setClientEvents(clientFd, POLLOUT);
    }
}

/*
函数：requestUsesChunkedBody
用途：读取 RequestParser 已完成解析的 headers，判断当前未完成请求是否使用 chunked framing。
参数来源：request 是 Connection 中当前正在解析的 Request。
实现逻辑：读取 transfer-encoding，修剪 OWS 并转成 ASCII 小写；只有严格等于 chunked 才返回 true。
*/
static bool requestUsesChunkedBody(const Request &request)
{
    std::string value;
    size_t start = 0;
    std::string normalized = value.substr(start, end - start);
    size_t i = 0;

    if (!request.getHeader("transfer-encoding", value))
        return false;
    while (start < value.size() && (value[start] == ' ' || value[start] == '\t'))
        ++start;
    size_t end = value.size();
    while (end > start && (value[end - 1] == ' ' || value[end - 1] == '\t'))
        --end;
    while (i < normalized.size())
    {
        if (normalized[i] >= 'A' && normalized[i] <= 'Z')
            normalized[i] = static_cast<char>(normalized[i] - 'A' + 'a');
        ++i;
    }
    return normalized == "chunked";
}

/*
函数：advanceConnectionChunkScan
用途：为一个 Connection 初始化或继续 chunked 增量边界扫描。
参数来源：conn 持有原始 read_buffer、已解析 path 与跨 poll 保留的扫描位置；consumed 在完整时接收请求结尾。
实现逻辑：首次从 header 结束位置建立状态并计算 effective body limit；之后只调用 advanceChunkedScan 扫描新增 chunk。
*/
static int advanceConnectionChunkScan(Connection *conn, size_t &consumed)
{
    if (!conn->chunk_scan_active)
    {
        size_t headerEnd = conn->read_buffer.find("\r\n\r\n");
        if (headerEnd == std::string::npos)
            return REQUEST_INCOMPLETE;
        conn->chunk_scan_active = true;
        conn->chunk_scan_pos = headerEnd + 4;
        conn->chunk_decoded_size = 0;
        conn->chunk_body_limit = getEffectiveBodyLimit(
            &conn->config, conn->request.getPath());
    }
    return RequestParser::advanceChunkedScan(conn->read_buffer,
                                             conn->chunk_scan_pos, conn->chunk_body_limit,
                                             conn->chunk_decoded_size, consumed);
}

/*
函数：requestTargetsConfiguredCgi
用途：在 body 尚未完整时，仅依据已解析的 method/path 与 location CGI 后缀判断是否为 CGI POST。
说明：只用于大型请求的内存准入，不代替 buildResponse() 的最终权限、路径与执行校验。
*/
static bool requestTargetsConfiguredCgi(const Connection *conn)
{
    if (conn == NULL || conn->request.getMethod() != "POST")
        return false;
    const LocationConfig *location = findMatchingLocation(
        conn->request.getPath(), conn->config.locations);
    if (location == NULL)
        return false;
    std::map<std::string, std::string>::const_iterator it =
        location->cgi_extensions.begin();
    while (it != location->cgi_extensions.end())
    {
        const std::string &extension = it->first;
        const std::string &path = conn->request.getPath();
        if (!extension.empty() && path.size() >= extension.size() && path.compare(path.size() - extension.size(), extension.size(), extension) == 0)
            return true;
        ++it;
    }
    return false;
}

/*
函数：ServerManager::ensureLargeCgiSlot
用途：为超过阈值的 chunked CGI 申请完整缓冲槽；槽满时暂停客户端 POLLIN 并按 FIFO 排队。
返回值：已经取得槽返回 true；进入等待队列返回 false，调用方必须立即停止读取和解析。
*/
bool ServerManager::ensureLargeCgiSlot(Connection *conn, int clientFd)
{
    if (conn == NULL)
        return false;
    if (conn->large_cgi_slot_acquired)
        return true;
    if (conn->large_cgi_waiting)
        return false;
    if (this->_active_buffered_large_cgi < MAX_BUFFERED_LARGE_CGI_TASKS)
    {
        conn->large_cgi_slot_acquired = true;
        ++this->_active_buffered_large_cgi;
        return true;
    }
    conn->large_cgi_waiting = true;
    this->_waiting_buffered_large_cgi_clients.push_back(clientFd);
    this->setClientEvents(clientFd, 0);
    DEBUG_LOG("[ServerManager] Large CGI client " << clientFd
                                                  << " paused by memory admission control.");
    return false;
}

/*
函数：ServerManager::resumeWaitingLargeCgiClients
用途：有空槽时按 FIFO 恢复等待中的大型 CGI 客户端。
实现逻辑：队列允许残留已断开的 FD；恢复时用连接表和 waiting 标志双重校验并跳过失效项。
*/
void ServerManager::resumeWaitingLargeCgiClients()
{
    while (this->_active_buffered_large_cgi < MAX_BUFFERED_LARGE_CGI_TASKS && !this->_waiting_buffered_large_cgi_clients.empty())
    {
        int clientFd = this->_waiting_buffered_large_cgi_clients.front();

        this->_waiting_buffered_large_cgi_clients.pop_front();
        std::map<int, Connection *>::iterator it = this->_connections.find(clientFd);
        if (it == this->_connections.end() || it->second == NULL || !it->second->large_cgi_waiting)
            continue;
        Connection *conn = it->second;

        conn->large_cgi_waiting = false;
        conn->large_cgi_slot_acquired = true;
        ++this->_active_buffered_large_cgi;
        this->setClientEvents(clientFd, POLLIN);
        DEBUG_LOG("[ServerManager] Large CGI client " << clientFd
                                                      << " resumed.");
    }
}

/*
函数：ServerManager::releaseLargeCgiSlot
用途：在响应发送完毕或连接提前关闭时释放大型 CGI 完整缓冲槽，并唤醒后续等待连接。
*/
void ServerManager::releaseLargeCgiSlot(int clientFd)
{
    std::map<int, Connection *>::iterator it = this->_connections.find(clientFd);
    if (it == this->_connections.end() || it->second == NULL)
        return;
    Connection *conn = it->second;
    bool released = conn->large_cgi_slot_acquired;
    if (released)
    {
        conn->large_cgi_slot_acquired = false;
        if (this->_active_buffered_large_cgi > 0)
            --this->_active_buffered_large_cgi;
    }
    conn->large_cgi_waiting = false;
    if (released)
        this->resumeWaitingLargeCgiClients();
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
    3. 首次调用 parseBuffer 解析 request-line/headers；若确认是未完成 chunked 请求，建立 Connection 级扫描状态。
    4. 后续 poll 只用 advanceChunkedScan 从上次边界继续，避免学校 tester 的大量小 chunk 触发 O(n²) 重扫。
    5. 收到完整 0-size chunk 后才运行一次最终 parseBuffer 解码 body；成功后释放原始大 buffer 并分发请求。
    6. 超限时先发送 413，再进入输入排空状态；客户端结束写入并关闭后才释放 socket，避免 RST。
    7. 其他语法错误返回 400。
*/
void ServerManager::handleClientRead(int clientFd, size_t pollIndex)
{
    std::map<int, Connection *>::iterator connIt =
        this->_connections.find(clientFd);
    if (connIt == this->_connections.end() || connIt->second == NULL)
    {
        std::cerr << "[ServerManager] Error: Client FD " << clientFd
                  << " is NULL or unmapped in handleClientRead!" << std::endl;
        this->closeConnection(clientFd, pollIndex);
        return;
    }
    Connection *conn = connIt->second;

    if (conn->drain_input_before_close)
    {
        char discard[BUFFER_SIZE];

        ssize_t bytesRead = conn->socket->read(discard, sizeof(discard));
        if (bytesRead > 0)
            return;
        this->closeConnection(clientFd, pollIndex);
        return;
    }
    if (!this->readSocketDataToBuffer(conn, clientFd, pollIndex))
        return;
    size_t consumed = 0;

    int status = REQUEST_INCOMPLETE;

    if (conn->chunk_scan_active)
    {
        if (requestTargetsConfiguredCgi(conn) && conn->read_buffer.size() >= LARGE_CGI_BUFFER_THRESHOLD && !this->ensureLargeCgiSlot(conn, clientFd))
            return;
        status = advanceConnectionChunkScan(conn, consumed);
        if (status == REQUEST_OK)
        {
            conn->chunk_scan_active = false;
            status = RequestParser::parseBuffer(conn->read_buffer,
                                                conn->request, &conn->config, consumed);
        }
    }
    else
    {
        status = RequestParser::parseBuffer(conn->read_buffer,
                                            conn->request, &conn->config, consumed);
        if (status == REQUEST_INCOMPLETE && requestUsesChunkedBody(conn->request))
        {
            if (requestTargetsConfiguredCgi(conn) && conn->read_buffer.size() >= LARGE_CGI_BUFFER_THRESHOLD && !this->ensureLargeCgiSlot(conn, clientFd))
                return;
            status = advanceConnectionChunkScan(conn, consumed);
            if (status == REQUEST_OK)
            {
                conn->chunk_scan_active = false;
                status = RequestParser::parseBuffer(conn->read_buffer,
                                                    conn->request, &conn->config, consumed);
            }
        }
    }
    if (status == REQUEST_OK)
    {
        DEBUG_LOG("[ServerManager] Request parsed successfully for FD " << clientFd);
        conn->chunk_scan_active = false;
        conn->chunk_scan_pos = 0;
        conn->chunk_decoded_size = 0;
        conn->chunk_body_limit = 0;
        conn->read_buffer.erase(0, consumed);
        if (conn->read_buffer.empty())
            std::string().swap(conn->read_buffer);
        this->processParsedRequest(conn, clientFd);
    }
    else if (status == REQUEST_INCOMPLETE)
        return;
    else if (status == REQUEST_BODY_TOO_LARGE)
    {
        std::cerr << "[ServerManager] Request body too large on FD "
                  << clientFd
                  << ". Sending 413, then draining remaining input before close."
                  << std::endl;
        conn->close_after_write = false;
        conn->drain_input_before_close = true;
        conn->chunk_scan_active = false;
        conn->chunk_scan_pos = 0;
        conn->chunk_decoded_size = 0;
        conn->chunk_body_limit = 0;
        std::string().swap(conn->read_buffer);
        conn->write_buffer = "HTTP/1.1 413 Payload Too Large\r\n"
                             "Content-Length: 0\r\nConnection: close\r\n\r\n";
        this->setClientEvents(clientFd, POLLOUT);
    }
    else
    {
        std::cerr << "[ServerManager] Request error (" << status
                  << ") on FD " << clientFd
                  << ". Pre-writing 400 response." << std::endl;
        conn->close_after_write = true;
        conn->write_buffer = "HTTP/1.1 400 Bad Request\r\n"
                             "Content-Length: 0\r\nConnection: close\r\n\r\n";
        this->setClientEvents(clientFd, POLLOUT);
    }
}

/**
 * 函数：ServerManager::handleClientWrite
 * 用途：在客户端已经由 poll() 报告 POLLOUT 后，只执行一次 send，并维护部分发送、连接关闭、413 drain 与 keep-alive 状态。
 * 参数来源：clientFd 和 pollIndex 来自 dispatchEvents() 当前 POLLOUT 节点。
 * 实现逻辑：
 *     1. write_buffer 为空时，根据 close_after_write、drain_input_before_close 或 keep-alive 状态完成收尾。
 *     2. write_buffer 非空时只调用一次 ClientSocket::write()。
 *     3. bytes_sent > 0 时删除本次已发送部分；未发完则保留 POLLOUT，等待下一轮 poll。
 *     4. bytes_sent == 0 或 bytes_sent < 0 时都认为本次发送无法继续，立即关闭连接，避免悬挂。
 *     5. 不在 send 后检查 errno，也不再依赖不可达的 -2 返回码。
 */
void ServerManager::handleClientWrite(int clientFd, size_t pollIndex)
{
    std::map<int, Connection *>::iterator it = this->_connections.find(clientFd);
    if (it == this->_connections.end() || it->second == NULL)
        return;
    Connection *conn = it->second;

    if (conn->write_buffer.empty())
    {
        if (conn->close_after_write)
            this->closeConnection(clientFd, pollIndex);
        else if (conn->drain_input_before_close)
            this->setClientEvents(clientFd, POLLIN);
        else
        {
            this->releaseLargeCgiSlot(clientFd);
            conn->clear();
            this->setClientEvents(clientFd, POLLIN);
        }
        return;
    }
    ssize_t bytes_sent = conn->socket->write(conn->write_buffer);

    if (bytes_sent > 0)
    {
        conn->write_buffer.erase(0, bytes_sent);

        if (conn->write_buffer.empty())
        {
            if (conn->close_after_write)
            {
                DEBUG_LOG("[ServerManager] Sent response completely to FD " << clientFd << ". Closing connection per policy.");
                this->closeConnection(clientFd, pollIndex);
            }
            else if (conn->drain_input_before_close)
            {
                DEBUG_LOG("[ServerManager] Sent 413 completely to FD " << clientFd << ". Draining remaining request input before close.");
                this->setClientEvents(clientFd, POLLIN);
            }
            else
            {
                DEBUG_LOG("[ServerManager] Sent response completely to FD " << clientFd << ". Resetting event to POLLIN.");
                this->releaseLargeCgiSlot(clientFd);
                conn->clear(); // 清空旧的请求/响应对象，准备迎接该 FD 上的下一个 HTTP 请求
                this->setClientEvents(clientFd, POLLIN);
            }
        }
    }
    else
    {
        if (bytes_sent == 0)
        {
            DEBUG_LOG("[ServerManager] send made no progress on ready Client FD " << clientFd << ". Closing connection.");
        }
        else
        {
            DEBUG_LOG("[ServerManager] send failed on ready Client FD " << clientFd << ". Closing connection.");
        }
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

    if (res.status == CGI_FINISHED || res.status == CGI_ERROR)
    {
        this->_cgi_read_fd_to_client_map.erase(cgiReadFd);
        this->eraseFdFromPoll(cgiReadFd);
        this->cleanupClientWritePipe(res.clientFd);

        std::map<int, Connection *>::iterator it = this->_connections.find(res.clientFd);
        if (it != this->_connections.end() && it->second != NULL)
        {
            Connection *conn = it->second;

            if (res.status == CGI_FINISHED)
            {
                Response cgiResponse = buildCgiResponse(conn->request, res.rawOutput);
                conn->response = cgiResponse;
            }
            else
                conn->response.createResponse(res.statusCode, "CGI Output Error", conn->config.error_pages);
            conn->write_buffer = conn->response.responseToString();
            this->setClientEvents(res.clientFd, POLLOUT);
        }
    }
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
        std::map<int, Connection *>::iterator it = this->_connections.find(res.clientFd);
        if (it != this->_connections.end() && it->second != NULL)
        {
            Connection *conn = it->second;
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
    for (size_t i = this->_poll_fds.size(); i > 0; --i)
    {
        size_t idx = i - 1;

        if (idx >= this->_poll_fds.size() || this->_poll_fds[idx].fd == -1 || this->_poll_fds[idx].revents == 0)
            continue;
        int activeFd = this->_poll_fds[idx].fd;
        short revents = this->_poll_fds[idx].revents;

        if (revents == 0)
            continue;
        if (this->isCgiReadFd(activeFd))
        {
            if (revents & POLLNVAL)
            {
                this->_cgi_read_fd_to_client_map.erase(activeFd);
                this->eraseFdFromPoll(activeFd);
                continue;
            }
            if (revents & (POLLIN | POLLHUP))
            {
                this->handleCgiRead(activeFd);
            }
            else if (revents & POLLERR)
            {
                std::map<int, int>::iterator it = this->_cgi_read_fd_to_client_map.find(activeFd);
                if (it != this->_cgi_read_fd_to_client_map.end())
                {
                    int clientFd = it->second;
                    this->_cgi_read_fd_to_client_map.erase(it);
                    this->_cgiManager.removeTaskByClientFd(clientFd);

                    Connection *conn = this->_connections[clientFd]; // 假设 client_map 里的一定合法，如果怕也可以继续 find
                    if (conn)
                    {
                        conn->response.createResponse(500, "CGI Read Pipe Error", conn->config.error_pages);
                        conn->write_buffer = conn->response.responseToString();
                        conn->close_after_write = true;
                        this->setClientEvents(clientFd, POLLOUT);
                    }
                }
                this->eraseFdFromPoll(activeFd);
            }
            continue;
        }
        if (this->isCgiWriteFd(activeFd))
        {
            if (revents & POLLNVAL)
            {
                this->_cgi_write_fd_to_client_map.erase(activeFd);
                this->eraseFdFromPoll(activeFd);
                continue;
            }
            if (revents & POLLOUT)
            {
                this->handleCgiWrite(activeFd);
            }
            else if (revents & (POLLERR | POLLHUP))
            {
                std::map<int, int>::iterator it = this->_cgi_write_fd_to_client_map.find(activeFd);
                if (it != this->_cgi_write_fd_to_client_map.end())
                {
                    int clientFd = it->second;
                    this->_cgi_write_fd_to_client_map.erase(it);
                    this->_cgiManager.removeTaskByClientFd(clientFd);
                    Connection *conn = this->_connections[clientFd];
                    if (conn)
                    {
                        conn->response.createResponse(500, "CGI Write Pipe Error", conn->config.error_pages);
                        conn->write_buffer = conn->response.responseToString();
                        conn->close_after_write = true;
                        this->setClientEvents(clientFd, POLLOUT);
                    }
                }
                this->eraseFdFromPoll(activeFd);
            }
            continue;
        }
        if (revents & POLLNVAL)
        {
            this->eraseFdFromPoll(activeFd);
            continue;
        }

        if (revents & (POLLERR | POLLHUP))
        {
            if (this->isListenFd(activeFd))
            {
                std::cerr << "[ServerManager] CRITICAL: Fatal event (" << revents
                          << ") on Listen FD " << activeFd << "!" << std::endl;
                this->_poll_fds[idx].fd = -1; // 不要轻易 close 监听，或者交由上层重启
            }
            else
                this->closeConnection(activeFd, idx);
            continue;
        }
        if (revents & POLLIN)
        {
            if (this->isListenFd(activeFd))
                this->acceptNewConnection(activeFd);
            else
                this->handleClientRead(activeFd, idx);
        }
        if (revents & POLLOUT)
        {
            if (idx < this->_poll_fds.size() && this->_poll_fds[idx].fd == activeFd)
                this->handleClientWrite(activeFd, idx);
        }
    }
}

/*
函数用途：服务器优雅退出时的全量资源回收与清理工作。
参数与变量：无。
实现逻辑：
1. 优雅提示：向控制台打印关闭日志。
2. 终止 CGI：调用 _cgiManager.stopAllTasks() 强行终止并回收所有正在运行的 CGI 子进程及管道。
3. 清理客户端连接：遍历 _connections 容器，安全释放所有活跃的 Connection 动态内存实例，随后清空容器。
4. 销毁监听套接字：遍历 _listen_sockets 容器，释放底层的 ServerSocket 实例并清空相关监听账本。
5. 清空轮询名册：清空全局 pollfd 监视数组成员 _poll_fds。
6. 收尾提示：打印退出成功日志。
*/
void ServerManager::stop()
{
    std::cout << "\n[Server] Shutting down gracefully..." << std::endl;
    this->_cgiManager.stopAllTasks();

    for (std::map<int, Connection *>::iterator it = _connections.begin();
         it != _connections.end(); ++it)
    {
        if (it->second != NULL)
        {
            delete it->second;
        }
    }
    _connections.clear();
    for (size_t i = 0; i < _listen_sockets.size(); ++i)
    {
        if (_listen_sockets[i] != NULL)
        {
            delete _listen_sockets[i];
        }
    }
    _listen_sockets.clear();
    _listen_socket_map.clear();
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
    while (g_loop_running)
    {
        this->prePollCleanup();
        int ret = this->executePoll(poll_error_retries);
        if (ret < 0)
        {
            std::cerr << "[ServerManager] Fatal poll failure limit reached. Breaking main loop." << std::endl;
            break;
        }
        if (ret > 0)
            this->dispatchEvents();
        std::vector<CgiEventResult> timeouts = this->_cgiManager.checkTimeout();
        for (size_t i = 0; i < timeouts.size(); ++i)
        {
            int clientFd = timeouts[i].clientFd;
            int statusCode = timeouts[i].statusCode;
            std::map<int, int>::iterator it = this->_cgi_read_fd_to_client_map.begin();
            while (it != this->_cgi_read_fd_to_client_map.end())
            {
                if (it->second == clientFd)
                {
                    this->eraseFdFromPoll(it->first);
                    this->_cgi_read_fd_to_client_map.erase(it++);
                }
                else
                    ++it;
            }
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
    this->stop();
    std::cout << "[ServerManager] Main loop safely terminated." << std::endl;
}
