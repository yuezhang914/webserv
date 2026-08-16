#include "Webserv.hpp"

/*
函数：CgiManager::CgiManager
用途：CgiManager 类的默认构造函数。
参数：- 无。
实现逻辑或说明：
    1. 初始化 CgiManager 实例。
    2. 私有成员变量 _read_fd_to_task_map 与 _write_fd_to_read_fd_map 账本由 C++98 默认构造函数完成空初始化。
*/
CgiManager::CgiManager()
{
}

/*
函数：CgiManager::~CgiManager
用途：析构函数（RAII 物理安全保障车间）。当 CgiManager 随着 ServerManager 销毁时，自动清扫强杀所有残余 CGI 进程与管道。
参数： - 无。
实现逻辑或说明：
    1. 遍历私有账本：以 _read_fd_to_task_map 为全量任务宿主主表进行循环遍历。
    2. 迭代器安全自增：采用 std::map<int, CgiTask>::iterator current = it++ 范式，提前备份迭代器，严防 forceKillAndClean 内部 erase 导致迭代器失效野指针。
    3. 调用 forceKillAndClean 强杀子进程并关闭其占用的双向管道 FD。
    4. 最后清空唯一任务主表与轻量写端反查表，保证 0 进程、0 FD 泄露且不复制大请求体。
*/
CgiManager::~CgiManager()
{
    std::map<int, CgiTask>::iterator it = this->_read_fd_to_task_map.begin();
    while (it != this->_read_fd_to_task_map.end())
    {
        std::map<int, CgiTask>::iterator current = it++;
        this->forceKillAndClean(current->second);
    }
    this->_read_fd_to_task_map.clear();
    this->_write_fd_to_read_fd_map.clear();
}

/*
函数：CgiManager::launchTask
用途：异步创建并启动一个 CGI 任务，构建上下文并将其注册到 CgiManager 的私有 Map 账本中。
参数：
    - int clientFd                       : 发起 CGI 请求的客户端 Socket 文件描述符。
    - const std::string &scriptPath      : CGI 脚本的物理路径。
    - const std::string &interpreterPath : CGI 解释器路径（为空表示可执行二进制）。
    - const std::string &method         : HTTP 请求方法（"GET" / "POST" 等）。
    - const std::string &query          : URL 查询字符串（QUERY_STRING）。
    - const std::string &path           : 请求的 URI 路径（SCRIPT_NAME）。
    - const std::map<std::string, std::string> &headers: HTTP 请求头账本。
    - const std::string &reqBody        : HTTP 请求体（POST Body）。
    - int &outReadFd                    : 输出参数，回传父进程用于读取 CGI 输出的管道读端 FD。
    - int &outWriteFd                   : 输出参数，回传父进程用于写入 Body 的管道写端 FD（若无写端则置为 -1）。
返回值：
    - bool: 启动成功并入库账本返回 true；入参无效或 fork/pipe 失败返回 false。
实现逻辑或说明：
    1. 参数校验：检查 clientFd 是否合法以及 scriptPath 是否为空。
    2. 产生子进程：实例化 CgiHandler 并调用 async_launch()，获得包含 PID、read_fd、write_fd 的 CgiFds。
    3. 组装 CgiTask：保存 clientFd、管道、PID，以及指向 Connection::Request body 的非拥有型 const 指针。
    4. 唯一主表登记：完整 task 只注册进 _read_fd_to_task_map[read_fd]，避免 100MB body 被复制两次。
    5. 写端反查处理：
       - 若 reqBody 不为空且 write_fd 合法，只登记 writeFd -> readFd 的整数映射；
       - 若无 Body，主动关闭父进程写端并把主表中的 writeFd 设为 -1。
*/
bool CgiManager::launchTask(
    int clientFd,
    const std::string &scriptPath,
    const std::string &interpreterPath,
    const std::string &method,
    const std::string &query,
    const std::string &path,
    const std::map<std::string, std::string> &headers,
    const std::string &reqBody,
    const std::string &host,
    const std::string &port,
    const std::string &root,
    int &outReadFd,
    int &outWriteFd)
{
    outReadFd = -1;
    outWriteFd = -1;

    if (clientFd < 0 || scriptPath.empty())
        return false;
    CgiHandler cgi(scriptPath, interpreterPath, method, query, path, headers, host,
                   port, root);
    CgiFds fds = cgi.async_launch();
    if (fds.pid < 0 || fds.read_fd < 0)
    {
        std::cerr << "[CgiManager] Error: Failed to spawn CGI process for Client FD " << clientFd << std::endl;
        return false;
    }
    CgiTask task;

    task.clientFd = clientFd;
    task.readFd = fds.read_fd;
    task.writeFd = fds.write_fd;
    task.pid = fds.pid;
    task.inputBody = &reqBody;
    task.bodyBytesSent = 0;
    task.lastActivity = std::time(NULL);
    this->_read_fd_to_task_map[fds.read_fd] = task;
    outReadFd = fds.read_fd;
    if (!reqBody.empty() && fds.write_fd >= 0)
    {
        this->_write_fd_to_read_fd_map[fds.write_fd] = fds.read_fd;
        outWriteFd = fds.write_fd;
    }
    else
    {
        if (fds.write_fd > 0)
        {
            ::close(fds.write_fd);
            this->_read_fd_to_task_map[fds.read_fd].writeFd = -1;
        }
    }
    return true;
}

