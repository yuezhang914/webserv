#include "Webserv.hpp"
#include "SessionStore.hpp"
#include "CgiHandler.hpp"

/*
函数用途：判定当前文件描述符（fd）是否为主监听套接字。
参数与变量：
- fd：待鉴别的目标文件描述符。
- _listen_socket_map：保存所有合法监听套接字的映射表。
实现逻辑：
1. 以 fd 为键检索 _listen_socket_map。
2. 若存在则返回 true（代表主监听端口），否则返回 false（代表普通客户端连接）。
*/
bool ServerManager::isListenFd(int fd)
{
    if (this->_listen_socket_map.count(fd) > 0)
        return true;
    return false;
}

/*
函数：ServerManager::isCgiReadFd
用途：查询指定的 FD 是否为 ServerManager 正在监听的 CGI 管道读端（CGI stdout -> Webserv）。
参数：
    - int fd: 待鉴定的文件描述符。
返回值：
    - bool: 若该 FD 存在于 _cgi_read_fd_to_client_map 账本中返回 true；否则返回 false。
实现逻辑或说明：
    1. 在 _cgi_read_fd_to_client_map 映射账本中执行 find(fd) 检索。
    2. 将迭代器与 end() 进行比对，确定该 FD 是否为某个 CGI 任务的管道读端。
    3. 供 dispatchEvents 事件引擎使用，用于将 POLLIN/POLLHUP 事件精准路由至 handleCgiRead 进行数据读取。
*/
bool ServerManager::isCgiReadFd(int fd) const
{
    return this->_cgi_read_fd_to_client_map.find(fd) != this->_cgi_read_fd_to_client_map.end();
}

/*
函数：ServerManager::isCgiWriteFd
用途：查询指定的 FD 是否为 ServerManager 正在监听的 CGI 管道写端（Webserv -> CGI stdin）。
参数：
    - int fd: 待鉴定的文件描述符。
返回值：
    - bool: 若该 FD 存在于 _cgi_write_fd_to_client_map 账本中返回 true；否则返回 false。
实现逻辑或说明：
    1. 在 _cgi_write_fd_to_client_map 映射账本中执行 find(fd) 检索。
    2. 将迭代器与 end() 进行比对，确定该 FD 是否为某个 CGI POST 请求的管道写端。
    3. 供 dispatchEvents 事件引擎使用，用于将 POLLOUT 事件精准路由至 handleCgiWrite 进行 POST Body 切片写入。
*/
bool ServerManager::isCgiWriteFd(int fd) const
{
    return this->_cgi_write_fd_to_client_map.find(fd) != this->_cgi_write_fd_to_client_map.end();
}

/*
函数：ServerManager::setClientEvents
用途：在多路复用循环中重置指定客户端 Socket 的 poll 监听事件（直接覆盖赋值，而非位或追加）。
参数：
    - int clientFd : 目标客户端 Socket 文件描述符。
    - short events : 新的 poll 事件掩码（如 POLLIN、POLLOUT 或 0）。
实现逻辑：
1. 遍历 _poll_fds 监视队列。
2. 匹配到目标 clientFd 后，将其 events 直接覆盖为新的事件值。
3. 使用直接赋值（=）而非位或（|=），可防止同时监听 POLLIN 和 POLLOUT 导致事件误触发；找到后立即返回。
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
1. 边界防卫：点验 targetFd 有效性，若传入的是 -1（未开通状态），当场优雅折返。
2. 物理检索：自头至尾正向扫描 this->_poll_fds 动态向量阵列。
3. 物理剜除与缩容：一旦匹配到 this->_poll_fds[i].fd == targetFd，立刻调用 vector::erase(begin() + i)
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
            this->_poll_fds[i].fd = -1;
            this->_poll_fds[i].events = 0;
            this->_poll_fds[i].revents = 0;
            break;
        }
    }
}

/*
函数：ServerManager::closeConnection
用途：当客户端断开连接、发生异常或传输完毕时，安全回收该 Connection 关联的所有物理资源与映射。
参数：
    - int clientFd    : 待断开清理的客户端 Socket 文件描述符。
    - size_t pollIndex: 该 clientFd 在 _poll_fds 监听队列中的当前索引下标。
实现逻辑：
1. 释放 CGI 资源：归还大文件/CGI 准入槽位，并调用 _cgiManager.removeTaskByClientFd 强行终止并回收对应的 CGI 子进程。
2. 清理管道映射：遍历并清理所有关联该 clientFd 的 CGI 读写管道 FD，将其从 poll 监听队列和映射表中移除。
3. 清理客户端 poll 槽位：将 _poll_fds 中对应位置设为失效（fd = -1），防止遍历时索引错位；必要时回退全局擦除。
4. 销毁会话实例：从 _connections 中查找并 delete 对应的 Connection 指针（通过 RAII 析构物理关闭 clientFd），最后打印调试日志。
*/
void ServerManager::closeConnection(int clientFd, size_t pollIndex)
{
    this->releaseLargeCgiSlot(clientFd);
    this->_cgiManager.removeTaskByClientFd(clientFd);
    std::map<int, int>::iterator readIt = this->_cgi_read_fd_to_client_map.begin();
    while (readIt != this->_cgi_read_fd_to_client_map.end())
    {
        if (readIt->second == clientFd)
        {
            this->eraseFdFromPoll(readIt->first); // 仅从 _poll_fds 移除/置-1
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
            this->eraseFdFromPoll(writeIt->first); // 仅从 _poll_fds 移除/置-1
            this->_cgi_write_fd_to_client_map.erase(writeIt++);
        }
        else
            ++writeIt;
    }
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
    std::map<int, Connection *>::iterator it = this->_connections.find(clientFd);
    if (it != this->_connections.end())
    {
        Connection *connection = it->second;
        delete connection;
        this->_connections.erase(it);
    }
    DEBUG_LOG("[ServerManager] Client FD " << clientFd << " successfully closed and cleaned up.");
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
            ++i;
    }
}

