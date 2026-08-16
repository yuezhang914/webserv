#include "Webserv.hpp"

// Creates a new ClientSocket object.
ClientSocket::ClientSocket() : _fd(-1)
{
}

// Creates a new ClientSocket object.
ClientSocket::ClientSocket(int fd) : _fd(fd)
{
    setNonBlocking();
}

// Cleans up this object and its owned resources.
ClientSocket::~ClientSocket()
{
    if (this->_fd >= 0)
    {
        ::close(this->_fd);
        this->_fd = -1;
    }
}

// Closes fd.
void ClientSocket::closeFd()
{
    if (this->_fd >= 0)
    {
        DEBUG_LOG("[ClientSocket] RAII Closing FD: " << this->_fd);
        close(this->_fd);
        this->_fd = -1;
    }
}

// Sets non blocking.
void ClientSocket::setNonBlocking()
{
    if (this->_fd < 0)
        return;
    if (fcntl(this->_fd, F_SETFL, O_NONBLOCK) < 0)
    {
        std::cerr << "Error: fcntl F_SETFL O_NONBLOCK failed for client fd " << this->_fd << std::endl;
        return;
    }
}

// Reads available data from the client socket.
ssize_t ClientSocket::read(char *buf, size_t size) const
{
    return::recv(this->_fd, buf, size, 0);
}

// Sends response data to the client socket.
ssize_t ClientSocket::write(const std::string &data) const
{
    if (data.empty())
        return 0;
    return::send(this->_fd, data.data(), data.size(), 0);
}

// Returns fd.
int ClientSocket::getFd() const
{
    return this->_fd;
}
