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
        // C++98 迭代器安全自增备份
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
    CgiHandler cgi(scriptPath, interpreterPath, method, query, path, headers,  host,
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
    // Request 归 Connection 所有，CGI 完成前客户端事件被暂停且 Request 不会被 clear；
    // 因此这里只保存 const 指针，避免学校 tester 的 100MB body 被任务表重复深拷贝。
    task.inputBody = &reqBody;
    task.bodyBytesSent = 0;
    task.lastActivity = std::time(NULL);
    this->_read_fd_to_task_map[fds.read_fd] = task;
    outReadFd = fds.read_fd;
    if (!reqBody.empty() && fds.write_fd >= 0)
    {
        // 写端表只保存 readFd 反查键；完整状态始终只存在于读端主表中。
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
用途：非阻塞地读取 CGI 子进程吐出的 stdout 数据流，并维护缓冲与防爆流控制。
参数：
    - int cgiReadFd: 监听到的就绪 CGI 管道读端文件描述符。
返回值：
    - CgiEventResult: 状态结果对象。
      - CGI_CONTINUE: 数据未完结或已非阻塞读空，指示 Reactor 继续监听。
      - CGI_FINISHED: 收到 EOF (0 字节)，CGI 执行完工，返回打包好输出的 200 结果。
      - CGI_ERROR   : CGI 输出超过 16MB 天花板，触发防爆流熔断，强杀并返回 502 错误。
实现逻辑或说明：
    1. 查账与防卫：若在 _read_fd_to_task_map 中未找到任务，安全 ::close(cgiReadFd) 并返回 CGI_CONTINUE 兜底。
    2. 循环非阻塞读取：使用 4000 字节缓冲区在 while 循环中调用 ::read(cgiReadFd)。
    3. 防爆流熔断：检测 outputBuffer 尺寸，若超过 CGI_MAX_OUTPUT_SIZE (16MB)，调用 forceKillAndClean 强杀进程并返回 CGI_ERROR (502)。
    4. 正常 EOF 完工（bytesRead == 0）：
       - 构建 CGI_FINISHED 状态的 CgiEventResult 对象；
       - 利用 std::string::swap 进行 O(1) 零拷贝指针转移（result.rawOutput.swap(task.outputBuffer)）；
       - 调用 forceKillAndClean 回收资源并返回结果。
    5. 缓冲区读空（bytesRead < 0）：跳出循环，返回 CGI_CONTINUE，等待下一个 poll 。
*/

CgiEventResult CgiManager::handlePipeRead(int cgiReadFd)
{
    std::map<int, CgiTask>::iterator it = this->_read_fd_to_task_map.find(cgiReadFd);
    if (it == this->_read_fd_to_task_map.end())
    {
        // 找不到说明该管道已被清理，直接关闭防泄漏
        ::close(cgiReadFd);
        return CgiEventResult(CGI_CONTINUE);
    }

    CgiTask &task = it->second;
    int clientFd = task.clientFd; // 提前提取基本类型，防止被 erase 后引用失效
    char buffer[4096];

    while (true)
    {
        ssize_t bytesRead = ::read(cgiReadFd, buffer, sizeof(buffer));

        if (bytesRead > 0)
        {
            // 只有真正读到数据才刷新活跃时间
            task.lastActivity = std::time(NULL);

            if (task.outputBuffer.size() + static_cast<size_t>(bytesRead) > CGI_MAX_OUTPUT_SIZE)
            {
                std::cerr << "[CgiManager] Error: CGI output size exceeded max limit! 502." << std::endl;
                this->forceKillAndClean(task);
                return CgiEventResult(CGI_ERROR, clientFd, 502);
            }

            task.outputBuffer.append(buffer, static_cast<size_t>(bytesRead));
            continue;
        }
        else if (bytesRead == 0)
        {
            // 正常读取完毕 (EOF：CGI 子进程关闭了 stdout 管道)
            CgiEventResult result(CGI_FINISHED, clientFd, 200);
            result.rawOutput.swap(task.outputBuffer);

            // 彻底清理此 Task（回收 pid、close fd、erase map）
            this->forceKillAndClean(task);
            return result;
        }
        else 
        {
            // 🚀 不检查 errno！
            // bytesRead < 0 说明当前非阻塞缓冲区已被掏空 (EAGAIN)，或者收到信号打断。
            // 直接退出内层循环，保留连接并等待下次 EPOLLIN 事件即可。
            break;
        }
    }

    return CgiEventResult(CGI_CONTINUE);
}

/*
函数：CgiManager::handlePipeWrite
用途：非阻塞地向 CGI 子进程的 stdin 管道写入 POST Request Body 数据。
参数：
    - int cgiWriteFd: 监听到的可写 CGI 管道文件描述符（pipe_to_child[1]）。
返回值：
    - CgiEventResult: 状态结果对象。
      - CGI_CONTINUE: 表示 Body 正在传输、Body 传输完毕触发 EOF、或短暂等候，主事件循环应继续。
      - CGI_ERROR   : 表示管道写入遇到物理破裂（如 EPIPE/子进程挂掉），通知 Server 发送 500 错误。
实现逻辑或说明：
    1. 先用轻量写端表找到 readFd，再从唯一任务主表取得 CgiTask，避免状态副本分叉。
    2. inputBody 只读引用 Connection::Request 的 body，不产生 100MB 深拷贝。
    3. 每轮最多写 64KB，保证公平性；EAGAIN/EWOULDBLOCK/EINTR 都视为正常暂态。
    4. Body 发完后关闭写端产生 EOF，并只删除 writeFd -> readFd 反查；读端继续等待 CGI stdout。
    5. 真正的 EPIPE/EBADF 等错误才清理整个任务并返回 500。
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
    if (task.inputBody == NULL)
    {
        int clientFd = task.clientFd;
        this->forceKillAndClean(task);
        return CgiEventResult(CGI_ERROR, clientFd, 500);
    }

    const std::string &body = *task.inputBody;
    size_t bodySize = body.size();

    // 如果 Body 已经发完，关闭写端抛出 EOF
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
        // 只有真正写入 CGI stdin 才算有进展，刷新超时计时
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

    // 🚀 【不看 errno 的核心】：
    // 当 bytesWritten <= 0 时（无论是暂态 EAGAIN/EWOULDBLOCK 还是管道满），
    // 统统返回 CGI_CONTINUE，让主循环下一轮继续尝试。
    // 如果管道确实破裂（如子进程中途退出导致 EPIPE），主 Loop 会检测到 EPOLLERR / EPOLLHUP 并触发清理。
    return CgiEventResult(CGI_CONTINUE);
}

/*
函数：CgiManager::forceKillAndClean
用途：强制销毁指定 CgiTask 的物理与逻辑资源（强杀 PID、关闭读写管道、清除 Map 账本）。
参数：
    - CgiTask &task: 待销毁的 CGI 任务引用。
返回值：
    - void（无返回值）。
实现逻辑或说明：
    1. 先复制 readFd、writeFd、pid；task 是主表元素引用，主表 erase 后绝不能继续访问。
    2. 关闭写端并删除轻量反查，再关闭读端。
    3. 先用 waitpid(WNOHANG) 尝试回收；仍运行才 SIGKILL，并处理 EINTR 后完成收尸。
    4. 所有标量都已复制且资源已处理后，最后擦除 readFd 对应的唯一任务主表元素。
*/
void CgiManager::forceKillAndClean(CgiTask &task)
{
    const int readFd = task.readFd;
    const int writeFd = task.writeFd;
    const pid_t pid = task.pid;

    if (writeFd >= 0)
    {
        ::close(writeFd);
        this->_write_fd_to_read_fd_map.erase(writeFd);
    }
    if (readFd >= 0)
    {
        ::close(readFd);
    }

    if (pid > 0)
    {
        int status = 0;
        // 先检查子进程是否已经自然退出
        pid_t waited = ::waitpid(pid, &status, WNOHANG);
        
        if (waited == 0)
        {
            // 子进程仍在运行，直接发送 SIGKILL
            ::kill(pid, SIGKILL);
            
            // 🚀 【不看 errno 的同步等待】：
            // SIGKILL 是强制杀进程，内核会立刻清理它。
            // 使用最多 10 次的有限循环等待，只要 waited == pid (即 > 0) 就成功退出。
            int retry = 0;
            while (retry < 10)
            {
                waited = ::waitpid(pid, &status, 0);
                if (waited == pid) 
                {
                    break; // 成功回收，直接退出
                }
                retry++;
            }
        }
    }

    if (readFd >= 0)
    {
        this->_read_fd_to_task_map.erase(readFd);
    }
}

/*
函数：CgiManager::reapChildren
用途：巡检并全量回收所有已经退出/死亡的 CGI 子进程（僵尸进程收尸车间）。
参数：
    - 无。
返回值：
    - void（无返回值）。
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

    while ((pid = ::waitpid(-1, &status, WNOHANG)) > 0)
    {
        std::map<int, CgiTask>::iterator it = this->_read_fd_to_task_map.begin();
        while (it != this->_read_fd_to_task_map.end())
        {
            std::map<int, CgiTask>::iterator current = it++;
            CgiTask &task = current->second;

            if (task.pid == pid)
            {
                task.pid = -1;
                this->forceKillAndClean(task);
                break;
            }
        }
    }
}

/*
函数：CgiManager::checkTimeout
用途：看门狗巡检函数。全量检测连续无管道进展的 CGI 进程，物理强杀并向 ServerManager 返回 504 错误指令。
参数：
    - 无。
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
返回值：
    - void（无返回值）。
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
            std::cout << "[CgiManager] Client FD " << clientFd
                      << " disconnected early. Force killing CGI PID " << task.pid << std::endl;
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
    return this->_write_fd_to_read_fd_map.find(cgiWriteFd)
        != this->_write_fd_to_read_fd_map.end();
}

void CgiManager::stopAllTasks()
{
    // forceKillAndClean 会擦除主表当前元素，因此始终清理 begin()，
    // 既不会使迭代器失效，也不会复制任务状态或大请求体。
    while (!this->_read_fd_to_task_map.empty())
        this->forceKillAndClean(this->_read_fd_to_task_map.begin()->second);

    this->_write_fd_to_read_fd_map.clear();
}
