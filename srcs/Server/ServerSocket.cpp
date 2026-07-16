#include "Webserv.hpp"

/**
 * @brief 构造函数：初始化服务器套接字的基本属性
 * 
 * @param host 监听的物理主机地址（如 "127.0.0.1"、"0.0.0.0" 或 "localhost"）。
 * @param port 监听的物理端口号。
 * 
 * @note 
 * 此时套接字仅完成属性装配，文件描述符初始化为安全哨兵值 -1，
 * 实际的内核套接字创建与绑定将延迟至调用 setup() 时物理执行。
 */
ServerSocket::ServerSocket(const std::string &host, int port)
    : _fd(-1), _host(host), _port(port) {}

/**
 * @brief 析构函数：践行 RAII 哲学，物理释放套接字系统资源
 * 
 * @details 
 * 当 ServerSocket 对象的生命周期结束（销毁或进程退出）时，自动触发此析构函数：
 * - 检查物理文件描述符 `_fd` 是否有效 (>= 0)。
 * - 若有效，则打印安全释放日志，并优雅调用系统函数 close() 释放该监听套接字。
 * 
 * @note 
 * 这确保了无论服务器是在正常运转下退出，还是由于异常熔断而自杀，
 * 系统套接字描述符都会被 100% 物理闭环回收，绝不给操作系统留下一丝 FD 泄漏隐患。
 */
ServerSocket::~ServerSocket()
{
    if (this->_fd >= 0)
    {
        std::cout << "[ServerSocket] Closing listen FD: " << this->_fd << " for " << this->_host << ":" << this->_port << std::endl;
        close(this->_fd);
    }
}

/**
 * @brief 将服务器监听套接字强制设置为 O_NONBLOCK 非阻塞模式 (C++98 / POSIX)
 * 
 * @details 
 * 利用系统调用 fcntl 执行“两步走”原子操作，物理改写套接字的文件状态标志：
 * 1. 调用 fcntl(fd, F_GETFL) 获取套接字当前的物理标志属性 (flags)。
 * 2. 对原有 flags 进行按位或 (|) 操作，注入 O_NONBLOCK 标志。
 * 3. 调用 fcntl(fd, F_SETFL, flags | O_NONBLOCK) 将更新后的标志写回内核。
 * 
 * @note 
 * - 这是多路复用 (poll/epoll) 架构的核心前置条件。非阻塞套接字在无数据可读/无空间可写时，
 *   系统调用 (如 accept, recv) 会物理返回 -1 并设置 errno 为 EAGAIN 或 EWOULDBLOCK，
 *   而不会导致整个服务器进程卡死挂起。
 * - 任何一步 fcntl 失败都会触发安全熔断，物理调用 exit(1) 终止进程。
 */
void ServerSocket::setNonBlocking()
{
    int flags = fcntl(this->_fd, F_GETFL, 0);
    if (flags < 0)
    {
        std::cerr << "Error: fcntl F_GETFL failed for fd " << this->_fd << std::endl;
        exit(1);
    }
    if (fcntl(this->_fd, F_SETFL, flags | O_NONBLOCK) < 0)
    {
        std::cerr << "Error: fcntl F_SETFL O_NONBLOCK failed for fd " << this->_fd << std::endl;
        exit(1);
    }
}

/**
 * @brief 初始化服务器监听套接字并绑定网络地址 (C++98 / POSIX)
 * 
 * @details 
 * 物理执行以下五个核心步骤以建立稳健的监听端：
 * 1. 创建 IPv4 字节流套接字 (TCP)。
 * 2. 启用 SO_REUSEADDR 地址复用，防止服务器重启时端口因 TIME_WAIT 状态而锁死。
 * 3. 强制将套接字设为 O_NONBLOCK 非阻塞模式，配合多路复用 (poll/epoll) 架构。
 * 4. 物理对齐配置中的 Host IP（自适应 localhost/127.0.0.1、通配 0.0.0.0 或指定静态 IP），并绑定物理端口。
 * 5. 开启 listen 监听，设定系统最大连接排队队列 (SOMAXCONN)。
 * 
 * @note 
 * 任何步骤失败都会直接向标准错误输出错误日志，物理调用 exit(1) 熔断自杀，
 * 绝不允许大管家带着损坏的套接字带病运行。
 */
void ServerSocket::setup()
{
    // 1. 创建 socket
    this->_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (this->_fd < 0)
    {
        std::cerr << "Error: Cannot create socket for port " << this->_port << std::endl;
        exit(1);
    }
    // 2. 开启 SO_REUSEADDR 地址复用
    int reuse = 1;
    if (setsockopt(this->_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0)
    {
        std::cerr << "Error: setsockopt(SO_REUSEADDR) failed" << std::endl;
        close(this->_fd);
        exit(1);
    }
    // 3. 强制设置为 O_NONBLOCK 非阻塞
    this->setNonBlocking();
    // 4. 绑定物理地址（精确对齐 Host，拒绝 INADDR_ANY 通配冲突）
    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(this->_port);
    if (this->_host == "localhost" || this->_host == "127.0.0.1")
    {
        addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    }
    else if (this->_host == "0.0.0.0")
    {
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
    }
    else
    {
        addr.sin_addr.s_addr = inet_addr(this->_host.c_str());
    }
    if (bind(this->_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        std::cerr << "Error: Cannot bind to " << this->_host << ":" << this->_port << std::endl;
        close(this->_fd);
        exit(1);
    }
    // 5. 开始监听
    if (listen(this->_fd, SOMAXCONN_BACKLOG) < 0)
    {
        std::cerr << "Error: Listen failed on port " << this->_port << std::endl;
        close(this->_fd);
        exit(1);
    }
}

/**
 * @brief 获取服务器监听套接字的物理文件描述符 (File Descriptor)
 * 
 * @return int 对应的套接字 fd。若未初始化或已关闭，则返回负值。
 * @note 供外部多路复用器 (如 poll/epoll) 将该监听端注册到事件监听队列中。
 */
int ServerSocket::getFd() const
{
    return this->_fd;
}

/**
 * @brief 获取当前服务器套接字绑定的物理主机地址 (Host)
 * 
 * @return const std::string& 绑定的 IP 地址或主机名（如 "127.0.0.1"、"0.0.0.0" 或 "localhost"）的常引用。
 * @note 采用 const 引用返回，物理避免了字符串拷贝的内存开销，同时锁死外部修改权限。
 */
const std::string &ServerSocket::getHost() const
{
    return this->_host;
}

/**
 * @brief 获取当前服务器套接字绑定的物理监听端口 (Port)
 * 
 * @return int 绑定的端口号（例如 3435）。
 */
int ServerSocket::getPort() const 
{ 
    return this->_port; 
}