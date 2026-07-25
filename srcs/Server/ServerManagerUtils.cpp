#include "Webserv.hpp"
#include "SessionStore.hpp"
#include "CgiHandler.hpp"
/**
 * 函数：ServerManager::isListenFd
 * 用乎：在 poll() 监听到事件响了之后，用来判定当前被唤醒的文件描述符（fd）究竟是大厅的主监听端口，还是已经建立的普通客户端连接。
 * 参数来源：来自 run() 主生命周期大循环中正在被遍历的当前活跃节点：_poll_fds[i].fd。
 * 变量解释：
 *     - fd：需要进行身份鉴别的高危目标文件描述符。
 *     - _listen_socket_map：关联数组映射表，里面只保存了在冷启动 setupSockets() 阶段绑定的合法 listenFd。
 * 实现逻辑：
 *     1. 拿着传入的 fd 作为 key，去专属的 _listen_socket_map 映射表中执行 count() 查找。
 *     2. 如果 count 返回大于 0（即非零），说明该 fd 存在于监听大户籍中，代表它是大厅的主监听端口，返回 true。
 *     3. 反之，如果找不到，说明它是一个普通的、由 accept 新生出来的客户端会话，返回 false。
 * 后续影响：主大循环依据该函数的布尔裁决结果流向不同的处理车间：
 *           - 若为 true：立刻分流去调用 acceptNewConnection() 去诞生成立新的新客户；
 *           - 若为 false：立刻分流去调用 handleClientRead() 或 handleClientWrite() 来处理真实的 HTTP 业务数据。
 */
bool ServerManager::isListenFd(int fd)
{
    // 1. 拿着传入的 fd 作为 key，去专属的 _listen_socket_map 映射表中执行 count() 查找
    if (this->_listen_socket_map.count(fd) > 0)
    {
        // 2. 如果存在于监听大户籍中，代表它是大厅的主监听端口，返回 true
        return true;
    }
    // 3. 反之，如果找不到，说明它是一个普通的客户端会话，返回 false
    return false;
}

bool ServerManager::isCgiReadFd(int fd) const
{
    return this->_cgi_read_fd_to_client_map.find(fd) != this->_cgi_read_fd_to_client_map.end();
}

bool ServerManager::isCgiWriteFd(int fd) const
{
    return this->_cgi_write_fd_to_client_map.find(fd) != this->_cgi_write_fd_to_client_map.end();
}

/*
成员函数：精准重置客户端 FD 的 poll 监听事件（覆盖原 events，而非 |=）
*/
void ServerManager::setClientEvents(int clientFd, short events)
{
    for (size_t i = 0; i < this->_poll_fds.size(); ++i)
    {
        if (this->_poll_fds[i].fd == clientFd)
        {
            this->_poll_fds[i].events = events;
            return;
        }
    }
}


/*
函数用途：作为多路复用雷达网的防卫外科车间，在 _poll_fds 阵列中纵向检索指定 FD，找到后将其物理抹除并缩容 vector 舱位，严防悬空 FD 污染 poll 监听。
参数与变量：
- targetFd (传入参数)：int，亟待从多路复用雷达网中注销剔除的物理文件描述符（如 cgi_read_fd 或 cgi_write_fd）。
- i (局部卡尺变量)：size_t，遍历 _poll_fds 动态阵列的检索游标。
实现逻辑：
1. 🛡️ 边界防卫：点验 targetFd 有效性，若传入的是 -1（未开通状态），当场优雅折返。
2. 🔍 物理检索：自头至尾正向扫描 this->_poll_fds 动态向量阵列。
3. 🪓 物理剜除与缩容：一旦匹配到 this->_poll_fds[i].fd == targetFd，立刻调用 vector::erase(begin() + i)
   将其从内存轨道上彻底剔除并压缩阵列，随后断开循环，向大管家交割绝对干净的雷达网时空！
*/
void ServerManager::eraseFdFromPoll(int targetFd)
{
    if (targetFd == -1)
        return;

    for (size_t i = 0; i < this->_poll_fds.size(); ++i)
    {
        if (this->_poll_fds[i].fd == targetFd)
        {
            // 💡 软标记为 -1！poll() 会自动跳过，绝不引发 Vector 迭代器/索引物理抖动！
            this->_poll_fds[i].fd = -1;
            this->_poll_fds[i].events = 0;
            this->_poll_fds[i].revents = 0;
            break;
        }
    }
}


