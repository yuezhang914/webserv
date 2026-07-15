#ifndef SERVERSOCKET_HPP
#define SERVERSOCKET_HPP

class ServerSocket
{
private:
    int _fd;
    std::string _host;
    int _port;

    // 阻止 C++98 的默认拷贝，防止套接字被误关闭
    ServerSocket(const ServerSocket &);
    ServerSocket &operator=(const ServerSocket &);

    void setNonBlocking();

public:
    ServerSocket(const std::string &host, int port);
    ~ServerSocket(); // RAII 释放：析构时自动 close fd

    // 核心动作：一键拉起监听物理大网
    void setup();

    int getFd() const;
    const std::string &getHost() const;
    int getPort() const;
};

#endif