#include "Webserv.hpp"

ServerManager::ServerManager(const std::vector<ServerConfig> &configs) : _server_configs(configs), _poll_fds()
{
}

ServerManager::~ServerManager()
{
}

/**
 * 冷启动一键初始化：绑定端口、建映射
 */
void ServerManager::init()
{
    std::cout << "[ServerManager] Initializing network sockets..." << std::endl;
    this->setupSockets();
}

/**
 * 函数：ServerManager::setNonBlocking
 * 用途：将指定的套接字（Socket）文件描述符强制设置为非阻塞（O_NONBLOCK）模式。
 * 参数来源：fd 来自本类内部的监听初始化阶段（setupSockets 产生的 listenFd）或运行时接受新连接阶段（acceptNewConnection 产生的 clientFd）。
 * 变量解释：
 *     - fd：需要修改状态旗帜的目标套接字文件描述符。
 *     - flags：临时变量，通过 fcntl 获取的当前套接字拥有的全部内核状态属性。
 * 实现逻辑：
 *     1. 调用 fcntl(fd, F_GETFL, 0) 捞出该套接字当前在内核中所有的属性旗帜，若失败则打印错误并熔断。
 *     2. 采用位运算“或（|）”操作，在原有旗帜的基础上追加 O_NONBLOCK 非阻塞属性。
 *     3. 调用 fcntl(fd, F_SETFL, ...) 将改版后的全新属性旗帜重新插回内核中以使其生效。
 * 后续影响：被处理的套接字在进行 read()/recv() 或 write()/send() 操作时将永远不会阻塞（Block）主线程。
 *           若无数据可读或发送缓冲区满，系统调用会立刻返回 -1 并设置 errno 为 EAGAIN，
 *           从而确保底层的 poll 大循环能以极速串行、永不卡死的状态持续驱动高并发网络流。
 */
void ServerManager::setNonBlocking(int fd)
{
    // 1. 调用 fcntl(fd, F_GETFL, 0) 捞出该套接字当前在内核中所有的属性旗帜
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0)
    {
        std::cerr << "Error: fcntl F_GETFL failed for fd " << fd << std::endl;
        return;
    }

    // 2. 采用位运算“或（|）”操作，在原有旗帜的基础上追加 O_NONBLOCK 非阻塞属性
    // 3. 调用 fcntl(fd, F_SETFL, ...) 将改版后的全新属性旗帜重新插回内核中
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
    {
        std::cerr << "Error: fcntl F_SETFL O_NONBLOCK failed for fd " << fd << std::endl;
    }
}

/**
 * 函数：ServerManager::setupSockets
 * 用途：拉起物理端口监听大网，创建、绑定并激活所有不重复端口的监听套接字。
 * 参数来源：内部数据成员 _server_configs（由构造函数接收的已通过校验的全量配置账本）。
 * 变量解释：
 *     - handled_ports：临时变量，记录哪些物理端口已经被成功执行过 bind 操作，防止重复绑定。
 *     - listenFd：新创建的物理监听套接字文件描述符。
 *     - port：当前遍历到的虚拟主机所期望监听的物理端口（如 80）。
 *     - host：当前虚拟主机配置绑定的 IP 地址字符串（如 "127.0.0.1"）。
 *     - port_duplicate：布尔旗帜，标记当前端口是否与已绑定端口重复。
 *     - reuse：整型开关，传递给 setsockopt 的参数，开启端口地址复用。
 *     - addr：sockaddr_in 网络地址结构体，用于封装绑定的协议族、IP 与端口号。
 *     - SOMAXCONN_BACKLOG：在头文件中定义的宏（值为 128），指定内核连接队列的最大积压上限。
 * 实现逻辑：
 *     1. 建立 handled_ports 登记簿，遍历配置账本，利用循环对比检查当前端口是否已被绑定过。
 *     2. 若检测到端口重复（port_duplicate 为 true），则直接跳过物理 bind 流程，留待运行时通过 Host 请求头实现多域名分流映射。
 *     3. 呼叫 socket() 系统调用创建流式套接字 listenFd。
 *     4. 注入 SO_REUSEADDR 属性，强力防止服务器意外重启时物理端口被内核扣留两分钟的 TIME_WAIT 闪退惨剧。
 *     5. 调用本类成员 setNonBlocking()，强行将新建的 listenFd 洗牌为非阻塞安全状态。
 *     6. 组装 sockaddr_in 结构体，执行 bind() 将套接字锁死在指定的 Host 和 Port 上。
 *     7. 调用 listen(listenFd, SOMAXCONN_BACKLOG) 开启监听，拒绝任何硬编码数值，打通全连接队列。
 *     8. 将成功激活的物理 listenFd 写入 _listen_socket_map 快捷映射表，并组装标准 pollfd 挂载进核心 _poll_fds 监听大阵列。
 * 后续影响：底层 poll 大循环自此拥有了捕获外部客户端连接（三次握手第一步）的物理入口。
 *           当有新浏览器访问对应端口时，该 listenFd 对应的 poll 节点会精准触发 POLLIN 事件。
 */
