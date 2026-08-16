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
用途：非阻塞地读取 CGI 子进程吐出的 stdout 数据流。每次事件仅进行单次读取以保障 Reactor 主线程并发公平性，并维护缓冲与防爆流控制。
参数：
    - int cgiReadFd: 监听到的就绪 CGI 管道读端文件描述符。
返回值：
    - CgiEventResult: 状态结果对象。
      - CGI_CONTINUE: 成功读取数据块或遇到非阻塞中断，指示 Reactor 将控制权收回并继续监听下一次事件。
      - CGI_FINISHED: 收到 EOF (0 字节)，CGI 执行完工，返回打包好输出的 200 结果。
      - CGI_ERROR   : CGI 输出超过 16MB 天花板，触发防爆流熔断，强杀并返回 502 错误。
实现逻辑或说明：
    1. 查账与防卫：若在 _read_fd_to_task_map 中未找到任务，安全 ::close(cgiReadFd) 并返回 CGI_CONTINUE 兜底。
    2. 单次非阻塞读取：剔除 while(true) 循环，契合 Level-Triggered (水平触发) 模型，每次仅用缓冲区调用一次 ::read(cgiReadFd)。避免大文件读取霸占 CPU，拖垮其他连接。
    3. 防爆流熔断：检测 outputBuffer 尺寸，若超过 CGI_MAX_OUTPUT_SIZE (16MB)，调用 forceKillAndClean 强杀进程并返回 CGI_ERROR (502)。
    4. 正常 EOF 完工（bytesRead == 0）：
       - 构建 CGI_FINISHED 状态的 CgiEventResult 对象；
       - 利用 std::string::swap 进行 O(1) 零拷贝指针转移（result.rawOutput.swap(task.outputBuffer)）；
       - 调用 forceKillAndClean 回收资源并返回结果。
    5. 读取异常妥协（bytesRead < 0）：严格遵守 42 sujet 不可使用 errno 的限制，对于 -1 不作区分，直接当作虚假唤醒或中断返回 CGI_CONTINUE。真正的致命故障交由外层 Timeout Manager 通过超时机制兜底清理。
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

    // 🚀 去掉 while(true)，每次事件只发起一次 read
    ssize_t bytesRead = ::read(cgiReadFd, buffer, sizeof(buffer));

    if (bytesRead > 0)
    {
        // 读到数据，更新活跃时间并存入 buffer
        task.lastActivity = std::time(NULL);

        if (task.outputBuffer.size() + static_cast<size_t>(bytesRead) > CGI_MAX_OUTPUT_SIZE)
        {
            std::cerr << "[CgiManager] Error: CGI output size exceeded max limit! 502." << std::endl;
            this->forceKillAndClean(task);
            return CgiEventResult(CGI_ERROR, clientFd, 502);
        }

        task.outputBuffer.append(buffer, static_cast<size_t>(bytesRead));

        // 把控制权交还给主事件循环。如果管道里还有数据，下一次 select 依然会触发这个 fd 的 POLLIN
        return CgiEventResult(CGI_CONTINUE);
    }
    else if (bytesRead == 0)
    {
        // CGI 子进程输出结束，关闭了管道
        CgiEventResult result(CGI_FINISHED, clientFd, 200);
        result.rawOutput.swap(task.outputBuffer);

        this->forceKillAndClean(task);
        return result;
    }
    else
    {
        // bytesRead < 0
        // 在不查 errno 且仅读一次的架构下，-1 可能是偶发的虚假唤醒(Spurious Wakeup)或系统中断。
        // 直接安全返回，什么也不做，让超时管理器(Timeout Manager)兜底即可。
        return CgiEventResult(CGI_CONTINUE);
    }
}

