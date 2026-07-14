#ifndef CONNECTION_HPP
#define CONNECTION_HPP

#include "ClientIO.hpp"     // 物理搬运工被包裹在内
#include "ServerConfig.hpp" // serverConfig
#include "Request.hpp"
// #include "HTTPContext.hpp" // 未来的 HTTP 业务也可以挂在这里！

class Connection
{
public:
    int fd;                  // 客人的物理套接字
    ServerConfig config;     // 绑定的虚拟主机配置
    std::string read_buffer; // 读缓冲区（拼接小抽屉）
    ClientIO io;             // 专属的物理搬运工实例
    Request request; // 🚀 每一个连接专属的 Request 实体，用来暂存它的解析结果

    // 满足 STL map 要求的默认构造
    Connection();

    // 诞生成立时的有参构造
    Connection(int clientFd, const ServerConfig &srv_cfg);
     
    ~Connection();

    const ServerConfig& getConfig()const;

    // 一键重置长连接状态（Keep-Alive 复位）
    void clear();
};

#endif