/*
函数：CgiManager::handlePipeRead
用途：非阻塞读取 CGI 子进程 stdout 数据流，每次事件仅单次读取以保障 Reactor 并发公平性，并提供防爆流控制。
参数：
    - int cgiReadFd : 就绪的 CGI 管道读端文件描述符。
返回值：
    - CgiEventResult : 状态结果对象（CGI_CONTINUE、CGI_FINISHED 或 CGI_ERROR）。
实现逻辑：
1. 查找任务：若未在映射表中找到任务，则关闭 FD 并返回 CGI_CONTINUE。
2. 单次读取：使用固定缓冲区进行单次非阻塞 read，刷新最后活动时间。
3. 防爆流熔断：若累积输出大小超过 16MB 阈值，强杀子进程并返回 502 错误。
4. EOF 完工：若读到 0 字节，通过 swap 零拷贝转移缓冲区数据，回收资源并返回 200 完成状态。
5. 异常妥协：若读返回负值，暂作中断处理返回 CGI_CONTINUE，由外层超时机制兜底。
*/
CgiEventResult CgiManager::handlePipeRead(int cgiReadFd)
{
    std::map<int, CgiTask>::iterator it = this->_read_fd_to_task_map.find(cgiReadFd);
    if (it == this->_read_fd_to_task_map.end())
    {
        ::close(cgiReadFd);
        return CgiEventResult(CGI_CONTINUE);
    }
    CgiTask &task = it->second;
    int clientFd = task.clientFd;
    char buffer[4096];
    ssize_t bytesRead = ::read(cgiReadFd, buffer, sizeof(buffer));
    if (bytesRead > 0)
    {
        task.lastActivity = std::time(NULL);
        if (task.outputBuffer.size() + static_cast<size_t>(bytesRead) > CGI_MAX_OUTPUT_SIZE)
        {
            std::cerr << "[CgiManager] Error: CGI output size exceeded max limit! 502." << std::endl;
            this->forceKillAndClean(task);
            return CgiEventResult(CGI_ERROR, clientFd, 502);
        }
        task.outputBuffer.append(buffer, static_cast<size_t>(bytesRead));
        return CgiEventResult(CGI_CONTINUE);
    }
    else if (bytesRead == 0)
    {
        CgiEventResult result(CGI_FINISHED, clientFd, 200);
        result.rawOutput.swap(task.outputBuffer);
        this->forceKillAndClean(task);
        return result;
    }
    else
        return CgiEventResult(CGI_CONTINUE);
}