/*
函数：CgiManager::handlePipeWrite
用途：非阻塞地向 CGI 子进程的 stdin 管道写入 POST Request Body 数据。每次事件仅进行单次限定大小的写入，以保证 Reactor 整体并发的公平性。
参数：
    - int cgiWriteFd: 监听到的可写 CGI 管道文件描述符（pipe_to_child[1]）。
返回值：
    - CgiEventResult: 状态结果对象。
      - CGI_CONTINUE: 表示 Body 正在正常传输、Body 传输完毕已关闭写端引发 EOF、或写缓冲区暂满（返回值 < 0），指示主事件循环继续。
      - CGI_ERROR   : 表示遇到致命物理故障（如 write 返回 0 或引用失效），强杀任务并通知 Server 发送 500 错误。
实现逻辑或说明：
    1. 查账与防卫：先用轻量写端表找到 readFd，再从唯一任务主表取得 CgiTask，避免状态副本分叉。
    2. 零拷贝引用：inputBody 只读引用 Connection::Request 的 body，不产生大文件深拷贝。
    3. 公平性控制：每轮最多写 64KB（单次 write），绝不写死循环，保障主进程不被大文件 IO 阻塞。
    4. 严格区分返回值（应对 Eval sheet 检查）：
       - [> 0] 正常写入：刷新活跃时间。Body 发完后关闭写端向 CGI 传入 EOF，仅擦除写端映射，保留读端继续监听 CGI stdout。
       - [== 0] 致命异常：尝试写入却返回 0，意味着对端异常或通道损坏。作为致命错误立即 forceKillAndClean 并返回 500。
       - [< 0]  无 errno 妥协：受限于 sujet 禁用 errno，不作 EPIPE/EAGAIN 区分，统一视为管道满（暂态），返回 CGI_CONTINUE。真正的管道破裂交由主 Loop 的 EPOLLERR 异常事件或 Timeout Manager 兜底清理。
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
    int clientFd = task.clientFd; // 提前提取，应对清理时失效

    if (task.inputBody == NULL)
    {
        this->forceKillAndClean(task);
        return CgiEventResult(CGI_ERROR, clientFd, 500);
    }

    const std::string &body = *task.inputBody;
    size_t bodySize = body.size();

    // 如果 Body 已经发完，关闭写端抛出 EOF（通知 CGI 脚本输入结束）
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

    // 🚀 核心改动：严格区分 >0, ==0, <0
    ssize_t bytesWritten = ::write(cgiWriteFd, dataPtr, writeSize);

    if (bytesWritten > 0)
    {
        // 正常写入，刷新活跃时间并累加进度
        task.lastActivity = std::time(NULL);
        task.bodyBytesSent += static_cast<size_t>(bytesWritten);

        // 如果刚好发完，在此处顺手关闭管道写端
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
        // 异常故障：企图写入 >0 字节数据，系统却返回 0。
        // 在 write/send 中，这往往意味着对端已断开或管道发生无法恢复的异常。
        // 必须按致命错误处理，停止 CGI 任务。
        std::cerr << "[CgiManager] Error: write to CGI pipe returned 0." << std::endl;
        this->forceKillAndClean(task);
        return CgiEventResult(CGI_ERROR, clientFd, 500);
    }
    else
    {
        // bytesWritten < 0
        // 在不能读取 errno 的前提下，将其视为非阻塞缓冲区满 (EAGAIN/EWOULDBLOCK)
        // 暂时放弃写入，让主循环下一轮继续尝试。
        // 注：若发生 EPIPE（如 CGI 脚本中途崩溃），
        // 1. 系统可能触发 SIGPIPE，需确保主程序开头已 signal(SIGPIPE, SIG_IGN)
        // 2. 主循环的 select/poll 会通过异常位报告，或由 TimeoutManager 清理。
        return CgiEventResult(CGI_CONTINUE);
    }
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
    // ==========================================
    // 1. 处理子进程的生命周期（强杀与收尸）
    // ==========================================
    if (task.pid > 0)
    {
        int status = 0;
        // 先检查子进程是否刚好已经自然退出
        pid_t waited = ::waitpid(task.pid, &status, WNOHANG);

        if (waited == 0)
        {
            // waited == 0 说明进程还在运行，直接发射 SIGKILL 强杀
            ::kill(task.pid, SIGKILL);

            // 补一枪 waitpid 收尸。
            // SIGKILL 是内核级信号，立刻生效。这一个 WNOHANG 足够抹除僵尸进程，无需 while(10)
            ::waitpid(task.pid, &status, WNOHANG);
        }
        
        // 彻底切断联系，防止被外层的 reapChildren 或下一次清理重复杀
        task.pid = -1;
    }

    // ==========================================
    // 2. 清理写端（通向 CGI 的 stdin）
    // ==========================================
    if (task.writeFd >= 0)
    {
        ::close(task.writeFd);
        this->_write_fd_to_read_fd_map.erase(task.writeFd);
        task.writeFd = -1;
    }

    // ==========================================
    // 3. 清理读端（来自 CGI 的 stdout）并彻底销毁 Task
    // ==========================================
    if (task.readFd >= 0)
    {
        ::close(task.readFd);
        // 这是 Task 在内存里的真正载体，erase 执行后，传入的 task 引用就会失效（被析构）
        this->_read_fd_to_task_map.erase(task.readFd);
        // 绝对不要在 erase 后再尝试访问 task.readFd 等属性！
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

    // 💡 优化保留：如果没有活跃的 CGI 任务，直接返回
    if (this->_read_fd_to_task_map.empty())
    {
        return;
    }

    // 循环非阻塞回收所有已结束的子进程
    while ((pid = ::waitpid(-1, &status, WNOHANG)) > 0)
    {
        std::map<int, CgiTask>::iterator it = this->_read_fd_to_task_map.begin();
        while (it != this->_read_fd_to_task_map.end())
        {
            if (it->second.pid == pid)
            {
                // 核心改正：只做标记，绝不调用 forceKillAndClean！
                // 将 pid 设为 -1，防止 Timeout 机制后续误杀别的复用 PID
                it->second.pid = -1;

                // 任务的真正清理，必须交给 handlePipeRead 读到 bytesRead == 0 时去处理
                break;
            }
            ++it;
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
    return this->_write_fd_to_read_fd_map.find(cgiWriteFd) != this->_write_fd_to_read_fd_map.end();
}

void CgiManager::stopAllTasks()
{
    // forceKillAndClean 会擦除主表当前元素，因此始终清理 begin()，
    // 既不会使迭代器失效，也不会复制任务状态或大请求体。
    while (!this->_read_fd_to_task_map.empty())
        this->forceKillAndClean(this->_read_fd_to_task_map.begin()->second);

    this->_write_fd_to_read_fd_map.clear();
}
