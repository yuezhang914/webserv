#include "Webserv.hpp"

ServerManager::ServerManager(const std::vector<ServerConfig> &configs) : _server_configs(configs), _poll_fds()
{

}

ServerManager::~ServerManager()
{

}

/**
 * 冷启动一键初始化：绑定端口、建映射
 */
void ServerManager::init()
{
    std::cout << "[ServerManager] Initializing network sockets..." << std::endl;
    this->setupSockets();
}

/**
 * 函数：ServerManager::setNonBlocking
 * 用途：将指定的套接字（Socket）文件描述符强制设置为非阻塞（O_NONBLOCK）模式。
 * 参数来源：fd 来自本类内部的监听初始化阶段（setupSockets 产生的 listenFd）或运行时接受新连接阶段（acceptNewConnection 产生的 clientFd）。
 * 变量解释：
 *     - fd：需要修改状态旗帜的目标套接字文件描述符。
 *     - flags：临时变量，通过 fcntl 获取的当前套接字拥有的全部内核状态属性。
 * 实现逻辑：
 *     1. 调用 fcntl(fd, F_GETFL, 0) 捞出该套接字当前在内核中所有的属性旗帜，若失败则打印错误并熔断。
 *     2. 采用位运算“或（|）”操作，在原有旗帜的基础上追加 O_NONBLOCK 非阻塞属性。
 *     3. 调用 fcntl(fd, F_SETFL, ...) 将改版后的全新属性旗帜重新插回内核中以使其生效。
 * 后续影响：被处理的套接字在进行 read()/recv() 或 write()/send() 操作时将永远不会阻塞（Block）主线程。
 *           若无数据可读或发送缓冲区满，系统调用会立刻返回 -1 并设置 errno 为 EAGAIN，
 *           从而确保底层的 poll 大循环能以极速串行、永不卡死的状态持续驱动高并发网络流。
 */
void ServerManager::setNonBlocking(int fd)
{
    // 1. 调用 fcntl(fd, F_GETFL, 0) 捞出该套接字当前在内核中所有的属性旗帜
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0)
    {
        std::cerr << "Error: fcntl F_GETFL failed for fd " << fd << std::endl;
        return;
    }

    // 2. 采用位运算“或（|）”操作，在原有旗帜的基础上追加 O_NONBLOCK 非阻塞属性
    // 3. 调用 fcntl(fd, F_SETFL, ...) 将改版后的全新属性旗帜重新插回内核中
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
    {
        std::cerr << "Error: fcntl F_SETFL O_NONBLOCK failed for fd " << fd << std::endl;
    }
}

/**
 * 函数：ServerManager::setupSockets
 * 用途：拉起物理端口监听大网，创建、绑定并激活所有不重复端口的监听套接字。
 * 参数来源：内部数据成员 _server_configs（由构造函数接收的已通过校验的全量配置账本）。
 * 变量解释：
 *     - handled_ports：临时变量，记录哪些物理端口已经被成功执行过 bind 操作，防止重复绑定。
 *     - listenFd：新创建的物理监听套接字文件描述符。
 *     - port：当前遍历到的虚拟主机所期望监听的物理端口（如 80）。
 *     - host：当前虚拟主机配置绑定的 IP 地址字符串（如 "127.0.0.1"）。
 *     - port_duplicate：布尔旗帜，标记当前端口是否与已绑定端口重复。
 *     - reuse：整型开关，传递给 setsockopt 的参数，开启端口地址复用。
 *     - addr：sockaddr_in 网络地址结构体，用于封装绑定的协议族、IP 与端口号。
 *     - SOMAXCONN_BACKLOG：在头文件中定义的宏（值为 128），指定内核连接队列的最大积压上限。
 * 实现逻辑：
 *     1. 建立 handled_ports 登记簿，遍历配置账本，利用循环对比检查当前端口是否已被绑定过。
 *     2. 若检测到端口重复（port_duplicate 为 true），则直接跳过物理 bind 流程，留待运行时通过 Host 请求头实现多域名分流映射。
 *     3. 呼叫 socket() 系统调用创建流式套接字 listenFd。
 *     4. 注入 SO_REUSEADDR 属性，强力防止服务器意外重启时物理端口被内核扣留两分钟的 TIME_WAIT 闪退惨剧。
 *     5. 调用本类成员 setNonBlocking()，强行将新建的 listenFd 洗牌为非阻塞安全状态。
 *     6. 组装 sockaddr_in 结构体，执行 bind() 将套接字锁死在指定的 Host 和 Port 上。
 *     7. 调用 listen(listenFd, SOMAXCONN_BACKLOG) 开启监听，拒绝任何硬编码数值，打通全连接队列。
 *     8. 将成功激活的物理 listenFd 写入 _listen_socket_map 快捷映射表，并组装标准 pollfd 挂载进核心 _poll_fds 监听大阵列。
 * 后续影响：底层 poll 大循环自此拥有了捕获外部客户端连接（三次握手第一步）的物理入口。
 *           当有新浏览器访问对应端口时，该 listenFd 对应的 poll 节点会精准触发 POLLIN 事件。
 */
