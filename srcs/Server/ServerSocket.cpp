#include "Webserv.hpp"

ServerSocket::ServerSocket(const std::string &host, int port)
    : _fd(-1), _host(host), _port(port) {}

ServerSocket::~ServerSocket()
{
    if (this->_fd >= 0)
    {
        std::cout << "[ServerSocket] Closing listen FD: " << this->_fd << " for " << this->_host << ":" << this->_port << std::endl;
        close(this->_fd);
    }
}

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
    else if (this->_host == "0.0.0.0" || this->_host.empty())
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

int ServerSocket::getFd() const { return this->_fd; }
const std::string &ServerSocket::getHost() const { return this->_host; }
int ServerSocket::getPort() const { return this->_port; }