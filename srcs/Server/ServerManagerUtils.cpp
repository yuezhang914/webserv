#include "Webserv.hpp"
#include "SessionStore.hpp"
#include "CgiHandler.hpp"
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

/*
成员函数：精准重置客户端 FD 的 poll 监听事件（覆盖原 events，而非 |=）
*/
void ServerManager::setClientEvents(int clientFd, short events)
{
    for (size_t i = 0; i < this->_poll_fds.size(); ++i)
    {
        if (this->_poll_fds[i].fd == clientFd)
        {
            this->_poll_fds[i].events = events; // 💡 物理覆盖！不带 POLLIN
            return;
        }
    }
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
    Connection *conn = this->_connections[clientFd];

    int loop_counter = 0; // 物理计数器

    // 1. 强攻非阻塞 Socket，把内核缓冲区捞干净
    while (true)
    {
        // 只要进入循环就累加。空转超过 1000 次强制熔断，防止卡死主线程！
        if (++loop_counter > 1000)
        {
            std::cerr << "DEAD LOOP DETECTED IN READ VALVE! Force breaking..." << std::endl;
            break;
        }

        ssize_t bytes_read = conn->socket->read(buffer, BUFFER_SIZE - 1);

        if (bytes_read == 0) // EOF（客户端优雅断开）
        {
            std::cout << "[ServerManager] Client FD " << clientFd << " closed connection (EOF)." << std::endl;
            this->closeConnection(clientFd, poll_index);
            return;
        }
        if (bytes_read == -1) // 正常的非阻塞读空，安全退出缓冲区读取
        {
            break;
        }
        if (bytes_read == -2) // 物理崩溃，强行断开
        {
            this->closeConnection(clientFd, poll_index);
            return;
        }
        if (bytes_read > 0)
        {
            buffer[bytes_read] = '\0';
            conn->read_buffer.append(buffer, bytes_read);
        }
    }

    // 2. 解析蓄水池里的数据
    size_t consumed = 0;
    int status = RequestParser::parseBuffer(conn->read_buffer, conn->request, &conn->config, consumed);

    if (status == REQUEST_OK)
    {
        std::cout << "[ServerManager] Request parsed successfully for FD " << clientFd << std::endl;
        conn->read_buffer.erase(0, consumed);

        // 💡 冲突解决：完美缝合 SessionStore 机制！
        static SessionStore sessionStore;
        Response res = buildResponse(conn->request, sessionStore);

        // 3. 检查这到底是不是一个隐藏的 CGI 请求
        std::string script_path;
        std::string interpreter_path;

        if (res.getHeader("X-Internal-CGI-Path", script_path))
        {
            res.getHeader("X-Internal-CGI-Interpreter", interpreter_path);

            CgiHandler cgi(conn->request, script_path, interpreter_path);
            CgiFds fds = cgi.async_launch();

            // ❌ CGI 启动失败 500 熔断
            if (fds.pid < 0 || fds.read_fd < 0 || fds.write_fd < 0)
            {
                std::cerr << "[CGI] Error: Failed to spawn CGI process for client " << clientFd << std::endl;
                conn->response.createResponse(500, "CGI Spawn Failed", conn->config.error_pages);
                conn->write_buffer = conn->response.responseToString();
                conn->close_after_write = true;

                // 💡 失败时直接激活 POLLOUT 准备发送 500 错误页
                this->setClientEvents(clientFd, POLLOUT);
                return;
            }

            // 🎯 【读端账本登记】
            this->_cgi_read_fd_to_client_map[fds.read_fd] = clientFd;

            conn->is_cgi = true;
            conn->cgi_read_fd = fds.read_fd;
            conn->cgi_pid = fds.pid;
            conn->cgi_body_bytes_sent = 0;
            conn->cgi_started_at = std::time(NULL); // 装填物理起始时间戳！

            std::string().swap(conn->cgi_output_buffer);

            // 1️⃣ 读端（CGI 管道）永远注册 POLLIN
            this->registerFdToPoll(fds.read_fd, POLLIN);

            // 2️⃣ 写端（CGI 管道）按需注册
            if (!conn->request.getBody().empty())
            {
                conn->cgi_write_fd = fds.write_fd;
                this->_cgi_write_fd_to_client_map[fds.write_fd] = clientFd;
                this->registerFdToPoll(fds.write_fd, POLLOUT);
            }
            else
            {
                // 无 Body：直接通过辅助函数优雅关闭写端！
                conn->cgi_write_fd = fds.write_fd; // 临时赋值给 conn 以便 closeCgiWritePipe 识别
                this->closeCgiWritePipe(conn);
            }

            // 💡 3️⃣ 🎯 【核心防线：暂停客户端 Socket 监听，防止 Request 被覆盖！】
            this->setClientEvents(clientFd, 0);

            std::cout << "[⚡ WebServ Core] Client " << clientFd << " successfully split into CGI pipeline, client read paused." << std::endl;
            return;
        }
        else
        {
            // 4. 普通静态响应，直接准备发送
            conn->write_buffer = res.responseToString();

            // 💡 普通静态响应生成后，也只监听 POLLOUT（发完再恢复 POLLIN）
            this->setClientEvents(clientFd, POLLOUT);
        }
    }
    else if (status == REQUEST_INCOMPLETE)
    {
        std::cout << "[ServerManager] Request incomplete for FD " << clientFd << ". Waiting for more data..." << std::endl;
    }
    else
    {
        std::cerr << "[ServerManager] Request error (" << status << ") on FD " << clientFd << ". Pre-writing 400 response." << std::endl;
        conn->close_after_write = true;

        std::string error_response = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        conn->write_buffer += error_response;

        // 在保持原有读管道监控的同时，追加写事件 400 报错
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
void ServerManager::handleClientWrite(int clientFd, size_t poll_index)
{
    Connection *conn = this->_connections[clientFd];

    // 防御：如果发件箱本来就是空的，直接返回
    if (conn->write_buffer.empty())
        return;
    // 1. 推进缓冲区队列
    ssize_t bytes_sent = conn->socket->write(conn->write_buffer);
    // 物理测谎判定
    if (bytes_sent == -2) // 致命故障（如客户端强行断开且引发了 EPIPE 等，返回 -2）
    {
        this->closeConnection(clientFd, poll_index);
        return;
    }
    if (bytes_sent == -1) // 正常的非阻塞写阻碍（缓冲区满了，直接返回等待下一轮 POLLOUT）
        return;
    // 2. 发出了多少，就从发件箱里切掉多少！
    if (bytes_sent > 0)
        conn->write_buffer.erase(0, bytes_sent);
    // 只有当这一轮的写缓冲区被彻底排空
    if (conn->write_buffer.empty())
    {
        // C++98 彻底释放内存空间防止虚胖
        std::string().swap(conn->write_buffer);
        // 3. 检查是否有延时自毁标签
        if (conn->close_after_write)
        {
            std::cout << "[ServerManager] Sent response completely. Now safely closing Client FD " << clientFd << std::endl;
            this->closeConnection(clientFd, poll_index);
            return;
        }
        // 4. 重洗 Request 智囊大脑，干干净净迎接下一个请求
        conn->clearRequest();
        // 5. 正常情况下（Keep-Alive 连接），发完回包后重新将 events 改回读（POLLIN），等待下一个请求到达
        this->_poll_fds[poll_index].events = POLLIN;
    }
}

/*
函数用途：作为多路复用雷达网的防卫外科车间，在 _poll_fds 阵列中纵向检索指定 FD，找到后将其物理抹除并缩容 vector 舱位，严防悬空 FD 污染 poll 监听。
参数与变量：
- targetFd (传入参数)：int，亟待从多路复用雷达网中注销剔除的物理文件描述符（如 cgi_read_fd 或 cgi_write_fd）。
- i (局部卡尺变量)：size_t，遍历 _poll_fds 动态阵列的检索游标。
实现逻辑：
1. 🛡️ 边界防卫：点验 targetFd 有效性，若传入的是 -1（未开通状态），当场优雅折返。
2. 🔍 物理检索：自头至尾正向扫描 this->_poll_fds 动态向量阵列。
3. 🪓 物理剜除与缩容：一旦匹配到 this->_poll_fds[i].fd == targetFd，立刻调用 vector::erase(begin() + i)
   将其从内存轨道上彻底剔除并压缩阵列，随后断开循环，向大管家交割绝对干净的雷达网时空！
*/
void ServerManager::_eraseFdFromPoll(int targetFd)
{
    if (targetFd == -1)
        return;

    for (size_t i = 0; i < this->_poll_fds.size(); ++i)
    {
        if (this->_poll_fds[i].fd == targetFd)
        {
            // 🧹 物理剔除并让 vector 自动收缩舱位，绝不留悬空垃圾
            this->_poll_fds.erase(this->_poll_fds.begin() + i);
            break;
        }
    }
}

void ServerManager::cleanupConnectionCgi(Connection *conn)
{
    if (conn == NULL)
        return;

    // 清理读端管道（若挂在 poll_fds 上，也需在调用处或此处清除，防悬空）
    if (conn->cgi_read_fd != -1)
    {
        this->_eraseFdFromPoll(conn->cgi_read_fd);
        this->_cgi_read_fd_to_client_map.erase(conn->cgi_read_fd);
        ::close(conn->cgi_read_fd);
        conn->cgi_read_fd = -1;
    }

    // 清理写端管道
    if (conn->cgi_write_fd != -1)
    {
        this->_eraseFdFromPoll(conn->cgi_write_fd);
        this->_cgi_write_fd_to_client_map.erase(conn->cgi_write_fd);
        ::close(conn->cgi_write_fd);
        conn->cgi_write_fd = -1;
    }

    // 🪓 斩杀子进程，防止客户端中途断连留下僵尸进程！
    if (conn->cgi_pid > 0)
    {
        ::kill(conn->cgi_pid, SIGKILL);
        ::waitpid(conn->cgi_pid, NULL, 0);
        conn->cgi_pid = -1;
    }
    conn->is_cgi = false;
}
/*
函数用途：作为客户端断连战后总务车间，优雅回收 Connection 堆内存资产，依靠 RAII 自动闭合物理 FD，并重置多路复用雷达槽位。
实现逻辑：
1. 🔍 账本反查：在 _connections 名册中定位该 clientFd 的 Connection* 实体。
2. 🧹 CGI 连带清场：如果该连接中途断连且正挂着 CGI，强制 kill 子进程、回收 PID、并注销 CGI 管道 FD（彻底封杀僵尸进程）。
3. 💎 资源回收（RAII 顺藤摸瓜）：
   - 执行 delete connection;
   - 触发 Connection::~Connection() -> delete socket -> 触发 ClientSocket::~ClientSocket()；
   - 由 ClientSocket 的析构函数唯一地、安全地执行 ::close(clientFd)，绝无重复关闭（Double Close）风险！
4. 🗑️ 账本注销：从 _connections 名册中 erase 清除指针节点。
5. 📡 雷达网擦除：将 _poll_fds 中对应的 pollIndex 槽位重置为 -1（留给 prePollCleanup 洗舱车间物理剔除），严防悬空 FD！
*/
void ServerManager::closeConnection(int clientFd, size_t pollIndex)
{
    std::map<int, Connection *>::iterator it = this->_connections.find(clientFd);

    if (it != this->_connections.end())
    {
        Connection *connection = it->second;

        // 1. 🛡️ 先清理该连接关联的 CGI pipe 和子进程（彻底封杀僵尸进程）
        this->cleanupConnectionCgi(connection);

        // 2. 🏛️ RAII 完美闭环：由 delete 触发 ClientSocket 析构函数关闭 clientFd
        delete connection;

        // 3. 🧹 清除名册账本节点
        this->_connections.erase(it);
    }

    // 4. 📡 物理抹去 poll 雷达网槽位
    if (pollIndex < this->_poll_fds.size())
    {
        this->_poll_fds[pollIndex].fd = -1;
        this->_poll_fds[pollIndex].events = 0;
        this->_poll_fds[pollIndex].revents = 0;
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

    int clientFd = ::accept(listenFd, (struct sockaddr *)&client_addr, &client_len);
    if (clientFd < 0)
    {
        std::cerr << "[Acceptor] Error: accept() failed on Listen FD " << listenFd << std::endl;
        return;
    }
    // 1. 创建ClientSocket
    ClientSocket *p_socket = new ClientSocket(clientFd);

    // 2. 创建Connection 指针
    Connection *conn = new Connection();
    conn->socket = p_socket;

    // 3. 安全抽取虚拟主机配置实体
    std::map<int, ServerConfig>::iterator config_it = this->_listen_socket_map.find(listenFd);
    if (config_it != this->_listen_socket_map.end())
    {
        conn->config = config_it->second;
    }

    // 4. 彻底锁进大户籍 Map 账本
    this->_connections[clientFd] = conn;

    // 5. 挂载到 poll 雷达网上监听读事件
    this->registerFdToPoll(clientFd, POLLIN);

    std::cout << "[ServerManager] Accepted new connection -> Allocated Client FD: "
              << clientFd << " (SUCCESSFULLY SET O_NONBLOCK!)" << std::endl;
}
