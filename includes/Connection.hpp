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

    // -------------------------------------------------------------
    // 2. CGI 状态区: 将易错的状态修改【收拢为原子方法】！
    // -------------------------------------------------------------
    // 💡 保持变量只读视角 (Read-only access)，通过内部统一控制
    bool isCgi() const;
    int getCgiReadFd() const;
    int getCgiWriteFd() const;
    pid_t getCgiPid() const;
    std::time_t getCgiStartedAt() const;

    std::string cgi_output_buffer; // Buffer 依然允许外部直接 append
    size_t cgi_body_bytes_sent;

    // -------------------------------------------------------------
    // 3. 状态原子操作函数 (防修改的关键防御！)
    // -------------------------------------------------------------
    // 💡 启动 CGI 时，强行绑定三要素，防漏洞！
    void startCgi(int readFd, int writeFd, pid_t pid);

    // 💡 重置 CGI 时，原子化清理，防残留！
    void resetCgi();

    void closeWriteFd();
    void closeReadFd();

    void clearCgiPid();

    Connection();
    Connection(int clientFd, const ServerConfig &srv_cfg);
    ~Connection();

    void clear();
    void clearRequest();

private:
    // 将极其敏感的状态变量私有化！
    bool _is_cgi;
    int _cgi_read_fd;
    int _cgi_write_fd;
    pid_t _cgi_pid;
    std::time_t _cgi_started_at;

    Connection(const Connection &other);
    Connection &operator=(const Connection &other);
};

#endif