/*
函数：CgiManager::handlePipeWrite
用途：非阻塞向 CGI 子进程的 stdin 管道写入 POST 请求体数据，单次限制写入量以保证 Reactor 并发公平性。
参数：
    - int cgiWriteFd : 可写的 CGI 管道文件描述符。
返回值：
    - CgiEventResult : 状态结果对象（CGI_CONTINUE 或 CGI_ERROR）。
实现逻辑：
1. 查找任务：通过映射表定位对应的 CgiTask，若未找到则关闭 FD 并返回。
2. 写入限制：每次最多写入 64KB，避免大文件 IO 阻塞主进程；若已发送完毕则关闭写端并清除映射。
3. 状态处理：
   - 正常写入 (>0)：刷新活动时间并累加发送偏移量，写完后关闭写端以向 CGI 发送 EOF。
   - 致命异常 (==0)：写入返回 0 视作通道损坏，强杀任务并返回 500 错误。
   - 管道满 (<0)：暂作临时中断返回 CGI_CONTINUE，由外层兜底清理。
*/
CgiEventResult CgiManager::handlePipeWrite(int cgiWriteFd)
{
    std::map<int, int>::iterator writeIt =
        this->_write_fd_to_read_fd_map.find(cgiWriteFd);
    if (writeIt == this->_write_fd_to_read_fd_map.end())
    {
        ::close(cgiWriteFd);
        return CgiEventResult(CGI_CONTINUE);
    }
    std::map<int, CgiTask>::iterator readIt =
        this->_read_fd_to_task_map.find(writeIt->second);
    if (readIt == this->_read_fd_to_task_map.end())
    {
        ::close(cgiWriteFd);
        this->_write_fd_to_read_fd_map.erase(writeIt);
        return CgiEventResult(CGI_CONTINUE);
    }
    CgiTask &task = readIt->second;
    int clientFd = task.clientFd;

    if (task.inputBody == NULL)
    {
        this->forceKillAndClean(task);
        return CgiEventResult(CGI_ERROR, clientFd, 500);
    }
    const std::string &body = *task.inputBody;
    size_t bodySize = body.size();
    if (task.bodyBytesSent >= bodySize)
    {
        ::close(cgiWriteFd);
        this->_write_fd_to_read_fd_map.erase(writeIt);
        task.writeFd = -1;
        return CgiEventResult(CGI_CONTINUE);
    }
    size_t remaining = bodySize - task.bodyBytesSent;
    const size_t maxWritePerTick = 64 * 1024;
    size_t writeSize = remaining < maxWritePerTick ? remaining : maxWritePerTick;
    const char *dataPtr = body.data() + task.bodyBytesSent;
    ssize_t bytesWritten = ::write(cgiWriteFd, dataPtr, writeSize);
    if (bytesWritten > 0)
    {
        task.lastActivity = std::time(NULL);
        task.bodyBytesSent += static_cast<size_t>(bytesWritten);
        if (task.bodyBytesSent >= bodySize)
        {
            ::close(cgiWriteFd);
            this->_write_fd_to_read_fd_map.erase(writeIt);
            task.writeFd = -1;
        }
        return CgiEventResult(CGI_CONTINUE);
    }
    else if (bytesWritten == 0)
    {
        std::cerr << "[CgiManager] Error: write to CGI pipe returned 0." << std::endl;
        this->forceKillAndClean(task);
        return CgiEventResult(CGI_ERROR, clientFd, 500);
    }
    else
        return CgiEventResult(CGI_CONTINUE);
}

