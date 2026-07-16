#include "Webserv.hpp"

/**
 * @brief 默认构造函数：初始化一个空客户端套接字安全哨兵
 */
ClientSocket::ClientSocket() : _fd(-1) {}

/**
 * @brief 有参构造函数：接管已被 accept 成功捕获的客户端连接描述符
 * 
 * @param fd 客户端连接套接字的文件描述符 (File Descriptor)
 */
ClientSocket::ClientSocket(int fd) : _fd(fd) {}

/**
 * @brief 析构函数：践行 RAII 哲学，确保对象销毁时连接 100% 被物理关闭
 */
ClientSocket::~ClientSocket()
{
    this->closeFd();
}

/**
 * @brief 物理关闭套接字并重置描述符，防止重复关闭与 FD 泄露
 * 
 * @details 
 * - 检查物理文件描述符 `_fd` 是否处于活跃状态 (>= 0)。
 * - 打印安全释放日志，调用 close() 系统函数物理释放资源。
 * - 黄金闭环：强制将 `_fd` 重置为哨兵值 -1，彻底断绝二次关闭导致的内核并发漏洞 (Double Close Bug)。
 */
void ClientSocket::closeFd()
{
    if (this->_fd >= 0)
    {
        std::cout << "[ClientSocket] RAII Closing FD: " << this->_fd << std::endl;
        close(this->_fd);
        this->_fd = -1;
    }
}

/**
 * @brief 将客户端连接套接字设置为 O_NONBLOCK 非阻塞模式
 * 
 * @note 
 * 强制将客户端 FD 改为非阻塞态，是保证大管家在 recv/send 数据时
 * 哪怕遇到客户端网速极慢或发送中断，也绝对不会发生线程挂起、卡死的唯一保障。
 */
void ClientSocket::setNonBlocking()
{
    if (this->_fd < 0)
        return;
    int flags = fcntl(this->_fd, F_GETFL, 0);
    if (flags < 0)
    {
        std::cerr << "Error: fcntl F_GETFL failed for client fd " << this->_fd << std::endl;
        return;
    }
    if (fcntl(this->_fd, F_SETFL, flags | O_NONBLOCK) < 0)
    {
        std::cerr << "Error: fcntl F_SETFL O_NONBLOCK failed for client fd " << this->_fd << std::endl;
    }
}

/**
 * @brief 从客户端套接字中物理读取（捞取）原始报文数据
 * 
 * @param buf 存放接收数据的缓冲区指针。
 * @param size 期望单次读取的最大字节数。
 * @return ssize_t 
 *         - 正数：实际读取到的物理字节数；
 *         - 0：EOF，代表客户端发起优雅拔线（正常关闭连接）；
 *         - -1：非阻塞探测，当前内核缓冲区暂无数据，大管家应跳出读取循环；
 *         - -2：连接彻底发生物理死亡（不可逆的断开连接异常）。
 * 
 * @details 
 * 采用【零 errno 物理测谎】的高级侦察防御逻辑：
 * 1. 正常调用 recv 抓取数据，若返回 -1，不依赖不稳定的 errno。
 * 2. 物理调用带 MSG_PEEK 标志的 recv 偷偷窥探内核队列。
 * 3. 若窥探同样返回 -1，证明仅仅是内核缓冲区没有数据了（EAGAIN），返回 -1 告知大管家安全收兵。
 * 4. 若窥探结果异常，断定通道已彻底死亡，返回 -2 告知大管家物理销毁此客户端。
 */
ssize_t ClientSocket::read(char *buf, size_t size) const
{
    // 1. 正常尝试从网线中捞取数据
    ssize_t bytes = recv(this->_fd, buf, size, 0);

    if (bytes == 0)
        return 0; // EOF（客户端优雅拔线）

    if (bytes < 0)
    {
        // 【零 errno 物理测谎】：利用 MSG_PEEK 偷偷瞄一眼内核缓冲区
        char dummy;
        ssize_t peek_bytes = recv(this->_fd, &dummy, 1, MSG_PEEK);
        // 如果偷看也返回 -1：内核里真的没数据了
        if (peek_bytes < 0)
        {
            return -1; // 优雅返回 -1，通知大管家跳出吞噬循环
        }
        // 否则，说明连接已经发生不可逆的物理死亡
        return -2; // 致命断连
    }
    return bytes;
}

/**
 * @brief 向客户端套接字物理发送（灌入）HTTP 响应数据
 * 
 * @param data 待发送的原始报文字符串。
 * @return ssize_t 实际发送成功的字节数，失败返回 -1。
 * 
 * @note 
 * 物理注入 MSG_NOSIGNAL 屏蔽信号标志。若客户端在中途已经异常切断网线（Broken Pipe），
 * 内核会默认向进程抛出致命的 SIGPIPE 信号导致服务器当场崩溃。
 * 开启此标志能强行降服信号，改由 send 函数物理返回 -1 报错，从而被大管家优雅捕获。
 */
ssize_t ClientSocket::write(const std::string &data) const
{
    if (data.empty())
        return 0;
    ssize_t bytes = send(this->_fd, data.data(), data.size(), MSG_NOSIGNAL);
    if (bytes < 0)
        return -1;
    return bytes;
}

/**
 * @brief 获取当前客户端连接的物理文件描述符
 * 
 * @return int 文件描述符 fd。
 */
int ClientSocket::getFd() const
{
    return this->_fd;
}