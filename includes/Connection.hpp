#ifndef CONNECTION_HPP
#define CONNECTION_HPP

#include "ClientIO.hpp"     // 物理搬运工被包裹在内
#include "ServerConfig.hpp" // serverConfig
#include "Request.hpp"

class Connection
{
private:
    int fd;
    ServerConfig config;
    std::string read_buffer;
    ClientIO io;
    Request request;

    // 🎫 唯独对大管家和解析器开放绝对特权
    friend class ServerManager;
    friend class RequestParser;

public:
    Connection();
    Connection(int clientFd, const ServerConfig &srv_cfg);
    ~Connection();
    void clear();
};

#endif