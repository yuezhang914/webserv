#ifndef CGI_MANAGER_HPP
#define CGI_MANAGER_HPP

#include "Webserv.hpp"
#include <map>
#include <sys/types.h>
#include <ctime>

// 前置声明，打破 CgiManager 与 ServerManager / Connection 的循环引用
class ServerManager;
class Connection;

/**
 * @brief CGI 全生命周期调度器 (CGI Lifecycle Controller)
 * 
 * @details 
 * 将所有与 CGI 管道 I/O、子进程 Fork/Waitpid、看门狗超时熔断、卡尺切片等业务逻辑
 * 从 ServerManager 中彻底剥离，充当独立的 CGI 管理服务。
 */
class CgiManager
{
public:
    CgiManager();
    ~CgiManager();

    // -------------------------------------------------------------
    // 1. CGI 管道启动与接管 (Pipeline Spawner)
    // -------------------------------------------------------------
    /**
     * @brief 尝试为连接启动 CGI 进程，并把管道 FD 注册到 ServerManager 的 Poll 监听网中
     */
    bool startCgiPipeline(Connection *conn, ServerManager &serverManager, 
                          const std::string &scriptPath, const std::string &interpreterPath);

    // -------------------------------------------------------------
    // 2. 管道事件回调 (Reactor Handlers) - 由 ServerManager 的 poll 主循环触发
    // -------------------------------------------------------------
    void handlePipeRead(int cgiReadFd, size_t pollIdx, ServerManager &serverManager);
    void handlePipeWrite(int cgiWriteFd, size_t pollIdx, ServerManager &serverManager);

    // -------------------------------------------------------------
    // 3. 看门狗与后台回收 (Watchdog & Reaper)
    // -------------------------------------------------------------
    void enforceTimeouts(ServerManager &serverManager);
    void reapFinishedChildren(ServerManager &serverManager);

    // -------------------------------------------------------------
    // 4. 熔断与物理清理 (Failover & Teardown)
    // -------------------------------------------------------------
    void failCgi(Connection *conn, int statusCode, ServerManager &serverManager);
    void cleanupCgiResources(Connection *conn, ServerManager &serverManager);
    void cleanupConnectionCgi(Connection *conn, ServerManager &serverManager);

    // -------------------------------------------------------------
    // 5. 快速反查账本 Setter/Getter
    // -------------------------------------------------------------
    bool isCgiReadFd(int fd) const;
    bool isCgiWriteFd(int fd) const;

private:
    // 🧹 将原先 ServerManager 内部的私有反查账本全面收拢到这里！
    std::map<int, Connection *> _read_fd_to_conn_map;  // cgi_read_fd -> Connection*
    std::map<int, Connection *> _write_fd_to_conn_map; // cgi_write_fd -> Connection*

    // 私有管道关闭 Helper
    void closeCgiWritePipe(Connection *conn, ServerManager &serverManager);
    void closeCgiReadPipe(Connection *conn, ServerManager &serverManager);
    void releaseCgiProcess(Connection *conn);

    // 禁用拷贝构造与赋值
    CgiManager(const CgiManager &other);
    CgiManager &operator=(const CgiManager &other);
};

#endif