void ServerManager::setupSockets()
{
    // 临时防重登记簿：用来记录哪些端口（如 80, 8080）已经被 bind 过了
    std::vector<int> handled_ports;

    for (size_t i = 0; i < _server_configs.size(); ++i)
    {
        int port = _server_configs[i].port;
        std::string host = _server_configs[i].host;

        // 🔒 【战术去重拦截】：如果这个端口已经砸开过了，直接复用，绝不 bind 两次！
        bool port_duplicate = false;
        for (size_t p = 0; p < handled_ports.size(); ++p) {
            if (handled_ports[p] == port) {
                port_duplicate = true;
                break;
            }
        }
        
        if (port_duplicate)
        {
            // 虽然不重新 bind，但这个虚拟主机的配置必须能通过未来的 clientFd 关联到。
            // 我们可以在大循环诞生连接时，再统一做多域名匹配。现在先跳过物理 bind
            std::cout << "[ServerManager] Multi-server configuration detected for port " << port << " (Skipping duplicate bind)" << std::endl;
            continue;
        }

        // 1. 创建 socket
        int listenFd = socket(AF_INET, SOCK_STREAM, 0);
        if (listenFd < 0)
        {
            std::cerr << "Error: Cannot create socket for port " << port << std::endl;
            exit(1);
        }

        // 2. 开启 SO_REUSEADDR（防止服务器重启时端口被内核扣留 2 分钟的 TIME_WAIT 惨剧）
        int reuse = 1;
        if (setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0)
        {
            std::cerr << "Error: setsockopt(SO_REUSEADDR) failed" << std::endl;
            close(listenFd);
            exit(1);
        }

        // 3. 强制设置为 O_NONBLOCK 非阻塞（42 铁律：所有 socket 必须非阻塞）
        this->setNonBlocking(listenFd);

        // 4. 绑定物理地址
        struct sockaddr_in addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = inet_addr(host.c_str());

        if (bind(listenFd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
        {
            std::cerr << "Error: Cannot bind to " << host << ":" << port << std::endl;
            close(listenFd);
            exit(1);
        }

        // 5. 开始监听
        if (listen(listenFd, SOMAXCONN_BACKLOG) < 0) // SOMMAXCONN_BACKLOG 为全连接队列大小上限
        {
            std::cerr << "Error: Listen failed on port " << port << std::endl;
            close(listenFd);
            exit(1);
        }

        std::cout << "[ServerManager] Successfully listening on " << host << ":" << port << " (FD: " << listenFd << ")" << std::endl;

        // 6. 核心资产入库
        handled_ports.push_back(port);

        // 将生成的真实物理 listenFd 反向锁进快捷映射表中
        _listen_socket_map[listenFd] = _server_configs[i];

        // 组装标准 pollfd，挂载进大阵列，开始关注读事件（POLLIN：代表有人来连接）
        struct pollfd pfd;
        pfd.fd = listenFd;
        pfd.events = POLLIN;
        pfd.revents = 0;
        _poll_fds.push_back(pfd);
    }
}