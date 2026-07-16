#include "Webserv.hpp"

/**
 * @brief 构造函数：装配大管家管理的所有虚拟主机配置
 * 
 * @param configs 解析好的虚拟服务器配置容器
 */
ServerManager::ServerManager(const std::vector<ServerConfig> &configs) : _server_configs(configs)
{
}

/**
 * @brief 析构函数：践行 RAII 机制，彻底闭环清理所有服务器端口资源
 * 
 * @details 
 * 遍历 _listen_sockets 阵列并物理释放动态分配的 ServerSocket 实例。
 * 释放套接字指针时，ServerSocket 的析构函数会被自动触发并物理 close 掉它持有的物理套接字 FD，
 * 绝不给操作系统留下一丝端口死锁或 FD 泄漏的温床。
 */
ServerManager::~ServerManager()
{
    for (size_t i = 0; i < this->_listen_sockets.size(); ++i)
    {
        delete this->_listen_sockets[i];
    }
}

/**
 * @brief 服务器冷启动一键初始化入口
 * 
 * @note 作为 setupSockets() 的对外包装接口，起到语义隔离作用。
 */
void ServerManager::init()
{
    std::cout << "[ServerManager] Initializing network sockets..." << std::endl;
    this->setupSockets();
}

/**
 * @brief 网络套接字物理装配车间
 * 
 * @details 
 * 1. 遍历所有传入的虚拟服务器配置，执行【同端口去重防御机制】。
 *    防止多个具有不同 server_name 但端口相同的虚拟主机重复绑定（bind）同一端口而导致崩溃。
 * 2. 工厂化动态创建 ServerSocket 实例，并调用其 setup() 开启内核监听。
 * 3. 获取物理监听 listenFd，在大管家的 _listen_socket_map 字典中做好账本登记。
 * 4. 物理组装 struct pollfd 结构体，注册 POLLIN 读事件，并压入 _poll_fds 监听阵列。
 */
void ServerManager::setupSockets()
{
    std::vector<int> handled_ports;

    for (size_t i = 0; i < _server_configs.size(); ++i)
    {
        int port = _server_configs[i].port;
        std::string host = _server_configs[i].host;
        
        // 物理去重探测
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

        // 工厂实例化
        ServerSocket *srv_sock = new ServerSocket(host, port);
        srv_sock->setup();
        int listenFd = srv_sock->getFd();
        std::cout << "[ServerManager] Successfully listening on " << host << ":" << port << " (FD: " << listenFd << ")" << std::endl;
        
        this->_listen_sockets.push_back(srv_sock);
        handled_ports.push_back(port);
        _listen_socket_map[listenFd] = _server_configs[i];
        
        // 挂载 poll 哨兵
        struct pollfd pfd;
        pfd.fd = listenFd;
        pfd.events = POLLIN;
        pfd.revents = 0;
        _poll_fds.push_back(pfd);
    }
}

/**
 * @brief 垃圾回收车间：在执行 poll 等待前，物理清洗已被标记死亡的 FD
 * 
 * @details 
 * 在事件派发阶段，由于客户端挂断或异常，部分连接对应的 fd 可能会被直接置为安全哨兵值 -1。
 * 为了防止 poll 监听阵列不断膨胀以及无效扫描，此函数采用原地移位删除，
 * 彻底清除所有已经被标记为 -1 的无效节点，保持核心事件阵列的高效与清爽。
 */
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

/**
 * @brief 执行真正的内核多路复用 poll 等待
 * 
 * @param retries poll 临时中断时的重试计数器引用
 * @return int 
 *         - 正数：代表当前已就绪的活跃通道个数；
 *         - 0：由于 EINTR 信号等导致的中断，可允许外层安全 continue 重新唤醒；
 *         - -1：连续三次系统报错彻底失败，宣告主循环发生崩溃性熔断。
 * 
 * @note 
 * 最后一个参数传入 -1，表示采取无限阻塞挂起（Blocking wait），直至网线上有电信号波动。
 */
int ServerManager::executePoll(int &retries)
{
    int ret = poll(&this->_poll_fds[0], this->_poll_fds.size(), -1);

    if (ret < 0)
    {
        if (retries < 3)
        {
            retries++;
            return 0; 
        }
        return -1; 
    }

    retries = 0; // 成功捕获事件，物理复位防崩溃计数器
    return ret;
}

/**
 * @brief 网络事件多路精准派发车间
 * 
 * @details 
 * 💡【核心防错法设计 ──── 逆向倒序扫描安全算法】：
 * 传统的正序扫描 (0 -> size-1) 在事件处理中极为危险。如果在处理某个 FD 读写时触发了断连
 * 并将其从 vector 中 erase 抹除，正序扫描会导致后续所有元素的物理索引瞬间前移，
 * 从而直接发生“越界踩空崩溃 (Segment Fault)”或“无端漏掉下一个就绪事件”。
 * 
 * 此处采用【从 size 递减至 1】的逆向扫描判定，无论前面的元素如何销毁重组，
 * 均不影响当前扫描位置（idx）左侧的元素稳定性。
 * 
 * 1. 黄金拦截：跳过被其他子函数置为 -1 的死线，或无任何就绪信号（revents == 0）的闲置通道。
 * 2. 分支 A (异常拦截)：若捕获 POLLERR/POLLHUP/POLLNVAL 等物理断裂信号，
 *    根据套接字类型，优雅注销监听端或调用 closeConnection 销毁客户端。
 * 3. 分支 B (读事件分流)：若有 POLLIN，通过 isListenFd 判定：
 *    - 是监听端口 ──► “迎宾通道”，调用 acceptNewConnection 物理接纳新客。
 *    - 是普通客户端 ──► “数据通道”，调用 handleClientRead 接管网线读取。
 * 4. 分支 C (写事件派发)：若捕获 POLLOUT ──► “发送通道”，调用 handleClientWrite 倾倒缓存区响应数据。
 */
void ServerManager::dispatchEvents()
{
    for (size_t i = this->_poll_fds.size(); i > 0; --i)
    {
        size_t idx = i - 1;

        if (this->_poll_fds[idx].fd == -1 || this->_poll_fds[idx].revents == 0)
            continue;

        int activeFd = this->_poll_fds[idx].fd;

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

        if (this->_poll_fds[idx].revents & POLLIN)
        {
            if (this->isListenFd(activeFd))
                this->acceptNewConnection(activeFd);
            else
                this->handleClientRead(activeFd, idx);
        }
        else if (this->_poll_fds[idx].revents & POLLOUT)
        {
            this->handleClientWrite(activeFd, idx);
        }
    }
}

/**
 * @brief 开启 Web 服务的时空奇点 ─── 运行大管家主循环
 * 
 * @details 
 * 启动后，程序将彻底进入单线程下的多路复用死循环（Entering the matrix...），
 * 依靠 prePollCleanup() -> executePoll() -> dispatchEvents() 
 * 三个子车间的环环相扣，提供极其高效、低延迟的高并发 Web 静态、动态服务。
 */
void ServerManager::run()
{
    if (this->_poll_fds.empty())
        return;
    std::cout << "[ServerManager] Main loop started. Entering the matrix..." << std::endl;

    int poll_error_retries = 0;
    while (true)
    {
        this->prePollCleanup();

        int ret = this->executePoll(poll_error_retries);
        if (ret < 0) 
            break; 

        this->dispatchEvents();
    }
}