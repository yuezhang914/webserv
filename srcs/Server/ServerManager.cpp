#include "Webserv.hpp"

ServerManager::ServerManager(const std::vector<ServerConfig> &configs) : _server_configs(configs)
{
}

ServerManager::~ServerManager()
{
    // RAII 机制：遍历 delete 这些套接字指针时，它们自己的析构函数会自动优雅地 close 掉它们持有的物理 FD！
    for (size_t i = 0; i < this->_listen_sockets.size(); ++i)
    {
        delete this->_listen_sockets[i];
    }
}

/**
 * 冷启动一键初始化：绑定端口、建映射
 */
void ServerManager::init()
{
    std::cout << "[ServerManager] Initializing network sockets..." << std::endl;
    this->setupSockets();
}

void ServerManager::setupSockets()
{
    std::vector<int> handled_ports;

    for (size_t i = 0; i < _server_configs.size(); ++i)
    {
        int port = _server_configs[i].port;
        std::string host = _server_configs[i].host;
        // 去重
        bool port_duplicate = false;
        for (size_t p = 0; p < handled_ports.size(); ++p)
        {
            if (handled_ports[p] == port)
            {
                port_duplicate = true;
                break;
            }
        }
        if (port_duplicate)
        {
            std::cout << "[ServerManager] Multi-server configuration detected for port " << port << " (Skipping duplicate bind)" << std::endl;
            continue;
        }
        // 1. 工厂化生产：实例化一个独立的、安全的套接字对象
        ServerSocket *srv_sock = new ServerSocket(host, port);
        // 2. 扔给它自己初始化（socket -> reuse -> non-block -> bind -> listen）
        srv_sock->setup();
        int listenFd = srv_sock->getFd();
        std::cout << "[ServerManager] Successfully listening on " << host << ":" << port << " (FD: " << listenFd << ")" << std::endl;
        // 3. 大管家只做登记工作
        this->_listen_sockets.push_back(srv_sock);
        handled_ports.push_back(port);
        _listen_socket_map[listenFd] = _server_configs[i];
        // 4. 组装标准 pollfd，挂载进大阵列
        struct pollfd pfd;
        pfd.fd = listenFd;
        pfd.events = POLLIN;
        pfd.revents = 0;
        _poll_fds.push_back(pfd);
    }
}

void ServerManager::prePollCleanup()
{
    for (size_t i = 0; i < this->_poll_fds.size();)
    {
        if (this->_poll_fds[i].fd == -1)
        {
            this->_poll_fds.erase(this->_poll_fds.begin() + i);
        }
        else
        {
            ++i;
        }
    }
}

int ServerManager::executePoll(int &retries)
{
    int ret = poll(&this->_poll_fds[0], this->_poll_fds.size(), -1);

    if (ret < 0)
    {
        if (retries < 3)
        {
            retries++;
            return 0; // 返回 0 代表可以让外层循环 continue 重新唤醒
        }
        return -1; // 连续 3 次彻底失败，宣告主循环熔断
    }

    retries = 0; // 成功后立刻物理复位计数器
    return ret;
}

void ServerManager::dispatchEvents()
{
    // 倒序安全扫描
    for (size_t i = this->_poll_fds.size(); i > 0; --i)
    {
        size_t idx = i - 1;

        // 拦截在这一轮子函数处理中突发被改成 -1 的死线，或者没事件发生就跳过
        if (this->_poll_fds[idx].fd == -1 || this->_poll_fds[idx].revents == 0)
            continue;

        int activeFd = this->_poll_fds[idx].fd;

        // A. 拦截致命异常（学姐之前的纠错版完美并轨）
        if (this->_poll_fds[idx].revents & (POLLERR | POLLHUP | POLLNVAL))
        {
            if (this->isListenFd(activeFd))
            {
                std::cerr << "[ServerManager] CRITICAL: Fatal event (" << this->_poll_fds[idx].revents 
                          << ") on Listen FD " << activeFd << "!" << std::endl;
                this->_poll_fds[idx].fd = -1;
            }
            else
            {
                this->closeConnection(activeFd, idx);
            }
            continue;
        }

        // B. 拦截读事件（分流：迎宾 vs 接客数据）
        if (this->_poll_fds[idx].revents & POLLIN)
        {
            if (this->isListenFd(activeFd))
                this->acceptNewConnection(activeFd);
            else
                this->handleClientRead(activeFd, idx);
        }
        // C. 拦截写事件
        else if (this->_poll_fds[idx].revents & POLLOUT)
        {
            this->handleClientWrite(activeFd, idx);
        }
    }
}

void ServerManager::run()
{
    if (this->_poll_fds.empty())
        return;
    std::cout << "[ServerManager] Main loop started. Entering the matrix..." << std::endl;

    int poll_error_retries = 0;
    while (true)
    {
        // 车间 1：清扫废弃的 FD 节点
        this->prePollCleanup();

        // 车间 2：执行真正的内核等待
        int ret = this->executePoll(poll_error_retries);
        if (ret < 0) 
            break; // 触发连续 3 次报错的黄金熔断

        // 车间 3：解析并分发就绪的网络事件
        this->dispatchEvents();
    }
}