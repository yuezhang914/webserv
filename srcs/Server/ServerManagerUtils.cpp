#include "Webserv.hpp"

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
    Connection &conn = this->_connections[clientFd];

    // 1. 强攻非阻塞 Socket，把内核缓冲区捞干净
    while (true)
    {
        ssize_t bytes_read = conn.socket->read(buffer, BUFFER_SIZE - 1);

        if (bytes_read == 0) // EOF（客户端优雅断开）
        {
            std::cout << "[ServerManager] Client FD " << clientFd << " closed connection (EOF)." << std::endl;
            this->closeConnection(clientFd, poll_index);
            return;
        }
        if (bytes_read == -1) // 正常的非阻塞读空，安全 break，绝对不往下走！
            break;
        if (bytes_read == -2) // 物理崩溃，强行断开
        {
            this->closeConnection(clientFd, poll_index);
            return;
        }
        // 只有当 bytes_read 严格大于 0 时，才允许操作 buffer！
        if (bytes_read > 0)
        {
            buffer[bytes_read] = '\0';                   // 绝对安全的边界截断
            conn.read_buffer.append(buffer, bytes_read); // 指明长度，绝不发生越界字符串扫描！
        }
    }
    // 2. 解析蓄水池里的数据
    size_t consumed = 0;
    int status = RequestParser::parseBuffer(conn.read_buffer, conn.request, &conn.config, consumed);
    if (status == REQUEST_OK)
    {
        std::cout << "[ServerManager] Request parsed successfully for FD " << clientFd << std::endl;
        // 卡尺精准裁剪，把已经解析掉的请求头部从蓄水池里切掉
        conn.read_buffer.erase(0, consumed);

        // 动态判定这次请求是不是要走 CGI 脚本
        std::string path = conn.request.getPath();
        if (path.find("/cgi-bin/") != std::string::npos || path.find(".py") != std::string::npos)
        {
            std::cout << "[ServerManager] ROUTING TO CGI WORKSHOP: " << path << std::endl;
            // 修正为拼好 root 的“硬核本地相对路径”：
            std::string real_script_path = "srv/www" + path; // 拼装出 "srv/www/cgi-bin/login.py"
            std::cout << "[ServerManager] Executing real local CGI path: " << real_script_path << std::endl;
            // 1. 实例化我们刚刚千锤百炼打磨好的 CGI 执行官
            // 传入当前请求的智囊大脑，以及脚本在服务器本地的真实物理路径
            CgiHandler cgi(conn.request, real_script_path);
            // 2. 执行物理一写一读，父子管道对流，拿到子进程 Python 吐出来的原始结晶
            std::string cgi_raw_output = cgi.execute();
            // 3. 扔进包装工厂，智能穿上 HTTP 状态行和必要的 Headers 外壳
            std::string final_http_packet = cgi.buildHttpResponse(cgi_raw_output);
            // 4. 彻底清洗发件箱，注入全新出炉的 CGI 动态响应报文
            std::string().swap(conn.write_buffer);
            conn.write_buffer = final_http_packet;
        }
        else
        {
            // 去磁盘上找真正的文件
            // 1. 物理路径拼接：root + path -> "srv/www/index.html"
            // 注意：如果 path 只是 "/"，我们需要自动帮它对齐到默认的 index.html
            std::string request_path = conn.request.getPath();
            if (request_path == "/")
            {
                request_path = "/index.html";
            }

            // 假设你的配置类里能拿到 root 字符串，拼出磁盘物理路径
            std::string file_path = "srv/www" + request_path;
            // 2. 强攻磁盘：用二进制模式打开文件，防止图片或文本损毁
            std::ifstream file(file_path.c_str(), std::ios::binary);
            if (!file.is_open())
            {
                // 如果磁盘上根本没有这个文件，赶紧回 404
                std::cout << "[ServerManager] 404 File Not Found: " << file_path << std::endl;
                std::string error_body = "<h1>404 Not Found</h1>\n<p>Webserv can't find this file.</p>";
                std::stringstream ss;
                ss << "HTTP/1.1 404 Not Found\r\n"
                   << "Content-Type: text/html\r\n"
                   << "Content-Length: " << error_body.size() << "\r\n"
                   << "Connection: keep-alive\r\n"
                   << "\r\n"
                   << error_body;
                std::string().swap(conn.write_buffer);
                conn.write_buffer = ss.str();
            }
            else
            {
                // 3.把磁盘文件内容存入body
                std::stringstream file_content;
                file_content << file.rdbuf();
                file.close(); // 读完立刻关闭文件描述符，防泄漏
                std::string body = file_content.str();
                // 4. 判断 Content-Type （简单做个后缀对齐，防止浏览器乱码）
                std::string content_type = "text/html";
                if (file_path.find(".css") != std::string::npos)
                    content_type = "text/css";
                else if (file_path.find(".js") != std::string::npos)
                    content_type = "application/javascript";
                else if (file_path.find(".png") != std::string::npos)
                    content_type = "image/png";
                else if (file_path.find(".jpg") != std::string::npos || file_path.find(".jpeg") != std::string::npos)
                    content_type = "image/jpeg";
                // 5. 动态拼装回包头部
                std::stringstream ss;
                ss << "HTTP/1.1 200 OK\r\n"
                   << "Server: Webserv/1.0\r\n"
                   << "Content-Type: " << content_type << "\r\n"
                   << "Content-Length: " << body.size() << "\r\n" // 物理对齐动态文件的真实大小
                   << "Connection: keep-alive\r\n"
                   << "\r\n"
                   << body;
                // 6. 存入连接发件箱
                std::string().swap(conn.write_buffer);
                conn.write_buffer = ss.str();
                std::cout << "[ServerManager] Successfully served dynamic static file: " << file_path << " (" << body.size() << " bytes)" << std::endl;
            }
        }
        // 5. 统一调转枪头关注写事件！让大循环的 POLLOUT 下一轮进来把写缓冲区送出网线
        this->_poll_fds[poll_index].events = POLLOUT;
    }
    else if (status == REQUEST_INCOMPLETE)
    {
        // 半包挂起：不裁剪 buffer，consumed 应该为 0。等待下一次 poll 读就绪唤醒。
        std::cout << "[ServerManager] Request incomplete for FD " << clientFd << ". Waiting for more data..." << std::endl;
    }
    else
    {
        // 解析发生硬伤（400, 413, 505 等）
        std::cerr << "[ServerManager] Request error (" << status << ") on FD " << clientFd << ". Pre-writing 400 response." << std::endl;
        // 1. 关键防线：给当前连接打上“死缓”标签
        conn.close_after_write = true;
        // 2. 快速响应一个 400 Bad Request 灌入写缓冲区，物理保留 Connection 活口
        std::string error_response = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        // 直接把 400 报错报文追加到连接自己的写缓冲区（发件箱）中
        conn.write_buffer += error_response;
        // 3. 调转枪头，去 poll 监听可写事件，等 400 真正送出网线后再执行制裁
        this->_poll_fds[poll_index].events = POLLOUT;
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
    Connection &conn = this->_connections[clientFd];

    // 防御：如果发件箱本来就是空的，直接返回
    if (conn.write_buffer.empty())
        return;
    // 1. 推进缓冲区队列
    ssize_t bytes_sent = conn.socket->write(conn.write_buffer);
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
        conn.write_buffer.erase(0, bytes_sent);
    // 只有当这一轮的写缓冲区被彻底排空
    if (conn.write_buffer.empty())
    {
        // C++98 彻底释放内存空间防止虚胖
        std::string().swap(conn.write_buffer);
        // 3. 检查是否有延时自毁标签
        if (conn.close_after_write)
        {
            std::cout << "[ServerManager] Sent response completely. Now safely closing Client FD " << clientFd << std::endl;
            this->closeConnection(clientFd, poll_index);
            return;
        }
        // 4. 重洗 Request 智囊大脑，干干净净迎接下一个请求
        conn.clearRequest();
        // 5. 正常情况下（Keep-Alive 连接），发完回包后重新将 events 改回读（POLLIN），等待下一个请求到达
        this->_poll_fds[poll_index].events = POLLIN;
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

    // 2.只要一笔抹掉这个 clientFd 的盒子，盒子里面的 read_buffer、物理搬运工 ClientIO（包含写缓冲区）全部自动物理析构，干净利落！
    this->_connections.erase(clientFd);

    /*
        3. 保持原来的排序，不在这里立马从 vector 中 erase 它，而是把它的标志位设为死寂状态 (-1)
        告诉内核：下一次 poll 别再看它了。同时让 run() 的倒序大循环完完整整走完这轮点名，不踩空
    */
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

    // 零 errno 物理屏障：非阻塞下捞空或被信号中断时优雅退回 poll
    if (clientFd < 0)
        return;

    // 2. 打印物理迎宾日志
    std::cout << "[ServerManager] Accepted new connection from "
              << inet_ntoa(client_addr.sin_addr) << ":" << ntohs(client_addr.sin_port)
              << " -> Allocated Client FD: " << clientFd << std::endl;

    // 3.【物理并轨】：直接打包塞进账本（Connection 内部会安全地把 clientFd 剥夺阻塞特权）
    this->_connections[clientFd] = Connection(clientFd, this->_listen_socket_map[listenFd]);

    // 4. 挂载进 poll 阵列，静待第一轮 POLLIN 读数据
    struct pollfd pfd;
    pfd.fd = clientFd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    this->_poll_fds.push_back(pfd);
}