#ifndef SERVER_MANAGER_HPP
#define SERVER_MANAGER_HPP

// 🟢 1. 显式并网 C++98 和系统内核所需的全部底层资产
#include <vector>
#include <map>
#include <poll.h> // 🟢 必须包含它，编译器才能认出 struct pollfd
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "ServerConfig.hpp"
#include "Connection.hpp"
#include "RequestParser.hpp"
#include "ServerSocket.hpp"
#include "CgiHandler.hpp"

class ServerManager
{
private:
    // 1. 核心网络资产
    std::vector<ServerConfig> _server_configs;   // 配置账本备份
    std::vector<struct pollfd> _poll_fds;        // poll 监听大阵列
    std::vector<ServerSocket *> _listen_sockets; // 统一管理所有创建的套接字指针

    // 2. 运行时高效映射表
    std::map<int, ServerConfig> _listen_socket_map; // listenFd -> ServerConfig
    std::map<int, Connection*> _connections;         // clientFd -> Connection

    // 🚀 【CGI 并网资产】：逆向雷达与延迟追加队列
    std::map<int, int> _cgi_read_fd_to_client_map;  // 读端专属：CGI 读 Fd -> Client Fd
    std::map<int, int> _cgi_write_fd_to_client_map; // 写端专属：CGI 写 Fd -> Client Fd
    std::vector<struct pollfd> _fds_to_add;   // 暂存箱，封锁 vector 扩容带来的野指针段错误

    // 3. 内部私有工具函数
    void setupSockets();                                     // 砸开所有配置的物理端口
    bool isListenFd(int fd);                                 // 判别是监听端口还是普通客户连接
    void acceptNewConnection(int listenFd);                  // 诞生新客户并挂载上网
    void handleClientRead(int clientFd, size_t poll_index);  // 读取客户端请求
    void handleClientWrite(int clientFd, size_t poll_index); // 发送响应给客户端
    void closeConnection(int clientFd, size_t poll_index);   // 清理并断开连接
    void prePollCleanup();
    int executePoll(int &retries);
    void dispatchEvents();

    void registerFdToPoll(int fd, short events);

    void enableClientWriteEvent(int clientFd);

    // 🚀 【CGI 并网工具】：异步分流与收割车间
    bool isCgiPipeFd(int fd);                               // 侦测触发的是不是 CGI 管道
    void handleCgiPipeRead(int cgiReadFd, size_t poll_idx); // 异步收割 Python 输出
  
    void handleCgiPipeWrite(int cgiWriteFd, size_t poll_idx);
    void _cleanupCgiResources(Connection *conn);

public:
    // 构造与析构
    ServerManager(const std::vector<ServerConfig> &configs);
    ~ServerManager();

    // 4. 对外唯一暴露的宏观接口
    void init(); // 触发冷启动：砸端口、建映射
    void run();  // 进入永不停止的 poll() 核心大循环

private:
    // 封杀 C++98 的默认拷贝与赋值
    ServerManager(const ServerManager &src);
    ServerManager &operator=(const ServerManager &rhs);
}; // 🎯 分号完好无损

#endif