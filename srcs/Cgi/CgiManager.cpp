#include "Webserv.hpp"

// CgiManager.cpp 内部的私有 Helper
void CgiManager::closeCgiWritePipe(Connection *conn, ServerManager &serverManager)
{
    if (conn == NULL)
        return;

    int writeFd = conn->getCgiWriteFd();
    if (writeFd > 0)
    {
        // 1. 从 CgiManager 自己的账本中擦除
        this->_write_fd_to_conn_map.erase(writeFd);

        // 2. 调用 ServerManager 的 public 接口从 Poll 网注销 FD
        serverManager.eraseFdFromPoll(writeFd);

        // 3. 物理关闭管道写端 FD 并重置 Connection 内部写端标记
        conn->closeWriteFd();
    }
}

 void CgiManager::closeCgiReadPipe(Connection *conn, ServerManager &serverManager)
 {
        if (conn == NULL)
        return;

    int readFd = conn->getCgiReadFd();
    if (readFd > 0)
    {
        // 1. 从 CgiManager 自己的账本中擦除
        this->_read_fd_to_conn_map.erase(readFd);

        // 2. 调用 ServerManager 的 public 接口从 Poll 网注销 FD
        serverManager.eraseFdFromPoll(readFd);

        // 3. 物理关闭管道写端 FD 并重置 Connection 内部写端标记
        conn->closeReadFd();
    }
 }

bool CgiManager::startCgiPipeline(Connection *conn, ServerManager &serverManager,
                                  const std::string &script_path, const std::string &interpreter_path)
{
    // 💡 1. 顶层安全防护：防止 NULL 指针解引用物理崩溃
    if (conn == NULL || conn->socket == NULL)
        return false;

    // 💡 2. 提前提取 clientFd，供全函数使用
    int clientFd = conn->socket->getFd();

    // 3. 实例化 CgiHandler 并物理启动子进程与管道
    CgiHandler cgi(conn->request, script_path, interpreter_path);
    CgiFds fds = cgi.async_launch();

    // ❌ CGI 启动失败 500 熔断
    if (fds.pid < 0 || fds.read_fd < 0 || fds.write_fd < 0)
    {
        std::cerr << "[CGI] Error: Failed to spawn CGI process for client " << clientFd << std::endl;
        conn->response.createResponse(500, "CGI Spawn Failed", conn->config.error_pages);
        conn->write_buffer = conn->response.responseToString();
        conn->close_after_write = true;

        // 💡 失败时直接激活 POLLOUT 准备发送 500 错误页
        serverManager.setClientEvents(clientFd, POLLOUT);
        return false;
    }

    // 🎯 【读端账本登记】：直接映射 cgi_read_fd -> Connection*
    this->_read_fd_to_conn_map[fds.read_fd] = conn;

    // 💡 【原子起航】：拉起 is_cgi、read_fd、write_fd、pid 并完成 time(NULL) 时间戳打点！
    conn->startCgi(fds.read_fd, fds.write_fd, fds.pid);

    // 1️⃣ 读端（CGI 管道）永远注册 POLLIN
    serverManager.registerFdToPoll(fds.read_fd, POLLIN);

    // 2️⃣ 写端（CGI 管道）按需注册
    if (!conn->request.getBody().empty())
    {
        this->_write_fd_to_conn_map[fds.write_fd] = conn;
        serverManager.registerFdToPoll(fds.write_fd, POLLOUT);
    }
    else
    {
        // 无 Body：通过 closeCgiWritePipe 安全清空写管道！
        this->closeCgiWritePipe(conn, serverManager);
    }

    // 💡 3️⃣ 🎯 【核心防线】：暂停客户端 Socket 监听，防止 Request 被后续数据覆盖！
    serverManager.setClientEvents(clientFd, 0);

    std::cout << "[⚡ WebServ Core] Client " << clientFd << " successfully split into CGI pipeline, client read paused." << std::endl;
    return true; // 🟢 成功拉起返回 true
}