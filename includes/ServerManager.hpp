#ifndef SERVER_MANAGER_HPP
#define SERVER_MANAGER_HPP

#include "ServerConfig.hpp"
#include "Connection.hpp"
#include "RequestParser.hpp"
#include "ServerSocket.hpp"

class ServerManager
{
private:
    // 1. 核心网络资产
    std::vector<ServerConfig> _server_configs; // 配置账本备份
    std::vector<struct pollfd> _poll_fds;      // poll 监听大阵列
    std::vector<ServerSocket *> _listen_sockets; // 统一管理所有创建的套接字指针

    // 2. 运行时高效映射表
    std::map<int, ServerConfig> _listen_socket_map; // listenFd -> ServerConfig
                 
    std::map<int, Connection> _connections;         // 一个 key 对应一个完整的生命盒子

    // 3. 内部私有工具函数
    void setupSockets();                                     // 砸开所有配置的物理端口
    bool isListenFd(int fd);                                 // 判别是监听端口还是普通客户连接
    void acceptNewConnection(int listenFd);                  // 诞生新客户并挂载上网
    void handleClientRead(int clientFd, size_t poll_index);  // 读取客户端请求
    void handleClientWrite(int clientFd, size_t poll_index); // 发送响应给客户端
    void closeConnection(int clientFd, size_t poll_index);   // 清理并断开连接

public:
    // 🛠️ 构造与析构
    ServerManager(const std::vector<ServerConfig> &configs);
    ~ServerManager();

    // 🚀 4. 对外唯一暴露的宏观大动脉接口
    void init(); // 触发冷启动：砸端口、建映射
    void run();  // 挺进永不停止的 poll() 核心大循环

private:
    // 🔒 封杀 C++98 的默认拷贝与赋值
    ServerManager(const ServerManager &src);
    ServerManager &operator=(const ServerManager &rhs);
};

#endif