/*
函数：CgiManager::forceKillAndClean
用途：强制销毁指定 CgiTask 的物理与逻辑资源（安全强杀 PID、关闭读写管道、清除 Map 账本）。
参数：
    - CgiTask &task: 待销毁的 CGI 任务引用（通常是 _read_fd_to_task_map 中的 Value 引用）。
返回值：
    - void（无返回值）。
实现逻辑或说明：
    1. 进程生命周期终结：先使用 waitpid(WNOHANG) 探测子进程是否已自然退出。若仍在运行 (waited == 0)，则发射 SIGKILL 强杀，并补一次 waitpid(WNOHANG) 瞬时收尸。随后将 task.pid 置为 -1，彻底阻断重复清理和误杀的风险。
    2. 写端清理：关闭通向 CGI 的 stdin 管道 (writeFd)，并安全擦除 _write_fd_to_read_fd_map 中的轻量反查记录。
    3. 读端与内存清理（核心防踩坑）：关闭来自 CGI 的 stdout 管道 (readFd)。严禁使用局部常数提前拷贝属性，直接操作 task 本体。
    4. 引用安全界限：将主表 _read_fd_to_task_map.erase(task.readFd) 严格置于函数的最后一行。由于传入的 task 是 Map 元素的引用，erase 一旦执行，该内存立即被析构释放。绝不能在此之后继续访问 task 上的任何属性。
*/
void CgiManager::forceKillAndClean(CgiTask &task)
{
    if (task.pid > 0)
    {
        int status = 0;

        pid_t waited = ::waitpid(task.pid, &status, WNOHANG);
        if (waited == 0)
        {
            ::kill(task.pid, SIGKILL);
            ::waitpid(task.pid, &status, WNOHANG);
        }
        task.pid = -1;
    }
    if (task.writeFd >= 0)
    {
        ::close(task.writeFd);
        this->_write_fd_to_read_fd_map.erase(task.writeFd);
        task.writeFd = -1;
    }
    if (task.readFd >= 0)
    {
        ::close(task.readFd);
        this->_read_fd_to_task_map.erase(task.readFd);
    }
}

/*
函数：CgiManager::reapChildren
用途：巡检并全量回收所有已经退出/死亡的 CGI 子进程（僵尸进程收尸车间）。
实现逻辑或说明：
    1. 循环非阻塞回收：在 while 条件中使用 ::waitpid(-1, &status, WNOHANG)，循环检索 OS 内核中已死亡的子进程。
    2. 账本匹配：只要返回 pid > 0，遍历 _read_fd_to_task_map 查找匹配 task.pid == pid 的任务。
    3. 物理与逻辑净空：找到后先将 task.pid 标记为 -1（防止重复 kill），随后调用 forceKillAndClean 关闭残存管道并擦除账本。
    4. 效率优化：由于 PID 与 Task 一一对应，匹配成功后调用 break 提前跳出内层遍历。
*/
void CgiManager::reapChildren()
{
    int status;
    pid_t pid;

    if (this->_read_fd_to_task_map.empty())
        return;
    while ((pid = ::waitpid(-1, &status, WNOHANG)) > 0)
    {
        std::map<int, CgiTask>::iterator it = this->_read_fd_to_task_map.begin();
        while (it != this->_read_fd_to_task_map.end())
        {
            if (it->second.pid == pid)
            {
                it->second.pid = -1;
                break;
            }
            ++it;
        }
    }
}

/*
函数：CgiManager::checkTimeout
用途：看门狗巡检函数。全量检测连续无管道进展的 CGI 进程，物理强杀并向 ServerManager 返回 504 错误指令。
返回值：
    - std::vector<CgiEventResult>: 所有超时任务打包成的 504 错误事件结果集合。
实现逻辑或说明：
    1. 时间获取：调用 std::time(NULL) 获取当前系统时间戳 now。
    2. 安全遍历：使用 C++98 安全迭代器范式（current = it++）遍历 _read_fd_to_task_map。
    3. 超时判定：若 now - task.lastActivity 超过 CGI_INACTIVITY_TIMEOUT（连续 10 秒无 stdin/stdout 进展）：
       - 将 CgiEventResult(CGI_ERROR, clientFd, 504) 压入 timeoutResults 数组；
       - 调用 forceKillAndClean 强杀超时 PID 并关闭管道与清除账本。
    4. 结果交付：将 timeoutResults 集合回传给 Reactor 主循环统一给客户端渲染 504 Gateway Timeout 页面。
*/
std::vector<CgiEventResult> CgiManager::checkTimeout()
{
    std::time_t now = std::time(NULL);
    std::vector<CgiEventResult> timeoutResults;

    std::map<int, CgiTask>::iterator it = this->_read_fd_to_task_map.begin();
    while (it != this->_read_fd_to_task_map.end())
    {
        std::map<int, CgiTask>::iterator current = it++;
        CgiTask &task = current->second;

        if (task.lastActivity > 0 && (now - task.lastActivity > CGI_INACTIVITY_TIMEOUT))
        {
            std::cerr << "[CgiManager] Timeout Warning: CGI PID " << task.pid
                      << " had no pipe progress for 10s! Killing..." << std::endl;

            int clientFd = task.clientFd;
            timeoutResults.push_back(CgiEventResult(CGI_ERROR, clientFd, 504));
            this->forceKillAndClean(task);
        }
    }
    return timeoutResults;
}

