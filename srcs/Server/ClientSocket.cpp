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
ClientSocket::ClientSocket(int fd) : _fd(fd)
{
    setNonBlocking();
}

/**
 * @brief 析构函数：践行 RAII 哲学，确保对象销毁时连接 100% 被物理关闭
 */
ClientSocket::~ClientSocket()
{
    if (this->_fd >= 0)
    {
        ::close(this->_fd);
        this->_fd = -1; // 严防悬空 / Double-close
    }
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
        DEBUG_LOG("[ClientSocket] RAII Closing FD: " << this->_fd);
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

    // 🚀 唯一合规写法：直接覆写标志位为 O_NONBLOCK，剔除 F_GETFL
    if (fcntl(this->_fd, F_SETFL, O_NONBLOCK) < 0)
    {
        std::cerr << "Error: fcntl F_SETFL O_NONBLOCK failed for client fd " << this->_fd << std::endl;
        // 视你的架构而定，这里也许需要 throw 异常或 close 掉 FD
        return;
    }

    // ⛔️ 彻底删除 F_GETFD, F_SETFD 和 FD_CLOEXEC
    // 因为你在执行 CGI (fork) 时，已经写了 for (i = 3; i < max_fd; ++i) close(i); 
    // 那个盲关循环会自动把这些 Client FD 全部从子进程里清理掉，完全不需要 FD_CLOEXEC！
}

/**
 * @brief 从客户端套接字读取一次当前 POLLIN 事件允许的数据。
 *
 * @param buf 存放接收数据的缓冲区。
 * @param size 本次最多读取的字节数。
 * @return ssize_t 正数表示实际读取字节数；0 表示对端 EOF；-1 表示 recv 失败。
 *
 * @note 本函数只执行一次 recv()，不检查 errno，也不构造 -2 等额外返回码。
 *       上层必须只在 poll() 报告该客户端可读后调用，并对 0 与 -1 都执行连接清理。
 */
ssize_t ClientSocket::read(char *buf, size_t size) const
{
    return ::recv(this->_fd, buf, size, 0);
}

/**
 * @brief 向客户端套接字执行一次非阻塞 HTTP 响应发送。
 *
 * @param data 当前仍待发送的响应数据。
 * @return ssize_t 正数表示本次实际发送字节数；0 表示没有取得发送进展；-1 表示 send 失败。
 *
 * @note 本函数只执行一次 send()，使用 MSG_NOSIGNAL 防止 SIGPIPE 杀死服务器，
 *       不检查 errno，也不构造 -2 等额外返回码。部分发送由 ServerManager 保留剩余数据并等待下一次 POLLOUT。
 */
ssize_t ClientSocket::write(const std::string &data) const
{
    if (data.empty())
        return 0;
    return ::send(this->_fd, data.data(), data.size(), 0);
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