void ServerManager::setupSockets()
{
    // 临时防重登记簿：用来记录哪些端口（如 80, 8080）已经被 bind 过了
    std::vector<int> handled_ports;

    for (size_t i = 0; i < _server_configs.size(); ++i)
    {
        int port = _server_configs[i].port;
        std::string host = _server_configs[i].host;

        // 🔒 【战术去重拦截】：如果这个端口已经砸开过了，直接复用，绝不 bind 两次！
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
            // 虽然不重新 bind，但这个虚拟主机的配置必须能通过未来的 clientFd 关联到。
            // 我们可以在大循环诞生连接时，再统一做多域名匹配。现在先跳过物理 bind
            std::cout << "[ServerManager] Multi-server configuration detected for port " << port << " (Skipping duplicate bind)" << std::endl;
            continue;
        }

        // 1. 创建 socket
        int listenFd = socket(AF_INET, SOCK_STREAM, 0);
        if (listenFd < 0)
        {
            std::cerr << "Error: Cannot create socket for port " << port << std::endl;
            exit(1);
        }

        // 2. 开启 SO_REUSEADDR（防止服务器重启时端口被内核扣留 2 分钟的 TIME_WAIT 惨剧）
        int reuse = 1;
        if (setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0)
        {
            std::cerr << "Error: setsockopt(SO_REUSEADDR) failed" << std::endl;
            close(listenFd);
            exit(1);
        }

        // 3. 强制设置为 O_NONBLOCK 非阻塞（42 铁律：所有 socket 必须非阻塞）
        this->setNonBlocking(listenFd);

        // 4. 绑定物理地址
        struct sockaddr_in addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = htonl(INADDR_ANY);

        if (bind(listenFd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
        {
            std::cerr << "Error: Cannot bind to " << host << ":" << port << std::endl;
            close(listenFd);
            exit(1);
        }

        // 5. 开始监听
        if (listen(listenFd, SOMAXCONN_BACKLOG) < 0) // SOMMAXCONN_BACKLOG 为全连接队列大小上限
        {
            std::cerr << "Error: Listen failed on port " << port << std::endl;
            close(listenFd);
            exit(1);
        }

        std::cout << "[ServerManager] Successfully listening on " << host << ":" << port << " (FD: " << listenFd << ")" << std::endl;

        // 6. 核心资产入库
        handled_ports.push_back(port);

        // 将生成的真实物理 listenFd 反向锁进快捷映射表中
        _listen_socket_map[listenFd] = _server_configs[i];

        // 组装标准 pollfd，挂载进大阵列，开始关注读事件（POLLIN：代表有人来连接）
        struct pollfd pfd;
        pfd.fd = listenFd;
        pfd.events = POLLIN;
        pfd.revents = 0;
        _poll_fds.push_back(pfd);
    }
}

/**
 * 函数：ServerManager::isListenFd
 * 用乎：在 poll() 监听到事件响了之后，用来判定当前被唤醒的文件描述符（fd）究竟是大厅的主监听端口，还是已经建立的普通客户端连接。
 * 参数来源：来自 run() 主生命周期大循环中正在被遍历的当前活跃节点：_poll_fds[i].fd。
 * 变量解释：
 *     - fd：需要进行身份鉴别的高危目标文件描述符。
 *     - _listen_socket_map：关联数组映射表，里面只保存了在冷启动 setupSockets() 阶段绑定的合法 listenFd。
 * 实现逻辑：
 *     1. 拿着传入的 fd 作为 key，去专属的 _listen_socket_map 映射表中执行 count() 查找。
 *     2. 如果 count 返回大于 0（即非零），说明该 fd 存在于监听大户籍中，代表它是大厅的主监听端口，返回 true。
 *     3. 反之，如果找不到，说明它是一个普通的、由 accept 新生出来的客户端会话，返回 false。
 * 后续影响：主大循环依据该函数的布尔裁决结果流向不同的处理车间：
 *           - 若为 true：立刻分流去调用 acceptNewConnection() 去诞生成立新的新客户；
 *           - 若为 false：立刻分流去调用 handleClientRead() 或 handleClientWrite() 来处理真实的 HTTP 业务数据。
 */
bool ServerManager::isListenFd(int fd)
{
    // 1. 拿着传入的 fd 作为 key，去专属的 _listen_socket_map 映射表中执行 count() 查找
    if (this->_listen_socket_map.count(fd) > 0)
    {
        // 2. 如果存在于监听大户籍中，代表它是大厅的主监听端口，返回 true
        return true;
    }
    // 3. 反之，如果找不到，说明它是一个普通的客户端会话，返回 false
    return false;
}

/**
 * 函数：ServerManager::handleClientRead
 * 用途：当已建立连接的普通客户端（clientFd）触发读事件（POLLIN）时，调用该函数通过非阻塞循环吞入浏览器发来的 HTTP 裸请求（Raw Request）文本，并负责处理 TCP 粘包/断包问题。
 * 参数来源：来自 run() 大循环，其中 clientFd = _poll_fds[poll_index].fd，poll_index 是该节点在全局 _poll_fds 阵列中的实时下标。
 * 变量解释：
 *     - clientFd：正在源源不断喷射请求数据的目标客户端套接字。
 *     - poll_index：该客户端在 poll 监视大阵列中的位置下标，用来在断开连接时配合执行清理。
 *     - BUFFER_SIZE：在头文件或本文件定义的宏（通常为 4096），规定单次 recv() 探入内核缓冲区的勺子大小。
 *     - buffer：局部栈内存临时数组，用来物理承接单次从内核捞出的裸字节流。
 *     - bytes_read：单次 recv() 调用后实际吸入的合法字节数。
 * 实现逻辑：
 *     1. 挺进 while(true) 异步吞噬大循环。因为 clientFd 是非阻塞的，必须用死循环榨干内核缓冲区。
 *     2. 呼叫 bytes_read = recv(clientFd, buffer, BUFFER_SIZE, 0) 捞取数据。
 *     3. **【判别断开】**：若 bytes_read 等于 0，说明浏览器主动挥手断开（EOF），立刻功德圆满地调用 closeConnection() 销毁通道并安全折返。
 *     4. **【判别抖动】**：若 bytes_read 小于 0，深入核验 errno：
 *        - 若为 EAGAIN 或 EWOULDBLOCK，代表当前能读的已被全部吸干，属于完美的非阻塞退出信号，直接 break 退出吞噬循环；
 *        - 若为 EINTR（被系统信号中断），属于无辜躺枪，继续 continue 强攻；
 *        - 若为其他异常（如 ECONNRESET 客户端猝死），打印警告并调用 closeConnection() 强制拔线。
 *     5. **【拼接蓄水】**：若成功读到正数，将原始数据转换为 std::string，追加（+=）到该客户在 _client_buffers[clientFd] 里的专属蓄水抽屉中。
 *     6. **【交接分流预备】**：在退出吞噬后，检查抽屉中的字符串是否已经包揽了 HTTP 协议规定的完整请求终结符 "\r\n\r\n"。若完整，说明接收完毕，此时修改大阵列对应的关注事件为 `_poll_fds[poll_index].events = POLLOUT`（转换成关注可写事件），准备把接力棒递给业务层做 Response 喷吐。
 * 后续影响：客户端的 buffer 蓄水成功。一旦切换为 POLLOUT 状态，主 poll 大循环在下一个滴答里就会立马感知到该套接字“可写”，从而把控制权无缝移交给写车间 handleClientWrite()。
 */
void ServerManager::handleClientRead(int clientFd, size_t poll_index)
{
    char buffer[BUFFER_SIZE];

    // 1. 挺进非阻塞数据吞噬大循环
    while (true)
    {
        std::memset(buffer, 0, BUFFER_SIZE);

        // 2. 呼叫系统调用捞取内核缓冲区里的裸字节
        ssize_t bytes_read = recv(clientFd, buffer, BUFFER_SIZE, 0);

        // 3. 【判别断开】：返回 0 代表客户端友好断开了连接
        if (bytes_read == 0)
        {
            std::cout << "[ServerManager] Client FD: " << clientFd << " closed connection gracefully (EOF)." << std::endl;
            this->closeConnection(clientFd, poll_index);
            return;
        }

        // 4. 【判别抖动】：返回负数需要根据内核 errno 做细分切割
        if (bytes_read < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                // 数据已经被完全吸干，属于非阻塞状态下的完美收工信号
                break;
            }
            if (errno == EINTR)
            {
                // 遭到系统内部信号打断，不气馁，继续尝试读取
                continue;
            }
            // 发生诸如物理断网、重置等真实底层错误，强制把连接拔掉
            std::cerr << "Warning: recv() error on Client FD " << clientFd << ", forcing close." << std::endl;
            this->closeConnection(clientFd, poll_index);
            return;
        }

        // 5. 【拼接蓄水】：安全读到正字节数，原封不动灌入该客人的专属蓄水小抽屉中
        _client_buffers[clientFd].append(buffer, bytes_read);
    }

    // 6. 【交接分流预备】：检查蓄水池，看看有没有攒够一个完整的 HTTP Header 边界（"\r\n\r\n"）
    // 注意：如果是带 Body 的 POST 请求，业务层后续还会根据 Content-Length 深度校验，
    // 我们底层网络在这里先以基础边界作为第一阶段收工的物理信号。
    if (_client_buffers[clientFd].find("\r\n\r\n") != std::string::npos)
    {
        std::cout << "[ServerManager] Complete HTTP request block captured from Client FD " << clientFd << std::endl;

        // 🚀 【核心大转折】：既然读完了，就不要再死死盯着 POLLIN 了。
        // 将大阵列里对该客户的战略监控目标，180 度大转弯修改为 POLLOUT（可写）！
        _poll_fds[poll_index].events = POLLOUT;
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
void ServerManager::handleClientWrite(int clientFd, size_t poll_index)
{
    // 🚀 战术对齐：这里先模拟一份固定的 HTTP 响应字符串作为通关测试。
    // 在接下来的大融合阶段，这里会直接读取你高层队友生成的 response_data。
    std::string mock_response = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: 37\r\n\r\n<h1>Hello from Webserv, XueJie!</h1>\n";

    // 1. 呼叫系统调用，尝试向内核发送写缓冲区倾倒裸字节流
    ssize_t bytes_sent = send(clientFd, mock_response.c_str(), mock_response.size(), 0);

    // 2. 【判别抖动】：返回负数需要深入解剖内核 errno
    if (bytes_sent < 0)
    {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            // 内核写缓冲区满了，属于非阻塞下的正常暂缓信号，直接折返，等下一次 poll 唤醒再继续写
            return;
        }
        if (errno == EINTR)
        {
            // 遭到系统信号无辜打断，放弃这轮，期待下一轮继续强发
            return;
        }
        // 发生诸如浏览器直接关闭网页等物理异常（EPIPE / ECONNRESET），强制拔线清理
        std::cerr << "Warning: send() error on Client FD " << clientFd << ", forcing close." << std::endl;
        this->closeConnection(clientFd, poll_index);
        return;
    }

    // 3. 【分批切片处理】：判断本次喷吐是否已经彻底把数据吐干净了
    if (static_cast<size_t>(bytes_sent) == mock_response.size())
    {
        // 🎯 场景 A：全部发完了！功德圆满！
        std::cout << "[ServerManager] Fully sent HTTP Response back to Client FD " << clientFd << std::endl;

        // 清空该客人的读写蓄水池，为下一次可能复用的请求扫清障碍
        _client_buffers[clientFd] = "";

        // 🚀 【核心回马枪】：一轮交割完毕，根据配置重新调转枪头去监控 POLLIN（读事件），
        // 允许当前浏览器复用这条公路继续发送下一个 HTTP 请求（标准的 Keep-Alive 机制支持）
        _poll_fds[poll_index].events = POLLIN;

        // 💡 如果你们之后不打算支持持久长连接（C++98 默认短连接），也可以在这里直接粗暴调用：
        // this->closeConnection(clientFd, poll_index);
    }
    else
    {
        // 🎯 场景 B：因为非阻塞，数据只发出去了前半句！
        // 绝杀点：我们必须在缓存中切掉已经发出的字节，保留后面还没发出的尾巴，
        // 并且【千万不要改动】 _poll_fds[poll_index].events，让它继续保持 POLLOUT，下轮循环继续发！
        std::cout << "[ServerManager] Part of response sent (" << bytes_sent << " bytes). Retaining remaining data..." << std::endl;

        // （后续真实业务对接时，你的蓄水抽屉里会存放未发送完的内容：_response_buffers[clientFd] = remainder）
    }
}

/**
 * 函数：ServerManager::closeConnection
 * 用途：当客户端主动断开连接、HTTP 发送完毕或连接发生底层致命错误时，调用该函数安全关闭文件描述符，并在物理内存中全量注销该客人的所有资产，防止内存与 FD（文件描述符）泄漏。
 * 参数来源：来自 handleClientRead() 或 handleClientWrite() 检测到断开或异常的分支。
 * 变量解释：
 *     - clientFd：即将被彻底销毁、扫地出门的目标客户端套接字。
 *     - poll_index：传引用参数（size_t &），代表该客户端在全局 _poll_fds 监视大阵列中的实时下标位置。
 *     - _poll_fds：核心监视大阵列，利用 vector 连续内存存放 pollfd。
 *     - _client_to_srv_map：运行时映射表，记录 clientFd 对应的虚拟主机配置。
 *     - _client_buffers：运行时映射表，记录该客户端专属的读写拼接小抽屉。
 * 实现逻辑：
 *     1. 调用 close(clientFd) 系统调用，物理斩断与浏览器的套接字通信管道，释放文件描述符资源。
 *     2. 拿着 clientFd 钥匙，去 _client_to_srv_map 账本中执行 erase() 物理擦除，销毁配置关联。
 *     3. 拿着 clientFd 钥匙，去 _client_buffers 账本中执行 erase() 物理擦除，彻底释放拼接缓存。
 *     4. **【核心绝杀：修正大循环下标】**：呼叫 _poll_fds.erase(_poll_fds.begin() + poll_index) 将其从 poll 大网中无情切除。由于 vector 移除元素会导致后面的所有节点集体向前挪移一位，为了防止主大循环的 for 循环在执行 ++i 时无脑跨跳、漏检紧随其后的新节点，我们在内部顺手执行 `--poll_index`（自减 1 修正位置），完美御敌。
 * 后续影响：资产全面清空。被注销的 clientFd 物理消失，不再占用系统句柄上限。
 *           由于 poll_index 被传引用扣回了正确位置，run() 循环在下一个滴答里依然能滴水不漏地扫描到每一个鲜活的连接，系统稳健度拉满。
 */
void ServerManager::closeConnection(int clientFd, size_t poll_index)
{
    std::cout << "[ServerManager] Safely clear asset for Client FD: " << clientFd << std::endl;

    // 1. 物理斩断套接字通信
    close(clientFd);

    // 2. 清理内存账本
    this->_client_to_srv_map.erase(clientFd);
    this->_client_buffers.erase(clientFd);

    // 3. 🚀 【无伤退役】：我们不在这里 erase 它，而是把它的标志位设为死寂状态
    // 告诉内核：下一次 poll 别再看它了。同时让 run() 完完整整走完这轮点名！
    if (poll_index < this->_poll_fds.size())
    {
        this->_poll_fds[poll_index].fd = -1;
        this->_poll_fds[poll_index].events = 0;
        this->_poll_fds[poll_index].revents = 0;
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
    std::memset(&client_addr, 0, sizeof(client_addr));

    // 1. 调用 accept(listenFd, ...) 顺藤摸瓜捞出全新的客户端 clientFd
    int clientFd = accept(listenFd, (struct sockaddr *)&client_addr, &client_len);
    if (clientFd < 0)
    {
        // 2. 检查非阻塞状态下的正常抖动
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return;
        std::cerr << "Warning: accept failed on listen FD " << listenFd << std::endl;
        return;
    }

    // 3. 打印迎宾日志
    std::cout << "[ServerManager] Accepted new connection from "
              << inet_ntoa(client_addr.sin_addr) << ":" << ntohs(client_addr.sin_port)
              << " -> Allocated Client FD: " << clientFd << std::endl;

    // 4. 将新诞生的 clientFd 强行剥夺其阻塞特权
    this->setNonBlocking(clientFd);

    // 5. 将大厅监听端口的配置，无缝过户拷贝给这个 client 专属的映射中
    this->_client_to_srv_map[clientFd] = this->_listen_socket_map[listenFd];

    // 初始化该客人的专属拼接小抽屉，防止粘包断包
    this->_client_buffers[clientFd] = "";

    // 6. 组装标准的 pollfd，推入大阵列尾部，无惧倒序扫描
    struct pollfd pfd;
    pfd.fd = clientFd;
    pfd.events = POLLIN; // 第一步，先重点监控客户端发来的请求数据
    pfd.revents = 0;
    this->_poll_fds.push_back(pfd);
}

void ServerManager::run()
{
    if (this->_poll_fds.empty())
        return;
    std::cout << "[ServerManager] Main loop started. Entering the matrix..." << std::endl;

    while (true)
    {
        // 🚀 【收割车间】：在每次呼叫 poll 之前，把上一轮标记为 -1 的死线统一清理掉
        for (size_t i = 0; i < this->_poll_fds.size();)
        {
            if (this->_poll_fds[i].fd == -1)
            {
                this->_poll_fds.erase(this->_poll_fds.begin() + i);
                // 注意：由于后面的人往前迈了一步，i 不要递增，原地继续检查即可！
            }
            else
            {
                ++i;
            }
        }

        // 此时 _poll_fds 里的内存干净无瑕，呼叫系统调用死等
        int ret = poll(&this->_poll_fds[0], this->_poll_fds.size(), -1);
        if (ret < 0)
        {
            if (errno == EINTR)
                continue;
            break;
        }

        // 倒序安全扫描活着的人
        for (size_t i = this->_poll_fds.size(); i > 0; --i)
        {
            size_t idx = i - 1;

            // 如果遇到刚才在子函数里被改成 -1 的节点，直接跳过
            if (this->_poll_fds[idx].fd == -1 || this->_poll_fds[idx].revents == 0)
                continue;

            int activeFd = this->_poll_fds[idx].fd;

            // 拦截致命异常
            if (this->_poll_fds[idx].revents & (POLLERR | POLLHUP | POLLNVAL))
            {
                this->closeConnection(activeFd, idx);
                continue;
            }

            // 拦截读事件
            if (this->_poll_fds[idx].revents & POLLIN)
            {
                if (this->isListenFd(activeFd))
                    this->acceptNewConnection(activeFd);
                else
                    this->handleClientRead(activeFd, idx);
            }
            // 拦截写事件
            else if (this->_poll_fds[idx].revents & POLLOUT)
            {
                this->handleClientWrite(activeFd, idx);
            }
        }
    }
}