#ifndef CONNECTION_HPP
#define CONNECTION_HPP

#include "ClientSocket.hpp"
#include "Request.hpp"
#include "Response.hpp"
class Connection
{
public:
    // -------------------------------------------------------------
    // 1. 基础物理数据区 (Pod/Data Layer): 允许 Handler 直接追加与擦除 Buffer
    // -------------------------------------------------------------
    ClientSocket *socket;
    ServerConfig config;
    std::string read_buffer;
    std::string write_buffer;

    Request request;
    Response response;
    bool close_after_write;



    Connection();
    Connection(int clientFd, const ServerConfig &srv_cfg);
    ~Connection();

   

    void clear();
    void clearRequest();

private:
 
    Connection(const Connection &other);
    Connection &operator=(const Connection &other);
};

#endif