/*
函数用途：执行 poll 系统调用并处理中断/错误重试逻辑。
参数与变量：
- retries : 连续失败计数器，用于防范系统信号中断（EINTR）带来的震荡。
- _poll_fds : 全局多路复用监视名册。
- ret : poll 的返回值（就绪事件数或错误码）。
实现逻辑：
1. 监听调用：调用 ::poll 监视所有注册的 FD，超时时间设为 1000ms。
2. 错误与重试处理：若返回值小于 0（发生错误），递增计数器；若连续失败次数达到 5 次则触发熔断返回 -1，否则返回 0 尝试恢复。
3. 状态复位：若成功捕获事件（ret > 0），将连续失败计数器归零并返回就绪数。
*/
int ServerManager::executePoll(int &retries)
{
    int ret = ::poll(&this->_poll_fds[0], this->_poll_fds.size(), 1000);

    if (ret < 0)
    {
        retries++;
        std::cerr << "[executePoll] Warning: poll() returned " << ret
                  << ", retrying (" << retries << "/5)..." << std::endl;
        if (retries >= 5)
        {
            std::cerr << "[executePoll] Fatal: poll() failed consecutively over limit! Terminating main loop." << std::endl;
            return -1;
        }
        return 0;
    }
    if (ret > 0)
        retries = 0;
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
    for (size_t i = 0; i < this->_poll_fds.size(); ++i)
    {
        if (this->_poll_fds[i].fd == fd)
        {
            DEBUG_LOG("[ServerManager] Notice: FD " << fd << " already registered in poll tree. Updating events instead.");
            this->_poll_fds[i].events = events;
            this->_poll_fds[i].revents = 0;
            return;
        }
    }
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = events;
    pfd.revents = 0;
    this->_poll_fds.push_back(pfd);
    DEBUG_LOG("[ServerManager] FD " << fd << " successfully registered to poll tree with events: " << events);
}

/*
函数：ServerManager::cleanupClientWritePipe
用途：反查并安全清理指定客户端 Socket 绑定的 CGI 写管道 FD（从映射表与 poll 队列中移除）。
参数：
    - int clientFd: 目标客户端 Socket 文件描述符。
返回值：
    - void（无返回值）。
*/
void ServerManager::cleanupClientWritePipe(int clientFd)
{
    std::map<int, int>::iterator it = this->_cgi_write_fd_to_client_map.begin();
    while (it != this->_cgi_write_fd_to_client_map.end())
    {
        if (it->second == clientFd)
        {
            int writeFd = it->first;
            this->eraseFdFromPoll(writeFd);
            this->_cgi_write_fd_to_client_map.erase(it++);
            return;
        }
        else
            ++it;
    }
}
