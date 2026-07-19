#ifndef CONNECTION_HPP
#define CONNECTION_HPP

#include "ClientSocket.hpp"
#include "Request.hpp"
#include "Response.hpp"

class Connection
{
private:
    ClientSocket *socket; // RAII 持有客户端底层物理套接字！
    ServerConfig config;
    std::string read_buffer;
    std::string write_buffer;
    Request request;
    Response response; // 🟢 2. 挂载即将准备下发或者正在组装的响应体！
    bool close_after_write;

    bool is_cgi;
    int cgi_read_fd;  // 子进程吐数据的管道读端
    int cgi_write_fd; // 主进程喂数据的管道写端（如果是POST）
    pid_t cgi_pid;
    size_t cgi_body_bytes_sent;

    Connection(const Connection &other);
    Connection &operator=(const Connection &other);

    friend class ServerManager;
    friend class RequestParser;

public:
    Connection();
    Connection(int clientFd, const ServerConfig &srv_cfg);

    ~Connection();

    void clear();
    void clearRequest();
};

#endif