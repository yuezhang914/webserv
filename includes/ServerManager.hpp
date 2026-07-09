#ifndef SERVER_MANAGER_HPP
#define SERVER_MANAGER_HPP

#include "ServerConfig.hpp"
#include "ClientIO.hpp"

class ServerManager
{
private:
    // 1. 核心网络资产
    std::vector<ServerConfig> _server_configs; // 配置账本备份
    std::vector<struct pollfd> _poll_fds;      // poll 监听大阵列

    // 2. 运行时高效映射表
    std::map<int, ServerConfig> _listen_socket_map; // listenFd -> ServerConfig
    std::map<int, ServerConfig> _client_to_srv_map; // clientFd -> ServerConfig
    std::map<int, std::string> _client_buffers;     // clientFd -> 请求缓冲区
    std::map<int, std::string> _response_buffers; // 专属客户端写缓冲区映射（存未发完的尾巴）
    std::map<int, ClientIO> _ios;// key: 客人的物理套接字, value: 为这个客人量身定做的物理搬运工 (ClientIO 实例)
    

    // 3. 内部私有工具函数
    void setupSockets();                                     // 砸开所有配置的物理端口
    void setNonBlocking(int fd);                             // 将套接字设为非阻塞
    bool isListenFd(int fd);                                 // 判别是监听端口还是普通客户连接
    void acceptNewConnection(int listenFd);                  // 诞生新客户并挂载上网
    void handleClientRead(int clientFd, size_t poll_index);  // 读取客户端请求
    void handleClientWrite(int clientFd, size_t poll_index); // 发送响应给客户端
    void closeConnection(int clientFd, size_t poll_index);  // 清理并断开连接

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