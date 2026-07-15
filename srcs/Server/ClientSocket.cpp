#include "Webserv.hpp"

ClientSocket::ClientSocket() : _fd(-1) {}

ClientSocket::ClientSocket(int fd) : _fd(fd) {}

ClientSocket::~ClientSocket()
{
    this->closeFd();
}

void ClientSocket::closeFd()
{
    if (this->_fd >= 0)
    {
        std::cout << "[ClientSocket] RAII Closing FD: " << this->_fd << std::endl;
        close(this->_fd);
        this->_fd = -1;
    }
}

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

ssize_t ClientSocket::write(const std::string &data) const
{
    if (data.empty())
        return 0;
    ssize_t bytes = send(this->_fd, data.data(), data.size(), 0);
    if (bytes < 0)
        return -1;
    return bytes;
}

int ClientSocket::getFd() const
{
    return this->_fd;
}