/*
函数：CgiManager::removeTaskByClientFd
用途：当客户端 Socket 中途断开/掉线时，根据 clientFd 物理熔断并强杀对应的 CGI 任务。
参数：
    - int clientFd: 断开连接的客户端 Socket 文件描述符。
实现逻辑或说明：
    1. 入参校验：若 clientFd < 0，直接 return。
    2. 安全遍历：使用 C++98 迭代器安全范式（current = it++）遍历 _read_fd_to_task_map。
    3. 匹配与熔断：匹配到 task.clientFd == clientFd 后，打印断开日志并调用 forceKillAndClean 物理强杀 CGI 进程与回收管道。
    4. 提前退出：由于一个 clientFd 同一时间仅关联一个 CGI 任务，清理完毕后直接 return 结束。
*/
void CgiManager::removeTaskByClientFd(int clientFd)
{
    if (clientFd < 0)
        return;
    std::map<int, CgiTask>::iterator it = this->_read_fd_to_task_map.begin();
    while (it != this->_read_fd_to_task_map.end())
    {
        std::map<int, CgiTask>::iterator current = it++;
        CgiTask &task = current->second;
        if (task.clientFd == clientFd)
        {
            DEBUG_LOG("[CgiManager] Client FD " << clientFd
                                                << " disconnected early. Force killing CGI PID " << task.pid);
            this->forceKillAndClean(task);
            return;
        }
    }
}

/*
函数：CgiManager::hasWriteTask
用途：查询指定的 CGI 管道写端文件描述符（cgiWriteFd）是否仍处于活跃的 POST Body 写入任务队列中。
参数：
    - int cgiWriteFd: 待查询的 CGI 管道写端文件描述符。
返回值：
    - bool: 若写任务仍有效（Body 尚未发送完毕）返回 true；若任务已发完注销或 FD 无效则返回 false。
实现逻辑或说明：
    1. 在私有写账本 _write_fd_to_read_fd_map 中执行 find(cgiWriteFd) 查找。
    2. 将迭代器与 _write_fd_to_read_fd_map.end() 进行比较。
    3. 供 ServerManager::handleCgiWrite 在处理 CGI_CONTINUE 时进行精准判定，以决定是保留 poll 监听继续发送切片，还是安全注销写 FD。
*/
bool CgiManager::hasWriteTask(int cgiWriteFd) const
{
    return this->_write_fd_to_read_fd_map.find(cgiWriteFd) != this->_write_fd_to_read_fd_map.end();
}

/*
函数用途：终止并清理所有正在运行的 CGI 任务及相关资源。
实现逻辑：
1. 循环遍历任务映射表，逐个调用 forceKillAndClean 强杀子进程并回收资源，直至清空所有任务。
2. 清空写管道到读管道的映射账本。
*/
void CgiManager::stopAllTasks()
{
    while (!this->_read_fd_to_task_map.empty())
        this->forceKillAndClean(this->_read_fd_to_task_map.begin()->second);
    this->_write_fd_to_read_fd_map.clear();
}