void ServerManager::closeConnection(int clientFd, size_t pollIndex)
{
    // 💡 1. 物理强杀与清理该 clientFd 对应的 CGI 任务（CgiManager 内部自动完成 kill + close + erase）
    this->_cgiManager.removeTaskByClientFd(clientFd);

    // 💡 2. 擦除 ServerManager 侧的反查雷达映射（读端 & 写端）
    std::map<int, int>::iterator readIt = this->_cgi_read_fd_to_client_map.begin();
    while (readIt != this->_cgi_read_fd_to_client_map.end())
    {
        if (readIt->second == clientFd)
        {
            this->eraseFdFromPoll(readIt->first); // 顺便把读管道 FD 从 _poll_fds 中擦除
            this->_cgi_read_fd_to_client_map.erase(readIt++);
        }
        else
            ++readIt;
    }

    std::map<int, int>::iterator writeIt = this->_cgi_write_fd_to_client_map.begin();
    while (writeIt != this->_cgi_write_fd_to_client_map.end())
    {
        if (writeIt->second == clientFd)
        {
            this->eraseFdFromPoll(writeIt->first); // 顺便把写管道 FD 从 _poll_fds 中擦除
            this->_cgi_write_fd_to_client_map.erase(writeIt++);
        }
        else
            ++writeIt;
    }

    // 💡 3. 销毁 Connection 实体（RAII 析构触发 ::close(clientFd)）
    std::map<int, Connection *>::iterator it = this->_connections.find(clientFd);
    if (it != this->_connections.end())
    {
        Connection *connection = it->second;
        delete connection;
        this->_connections.erase(it);
    }

    // 💡 4. 抹去 poll 雷达网槽位（软置 -1 防索引抖动）
    if (pollIndex < this->_poll_fds.size() && this->_poll_fds[pollIndex].fd == clientFd)
    {
        this->_poll_fds[pollIndex].fd = -1;
        this->_poll_fds[pollIndex].events = 0;
        this->_poll_fds[pollIndex].revents = 0;
    }
    else
    {
        this->eraseFdFromPoll(clientFd);
    }

    std::cout << "[ServerManager] Client FD " << clientFd << " successfully closed and cleaned up." << std::endl;
}

