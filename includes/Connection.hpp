#ifndef CONNECTION_HPP
#define CONNECTION_HPP

#include "ClientSocket.hpp"
#include "Request.hpp"

class Connection
{
private:
    ClientSocket *socket; // RAII 持有客户端底层物理套接字！
    ServerConfig config;
    std::string read_buffer;
    std::string write_buffer;
    Request request;
    bool close_after_write;

    friend class ServerManager;
    friend class RequestParser;

public:
    Connection();
    Connection(int clientFd, const ServerConfig &srv_cfg);
    Connection(const Connection &other);
    Connection &operator=(const Connection &other);
    ~Connection();

    void clear();
    void clearRequest();
};

#endif