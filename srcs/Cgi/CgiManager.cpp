#include "Webserv.hpp"

/*
函数：CgiManager::CgiManager
用途：CgiManager 类的默认构造函数。
参数：- 无。
实现逻辑或说明：
    1. 初始化 CgiManager 实例。
    2. 私有成员变量 _read_fd_to_task_map 与 _write_fd_to_task_map 账本由 C++98 默认构造函数完成空初始化。
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
    4. 最后清空 _read_fd_to_task_map 与 _write_fd_to_task_map 两个账本，保证 0 进程与 0 FD 泄露。
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
    this->_write_fd_to_task_map.clear();
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
    3. 组装 CgiTask：填入 clientFd、readFd、writeFd、pid、inputBody 以及通过 std::time(NULL) 打点的时间戳。
    4. 读账本登记：将 task 注册进 _read_fd_to_task_map[read_fd]，并赋值 outReadFd。
    5. 写账本处理：
       - 若 reqBody 不为空且 fds.write_fd >= 0，注册进 _write_fd_to_task_map 并赋值 outWriteFd；
       - 若无 Body，主动关掉父进程手里的 write_fd，将 task.writeFd 设为 -1，避免无用写 FD 悬挂。
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
    task.inputBody = reqBody;
    task.bodyBytesSent = 0;
    task.startTime = std::time(NULL);
    this->_read_fd_to_task_map[fds.read_fd] = task;
    outReadFd = fds.read_fd;
    if (!reqBody.empty() && fds.write_fd >= 0)
    {
        this->_write_fd_to_task_map[fds.write_fd] = task;
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
    int clientFd = task.clientFd; // 提前提取基本类型，防止 erase 后引用失效
    char buffer[4096];

    while (true)
    {
        ssize_t bytesRead = ::read(cgiReadFd, buffer, sizeof(buffer));
        if (bytesRead > 0)
        {
            if (task.outputBuffer.size() + static_cast<size_t>(bytesRead) > CGI_MAX_OUTPUT_SIZE)
            {
                std::cerr << "[CgiManager] Error: CGI output size exceeded max limit! 502." << std::endl;
                this->forceKillAndClean(task); // 传入 cgiReadFd 而非 task 引用
                return CgiEventResult(CGI_ERROR, clientFd, 502);
            }
            task.outputBuffer.append(buffer, static_cast<size_t>(bytesRead));
            continue;
        }
        else if (bytesRead == 0)
        {
            // 正常读取完毕 (EOF)
            CgiEventResult result(CGI_FINISHED, clientFd, 200);
            result.rawOutput.swap(task.outputBuffer);
            
            // 彻底清理此 Task（包含 pid 回收、close fd、erase map）
            this->forceKillAndClean(task);
            return result;
        }
        else 
        {
            // 非阻塞读取正常结束（没数据了）
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                break;
            }
            // 管道发生真正异常错误 (如 EBADF, EPIPE)
            std::cerr << "[CgiManager] Error reading from CGI pipe fd: " << cgiReadFd << std::endl;
            this->forceKillAndClean(task);
            return CgiEventResult(CGI_ERROR, clientFd, 500);
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
    1. 查账与防卫：若在 _write_fd_to_task_map 中未找到任务，说明管道已释放，安全 close(cgiWriteFd) 并返回 CGI_CONTINUE。
    2. 状态检查：若 task.bodyBytesSent >= bodySize，说明先前已发完，主动关闭管道写端、发送 EOF 信号并从写账本擦除。
    3. 非阻塞切片写入：根据 bodyBytesSent 偏移量计算剩余未发送指针，调用 ::write 尝试发送。
    4. 结果判定（无 errno 依赖）：
       - bytesWritten > 0 : 累加已发送字节数。若发完，物理 close 写端触发 EOF 并擦除写账本，返回 CGI_CONTINUE。
       - bytesWritten == 0: 管道缓冲区满，不做破坏，返回 CGI_CONTINUE 等待下一个 poll Tick。
       - bytesWritten < 0 : 判定为物理管道破裂（如子进程崩溃），调用 forceKillAndClean 强杀进程并返回 CGI_ERROR (500)。
*/
CgiEventResult CgiManager::handlePipeWrite(int cgiWriteFd)
{
    std::map<int, CgiTask>::iterator it = this->_write_fd_to_task_map.find(cgiWriteFd);
    if (it == this->_write_fd_to_task_map.end())
    {
        ::close(cgiWriteFd);
        return CgiEventResult(CGI_CONTINUE);
    }
    CgiTask &task = it->second;
    const std::string &body = task.inputBody;
    size_t bodySize = body.size();
    size_t sentBytes = task.bodyBytesSent;
    if (sentBytes >= bodySize)
    {
        ::close(cgiWriteFd);
        this->_write_fd_to_task_map.erase(it);
        task.writeFd = -1;
        return CgiEventResult(CGI_CONTINUE); // 读端（stdout）还在继续，所以返回 CONTINUE
    }
    const char *dataPtr = body.data() + sentBytes;
    size_t remaining = bodySize - sentBytes;
    ssize_t bytesWritten = ::write(cgiWriteFd, dataPtr, remaining);
    if (bytesWritten > 0)
    {
        task.bodyBytesSent += static_cast<size_t>(bytesWritten);
        if (task.bodyBytesSent >= bodySize)
        {
            ::close(cgiWriteFd);
            this->_write_fd_to_task_map.erase(it);
            task.writeFd = -1;
        }
        return CgiEventResult(CGI_CONTINUE);
    }
    else if (bytesWritten == 0)
    {
        return CgiEventResult(CGI_CONTINUE);
    }
    std::cerr << "[CgiManager] Error: Failed to write POST body to CGI stdin! 500." << std::endl;
    int clientFd = task.clientFd;
    this->forceKillAndClean(task);
    return CgiEventResult(CGI_ERROR, clientFd, 500);
}