/*
函数用途：在拉起多路复用大闸前执行前置洗舱，全量剔除名册中已被打上 -1 熔断标记的废弃 FD，完成名册物理瘦身。
参数与变量：
- _poll_fds (类内部常驻容器)：std::vector<struct pollfd>，大管家赖以生存的多路复用全局核心轮询监视名册。
实现逻辑：
1. 动态变长正序扫描：纵向遍历全局轮询名册 _poll_fds。为了完美应对 vector 执行 erase 擦除导致的元素整体物理前移，
   必须在指针平移上采用“不进则删”的变长控制策略。
2. 物理销户与名册瘦身：若发现当前节点的 fd 已经沦为 -1（说明它已在之前的 handle 业务或熔断车间中宣告破产并关闭），
   则果断将其从 vector 名册中物理擦除，且索引 i 保持原地不动（用来精准迎击前移上来的下一个新节点）；否则，i 自增推进。
3. 筑坝防线：此举确保下一轮 executePoll 调用 ::poll 时，喂给 Linux 内核的名册全部由百分之百健康的活跃 FD 组成，
   彻底封杀因为带入脏 FD 导致内核疯狂报错、CPU 空转忙轮询的性能隐患。
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

/*
函数用途：物理拉起多路复用核心轮询大闸（::poll），并用分级弹性防线死守信号中断带来的系统震荡。
参数与变量：
- retries (传入引用参数)：外部主循环托付的防御性崩溃计数器，用来记录连续发生系统中断的次数。
- _poll_fds (类内部常驻容器)：std::vector<struct pollfd>，大管家赖以生存的全局多路复用核心轮询监视名册。
- ret (局部变量)：Linux 内核本次弹回的就绪事件总数，或代表负面危机的系统级错误代码。
实现逻辑：
1. 传入 -1 物理阻塞参数，强行命令主线程让出 CPU，陷入非阻塞全天候休眠，直到雷达名册中任意 FD 触发就绪。
2. 熔断危机拦截：若 poll 返回负数报错，启动分级弹性防线。在 3 次以内视作常规信号（如 EINTR）引发的轻微震荡，
   计数器自增并安全返回 0，迫使主循环无感刷新；一旦跨过 3 次红线则触发彻底熔断，断开危机大闸返回 -1。
3. 黄金复位并网：若成功捕捉到哪怕一字节的有效网络事件，立刻将崩溃计数器强行物理复位归零，重新开启帝国防御！
*/
int ServerManager::executePoll(int &retries)
{
    int ret = ::poll(&this->_poll_fds[0], this->_poll_fds.size(), 1000);

    if (ret < 0)
    {
        // 💡 1. 显式捕获 EINTR 信号打断：直接算作正常暂态，重置/不计入致命错误 retry
        if (errno == EINTR)
        {
            return 0; // 信号打断，安全跳过，下一轮继续 poll
        }

        // 💡 2. 真正的其他致命底层 poll 错误（如 EFAULT, EINVAL），触发弹性重试
        if (retries < 3)
        {
            std::cerr << "[executePoll] Warning: poll() system call failed with errno "
                      << errno << ", retrying (" << retries + 1 << "/3)..." << std::endl;
            retries++;
            return 0;
        }

        std::cerr << "[executePoll] Fatal: poll() failed consecutively over limit! Terminating main loop." << std::endl;
        return -1; // 连续 3 次致命错误，彻底熔断
    }

    retries = 0; // 成功捕获事件或超时，物理复位防崩溃计数器
    return ret;
}

/*
函数用途：将新生的物理文件描述符（Socket 或 Pipe）正式编入多路复用大循环，开启全天候事件监控。
参数与变量：
- fd (传入参数)：刚破壳而出（通过 accept 或 pipe 物理孵化）且亟待编入帝国防御网的底层文件描述符。
- events (传入参数)：预设的监控雷达事件方向，如代表接收数据的 POLLIN，或代表高效发货的 POLLOUT。
- pfd (局部变量)：系统原生 struct pollfd 结构体，作为大管家与 Linux 内核进行主权沟通的物理载体。
- _poll_fds (类内部常驻容器)：std::vector<struct pollfd>，大管家赖以生存的全局多路复用核心轮询监视名册。
实现逻辑：
1. 实例化一个物理 pollfd 节点，绑定目标文件描述符，锁定数据对流通道。
2. 灌入指定的监听事件类型（events），并手工强行将内核回执层（revents）洗白清零，彻底封杀幽灵事件的误触触发。
3. 执行 push_back 操作让该资产在 _poll_fds 名册里物理入籍，在下一轮 poll 轮询中正式享受多路复用全量护航！
*/
void ServerManager::registerFdToPoll(int fd, short events)
{
    if (fd < 0)
    {
        std::cerr << "[ServerManager] Error: Attempted to register invalid negative FD: " << fd << std::endl;
        return;
    }

    // 1. 防重复注册防线：检查该 FD 是否已经在 _poll_fds 名册中
    for (size_t i = 0; i < this->_poll_fds.size(); ++i)
    {
        if (this->_poll_fds[i].fd == fd)
        {
            std::cout << "[ServerManager] Notice: FD " << fd << " already registered in poll tree. Updating events instead." << std::endl;
            this->_poll_fds[i].events = events; // 存在则直接覆写事件掩码
            this->_poll_fds[i].revents = 0;
            return;
        }
    }

    // 2. 正常入籍
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = events; // 传入 POLLIN / POLLOUT
    pfd.revents = 0;     // 清空内核回执，彻底杜绝幽灵触发

    this->_poll_fds.push_back(pfd); // 正式入籍大循环名册

    std::cout << "[ServerManager] FD " << fd << " successfully registered to poll tree with events: " << events << std::endl;
}

