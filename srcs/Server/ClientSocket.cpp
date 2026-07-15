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
    ssize_t bytes = recv(this->_fd, buf, size, 0);
    if (bytes == 0)
        return 0; // EOF
    if (bytes < 0)
    {
        // 过滤非阻塞抖动
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return -1;
        return -2; // 致命断连
    }
    return bytes;
}

ssize_t ClientSocket::write(const std::string &data) const
{
    if (data.empty())
        return 0;

    // 🚀 【抓脏打印仪】：看看这多出来的 375 字节到底是在哪一步被塞进 string 的！
    std::cout << "[DEBUG_WRITE] Client FD: " << this->_fd
              << " | data.size() = " << data.size() << std::endl;

    if (data.size() > 50)
    {
        std::cout << "[DEBUG_WRITE] Header prefix: " << data.substr(0, 50) << std::endl;
        std::cout << "[DEBUG_WRITE] Tail suffix: " << data.substr(data.size() - 50) << std::endl;
    }
    else
    {
        std::cout << "[DEBUG_WRITE] Content: " << data << std::endl;
    }

    ssize_t bytes = send(this->_fd, data.data(), data.size(), 0);

    std::cout << "[DEBUG_WRITE] Actually sent by kernel: " << bytes << " bytes" << std::endl;

    if (bytes < 0)
        return -1;
    return bytes;
}

int ClientSocket::getFd() const
{
    return this->_fd;
}