/*
函数：CgiManager::forceKillAndClean
用途：强制销毁指定 CgiTask 的物理与逻辑资源（强杀 PID、关闭读写管道、清除 Map 账本）。
参数：
    - CgiTask &task: 待销毁的 CGI 任务引用。
返回值：
    - void（无返回值）。
实现逻辑或说明：
    1. 读管道清理：若 task.readFd >= 0，调用 ::close 并从 _read_fd_to_task_map 中擦除，重置为 -1。
    2. 写管道清理：若 task.writeFd >= 0，调用 ::close 并从 _write_fd_to_task_map 中擦除，重置为 -1。
    3. 子进程强杀与非阻塞收尸：若 task.pid > 0：
       - 发送 ::kill(task.pid, SIGKILL) 强行抹杀子进程；
       - 调用 ::waitpid(task.pid, &status, WNOHANG) 立即回收其 PCB 资源，绝不阻塞主线程，并重置 pid 为 -1。
*/
void CgiManager::forceKillAndClean(CgiTask &task)
{
    if (task.readFd >= 0)
    {
        std::cout << "cleanup readFd = " << task.readFd << std::endl;
        ::close(task.readFd);
        this->_read_fd_to_task_map.erase(task.readFd);
        task.readFd = -1;
    }
    if (task.writeFd >= 0) 
    {
        std::cout << "cleanup writeFd = " << task.writeFd << std::endl;
        ::close(task.writeFd);
        this->_write_fd_to_task_map.erase(task.writeFd);
        task.writeFd = -1;
    }
    if (task.pid > 0)
    {
        ::kill(task.pid, SIGKILL);
        int status;
        ::waitpid(task.pid, &status, 0); 
        task.pid = -1;
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
函数：CgiManager::checkTimeouts
用途：看门狗巡检函数。全量检测运行超时的 CGI 进程，物理强杀并向 ServerManager 返回 504 错误指令。
参数：
    - 无。
返回值：
    - std::vector<CgiEventResult>: 所有超时任务打包成的 504 错误事件结果集合。
实现逻辑或说明：
    1. 时间获取：调用 std::time(NULL) 获取当前系统时间戳 now。
    2. 安全遍历：使用 C++98 安全迭代器范式（current = it++）遍历 _read_fd_to_task_map。
    3. 超时判定：若 now - task.startTime > 10（超过 10 秒看门狗阈值）：
       - 将 CgiEventResult(CGI_ERROR, clientFd, 504) 压入 timeoutResults 数组；
       - 调用 forceKillAndClean 强杀超时 PID 并关闭管道与清除账本。
    4. 结果交付：将 timeoutResults 集合回传给 Reactor 主循环统一给客户端渲染 504 Gateway Timeout 页面。
*/
std::vector<CgiEventResult> CgiManager::checkTimeouts()
{
    std::time_t now = std::time(NULL);
    std::vector<CgiEventResult> timeoutResults;

    std::map<int, CgiTask>::iterator it = this->_read_fd_to_task_map.begin();
    while (it != this->_read_fd_to_task_map.end())
    {
        std::map<int, CgiTask>::iterator current = it++;
        CgiTask &task = current->second;

        if (task.startTime > 0 && (now - task.startTime > 10))
        {
            std::cerr << "[CgiManager] Timeout Warning: CGI PID " << task.pid
                      << " exceeded 10s timeout! Killing..." << std::endl;

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
    1. 在私有写账本 _write_fd_to_task_map 中执行 find(cgiWriteFd) 查找。
    2. 将迭代器与 _write_fd_to_task_map.end() 进行比较。
    3. 供 ServerManager::handleCgiWrite 在处理 CGI_CONTINUE 时进行精准判定，以决定是保留 poll 监听继续发送切片，还是安全注销写 FD。
*/
bool CgiManager::hasWriteTask(int cgiWriteFd) const
{
    return this->_write_fd_to_task_map.find(cgiWriteFd) != this->_write_fd_to_task_map.end();
}

void CgiManager::stopAllTasks()
{
    // 1. 先收集所有需要清理的任务，避免遍历 map 的同时执行 erase 导致迭代器失效
    std::vector<CgiTask> tasksToClean;

    for (std::map<int, CgiTask>::iterator it = _read_fd_to_task_map.begin();
         it != _read_fd_to_task_map.end(); ++it)
    {
        tasksToClean.push_back(it->second);
    }

    // 2. 依次调用你的 forceKillAndClean 进行物理清理
    for (size_t i = 0; i < tasksToClean.size(); ++i)
    {
        this->forceKillAndClean(tasksToClean[i]);
    }

    // 3. 彻底清空所有 Map 映射
    _read_fd_to_task_map.clear();
    _write_fd_to_task_map.clear();
}