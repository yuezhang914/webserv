#include "Webserv.hpp"

// Creates a new ServerSocket object.
ServerSocket::ServerSocket(const std::string &host, int port) : _fd(-1), _host(host), _port(port)
{
}

// Cleans up this object and its owned resources.
ServerSocket::~ServerSocket()
{
    if (this->_fd >= 0)
    {
        std::cout << "[ServerSocket] Closing listen FD: " << this->_fd << " for " << this->_host << ":" << this->_port << std::endl;
        close(this->_fd);
    }
}

// Sets non blocking.
void ServerSocket::setNonBlocking()
{
    if (fcntl(this->_fd, F_SETFL, O_NONBLOCK) < 0)
    {
        std::cerr << "Error: fcntl F_SETFL O_NONBLOCK failed for fd " << this->_fd << std::endl;
        exit(1);
    }
}

// Creates, binds, and starts the listening socket.
void ServerSocket::setup()
{
    this->_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (this->_fd < 0)
    {
        std::cerr << "Error: Cannot create socket for port " << this->_port << std::endl;
        exit(1);
    }
    int reuse = 1;
    if (setsockopt(this->_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0)
    {
        std::cerr << "Error: setsockopt(SO_REUSEADDR) failed" << std::endl;
        close(this->_fd);
        exit(1);
    }
    this->setNonBlocking();
    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(this->_port);
    if (this->_host == "0.0.0.0")
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
    else
    {
        struct addrinfo hints;
        struct addrinfo *res;
        std::memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        int status = getaddrinfo(this->_host.c_str(), NULL, &hints, &res);
        if (status != 0)
        {
            std::cerr << "Error: getaddrinfo failed for host '" << this->_host << "' (" << gai_strerror(status) << ")" << std::endl;
            close(this->_fd);
            exit(1);
        }
        struct sockaddr_in *ipv4 = reinterpret_cast<struct sockaddr_in *>(res->ai_addr);
        addr.sin_addr.s_addr = ipv4->sin_addr.s_addr;
        freeaddrinfo(res);
    }
    if (bind(this->_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        std::cerr << "Error: Cannot bind to " << this->_host << ":" << this->_port << std::endl;
        close(this->_fd);
        exit(1);
    }
    if (listen(this->_fd, SOMAXCONN_BACKLOG) < 0)
    {
        std::cerr << "Error: Listen failed on port " << this->_port << std::endl;
        close(this->_fd);
        exit(1);
    }
}

// Returns fd.
int ServerSocket::getFd() const
{
    return this->_fd;
}

// Returns host.
const std::string &ServerSocket::getHost() const
{
    return this->_host;
}

// Returns port.
int ServerSocket::getPort() const
{
    return this->_port;
}

// Closes fd.
void ServerSocket::closeFd()
{
    if (this->_fd >= 0)
    {
        std::cout << "[ServerSocket] Closing listen FD: " << this->_fd << " for " << this->_host << ":" << this->_port << std::endl;
        ::close(this->_fd);
        this->_fd = -1;
    }
}
