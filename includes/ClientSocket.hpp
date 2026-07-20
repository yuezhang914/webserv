#ifndef CLIENTSOCKET_HPP
#define CLIENTSOCKET_HPP

#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <iostream>

class ClientSocket
{
private:
    int _fd;

    // 自洗非阻塞（剥夺阻塞特权）
    void setNonBlocking();
    // 禁用拷贝，防止 FD 遭遇多重析构 close
    ClientSocket(const ClientSocket &);
    ClientSocket &operator=(const ClientSocket &);

public:
    // 默认构造，用来放入 std::map 等容器
    ClientSocket();
    explicit ClientSocket(int fd);
    ~ClientSocket(); // RAII：生命周期结束自动物理 close

        // 核心动作 2：底层无 errno 依赖物理读取
    // 如果读取成功，返回字节数；
    // 如果 EOF 触发返回 0；
    // 如果遇到阻碍（EAGAIN/EWOULDBLOCK）返回 -1；
    // 如果发生物理崩溃（ECONNRESET 等）返回 -2。
    ssize_t read(char *buf, size_t size) const;

    // 核心动作 3：底层无 errno 依赖物理喷吐
    ssize_t write(const std::string &data) const;

    // 核心动作 4：主动断开
    void closeFd();

    int getFd() const;
};

#endif