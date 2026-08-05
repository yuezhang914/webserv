#ifndef SERVER_MANAGER_HPP
#define SERVER_MANAGER_HPP

#include "ServerConfig.hpp"
#include "Connection.hpp"
#include "RequestParser.hpp"
#include "ServerSocket.hpp"
#include "CgiManager.hpp"
#include <deque>

class ServerManager
{
private:
    // 1. 核心网络资产
    std::vector<ServerConfig> _server_configs;   // 配置账本备份
    std::vector<struct pollfd> _poll_fds;        // poll 监听大阵列
    std::vector<ServerSocket *> _listen_sockets; // 统一管理套接字指针

    // 2. 运行时网络映射表
    std::map<int, ServerConfig> _listen_socket_map; // listenFd -> ServerConfig
    std::map<int, Connection *> _connections;       // clientFd -> Connection

    // 3. CGI 模块 integration
    CgiManager _cgiManager;                         // 长期持有的 CGI 引擎管家
    std::map<int, int> _cgi_read_fd_to_client_map;  // readFd  -> clientFd
    std::map<int, int> _cgi_write_fd_to_client_map; // writeFd -> clientFd
    std::vector<struct pollfd> _fds_to_add;         // 延迟追加队列（防 vector 扩容野指针）

    // 4. 大型 CGI 内存准入：限制完整 100MB request/response 同时驻留的数量。
    size_t _active_buffered_large_cgi;
    std::deque<int> _waiting_buffered_large_cgi_clients;

    // 大 CGI 准入与等待队列维护。
    bool ensureLargeCgiSlot(Connection *conn, int clientFd);
    void releaseLargeCgiSlot(int clientFd);
    void resumeWaitingLargeCgiClients();

    // 5. 物理管道 FD 身份识别
    bool isCgiReadFd(int fd) const;
    bool isCgiWriteFd(int fd) const;

    // 5. 内部 Reactor 调度函数
    void setupSockets();                    // 砸开所有配置端口
    bool isListenFd(int fd);                // 判别监听套接字 vs 客户连接
    void acceptNewConnection(int listenFd); // 建立新 Client 连接
    bool readSocketDataToBuffer(Connection *conn, int clientFd, size_t pollIndex);
    void dispatchCgiTask(Connection *conn, int clientFd, const Response &res);
    void processParsedRequest(Connection *conn, int clientFd);

    void handleClientRead(int clientFd, size_t pollIndex);  // 读取客户端 HTTP 请求
    void handleClientWrite(int clientFd, size_t pollIndex); // 发送 HTTP Response 给客户端
    void closeConnection(int clientFd, size_t pollIndex);   // 断开连接（无 poll_index 传参更安全）
    void prePollCleanup();
    int executePoll(int &retries);
    void dispatchEvents();

    // 6. CGI 管道事件派发
    void handleCgiRead(int cgiReadFd);
    void handleCgiWrite(int cgiWriteFd);

    void cleanupClientWritePipe(int clientFd);

    void stop();

public:
    // 对外/组件间辅助接口
    void setClientEvents(int clientFd, short events);
    void registerFdToPoll(int fd, short events);
    void eraseFdFromPoll(int targetFd);

    // 构造与析构
    ServerManager(const std::vector<ServerConfig> &configs);
    ~ServerManager();

    // 4. 对外核心接口
    void init(); // 砸端口、建映射
    void run();  // 主事件轮询大循环

private:
    // 封杀 C++98 默认拷贝与赋值
    ServerManager(const ServerManager &src);
    ServerManager &operator=(const ServerManager &rhs);
};

#endif