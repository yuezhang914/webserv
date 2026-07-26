#include "Webserv.hpp"

// 💡 1. 默认构造函数
CgiManager::CgiManager()
{
}

// 💡 2. 析构函数：RAII 物理安全保障车间
// 当 CgiManager 随 ServerManager 一起销毁时，自动扫尾强杀所有残余 CGI 进程
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

bool CgiManager::launchTask(int clientFd,
                            const std::string &scriptPath,
                            const std::string &interpreterPath,
                            const std::string &method,
                            const std::string &query,
                            const std::string &path,
                            const std::map<std::string, std::string> &headers,
                            const std::string &reqBody,
                            int &outReadFd,
                            int &outWriteFd)
{
    outReadFd = -1;
    outWriteFd = -1;

    if (clientFd < 0 || scriptPath.empty())
        return false;

    // 💡 实例化你的 CgiHandler，内部自动完成 RFC 3875 环境变量的构建！
    CgiHandler cgi(scriptPath, interpreterPath, method, query, path, headers);
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
        if (fds.write_fd >= 0)
        {
            ::close(fds.write_fd);
            this->_read_fd_to_task_map[fds.read_fd].writeFd = -1;
        }
    }

    return true;
}

CgiEventResult CgiManager::handlePipeRead(int cgiReadFd)
{
    // 1. 在私有账本里找到对应的 CgiTask
    std::map<int, CgiTask>::iterator it = this->_read_fd_to_task_map.find(cgiReadFd);
    if (it == this->_read_fd_to_task_map.end())
    {
        // 极端防卫：未找到任务说明 FD 已被释放，安全关闭防漏
        ::close(cgiReadFd);
        return CgiEventResult(CGI_CONTINUE);
    }

    CgiTask &task = it->second;
    char buffer[4096];

    while (true)
    {
        ssize_t bytesRead = ::read(cgiReadFd, buffer, sizeof(buffer));

        if (bytesRead > 0)
        {
            // 💡 防爆流保护：CGI 吐出的数据量超过天花板阈值（如 10MB），熔断防爆！
            if (task.outputBuffer.size() + static_cast<size_t>(bytesRead) > 10 * 1024 * 1024)
            {
                std::cerr << "[CgiManager] Error: CGI output size exceeded max limit! 502." << std::endl;
                int clientFd = task.clientFd;
                this->forceKillAndClean(task); // 强行 kill 掉 PID 并清理双向管道
                return CgiEventResult(CGI_ERROR, clientFd, 502);
            }

            // 持续往当前 Task 的 outputBuffer 蓄水池追加数据
            task.outputBuffer.append(buffer, static_cast<size_t>(bytesRead));
            continue;
        }
        else if (bytesRead == 0) // 🟢 EOF 正常完工！说明 CGI 子进程跑完并关闭了 stdout 管道
        {
            int clientFd = task.clientFd;

            // 💡 构造完成指令
            CgiEventResult result(CGI_FINISHED, clientFd, 200);

            // 💡 零拷贝优化：通过 swap 快速将 Task 里的数据转移到 result.rawOutput，指针瞬间替换！
            result.rawOutput.swap(task.outputBuffer);

            // 💡 回收该 Task 占用的 PID、管道以及私有 Map 账本
            this->forceKillAndClean(task);

            return result; // 完美完工返回！
        }
        else // bytesRead < 0 (EAGAIN / EWOULDBLOCK)
        {
            // 管道当前缓存区已读空，等待下一个 poll 滴答（Tick）继续读
            break;
        }
    }

    // 尚未读完 EOF，返回 CONTINUE，指示 Reactor 保持监听
    return CgiEventResult(CGI_CONTINUE);
}

CgiEventResult CgiManager::handlePipeWrite(int cgiWriteFd)
{
    // 1. 在写管道账本中查找 CgiTask
    std::map<int, CgiTask>::iterator it = this->_write_fd_to_task_map.find(cgiWriteFd);
    if (it == this->_write_fd_to_task_map.end())
    {
        // 极端防卫：未找到说明写管道已被释放，安全关闭
        ::close(cgiWriteFd);
        return CgiEventResult(CGI_CONTINUE);
    }

    CgiTask &task = it->second;
    const std::string &body = task.inputBody;
    size_t bodySize = body.size();
    size_t sentBytes = task.bodyBytesSent;

    // 💡 2. 防御检查：如果之前已经全部写完，立刻关闭写端并注销账本
    if (sentBytes >= bodySize)
    {
        ::close(cgiWriteFd);
        this->_write_fd_to_task_map.erase(it);
        task.writeFd = -1;
        return CgiEventResult(CGI_CONTINUE); // 读端（stdout）还在继续，所以返回 CONTINUE
    }

    // 💡 3. 计算本次需要写入的切片指针与剩余大小
    const char *dataPtr = body.data() + sentBytes;
    size_t remaining = bodySize - sentBytes;

    // 4. 非阻塞 write
    ssize_t bytesWritten = ::write(cgiWriteFd, dataPtr, remaining);

    if (bytesWritten > 0)
    {
        task.bodyBytesSent += static_cast<size_t>(bytesWritten);

        // 🟢 如果本次 write 之后 Body 已经全部送达 CGI stdin
        if (task.bodyBytesSent >= bodySize)
        {
            // 关掉父进程手里的写端管道，向子进程 stdin 发送 EOF 信号！
            ::close(cgiWriteFd);
            this->_write_fd_to_task_map.erase(it);
            task.writeFd = -1;
        }
        return CgiEventResult(CGI_CONTINUE);
    }
    else if (bytesWritten < 0)
    {
        // 如果是 EAGAIN 或 EWOULDBLOCK，说明 OS 管道缓冲区满，等下一个 poll 滴答（Tick）继续写
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            return CgiEventResult(CGI_CONTINUE);
        }

        // ❌ 物理写入报错（如 EPIPE，说明 CGI 子进程崩溃或主动关闭了 stdin）
        std::cerr << "[CgiManager] Error: Failed to write POST body to CGI stdin! 500." << std::endl;
        int clientFd = task.clientFd;

        // 强行 kill 进程并清理私有账本
        this->forceKillAndClean(task);
        return CgiEventResult(CGI_ERROR, clientFd, 500);
    }

    return CgiEventResult(CGI_CONTINUE);
}

void CgiManager::forceKillAndClean(CgiTask &task)
{
    // 💡 1. 安全关闭读管道 FD 并清理 Map 账本
    if (task.readFd >= 0)
    {
        ::close(task.readFd);
        this->_read_fd_to_task_map.erase(task.readFd);
        task.readFd = -1;
    }

    // 💡 2. 安全关闭写管道 FD 并清理 Map 账本
    if (task.writeFd >= 0)
    {
        ::close(task.writeFd);
        this->_write_fd_to_task_map.erase(task.writeFd);
        task.writeFd = -1;
    }

    // 💡 3. 彻底抹杀子进程并回收僵尸 (Non-blocking reap)
    if (task.pid > 0)
    {
        // 先尝试给子进程发 SIGKILL 强杀
        ::kill(task.pid, SIGKILL);

        // 利用 WNOHANG 立即回收子进程 PCB 资源，绝不阻塞主线程！
        int status;
        ::waitpid(task.pid, &status, WNOHANG);
        task.pid = -1;
    }
}

/*
函数用途：巡检并全量回收所有已经退出/死亡的 CGI 子进程（收尸车间）
实现逻辑：
1. 使用 waitpid(-1, &status, WNOHANG) 循环非阻塞检索。
2. 只要返回的 pid > 0，说明成功从 OS 内核中回收了一个僵尸进程的 PCB 资源。
3. 顺藤摸瓜：在私有 Map 账本中检索该 PID 对应的 CgiTask。
4. 一旦匹配成功，立刻关闭其可能残存的读写管道 FD，并从私有 Map 中抹除记录，完成 100% 物理与逻辑双重净空。
*/
void CgiManager::reapChildren()
{
    int status;
    pid_t pid;

    // 💡 1. 利用 WNOHANG 循环回收所有已退出的子进程，绝不卡死主线程
    while ((pid = ::waitpid(-1, &status, WNOHANG)) > 0)
    {
        // 💡 2. 遍历私有读管道账本，反向同步清理对应的 CgiTask（保持 C++98 迭代器安全）
        std::map<int, CgiTask>::iterator it = this->_read_fd_to_task_map.begin();
        while (it != this->_read_fd_to_task_map.end())
        {
            std::map<int, CgiTask>::iterator current = it++;
            CgiTask &task = current->second;

            if (task.pid == pid)
            {
                // 标记 PID 为已回收，防止 forceKillAndClean 里重复 kill / waitpid
                task.pid = -1;
                this->forceKillAndClean(task);
                break; // 一个 PID 只对应一个 Task，找到后提前跳出
            }
        }
    }
}

std::vector<CgiEventResult> CgiManager::checkTimeouts()
{
    std::time_t now = std::time(NULL);
    std::vector<CgiEventResult> timeoutResults;

    // 💡 1. 遍历私有账本（注意 C++98 的迭代器安全删除范式）
    std::map<int, CgiTask>::iterator it = this->_read_fd_to_task_map.begin();
    while (it != this->_read_fd_to_task_map.end())
    {
        // 提前备份当前迭代器，并让 it 自增，防止在 forceKillAndClean 中 erase 导致迭代器失效崩溃！
        std::map<int, CgiTask>::iterator current = it++;
        CgiTask &task = current->second;

        // 💡 2. 检查运行时间是否超forceKillAndClean(task)过阈值（例如 10 秒）
        if (task.startTime > 0 && (now - task.startTime > 10))
        {
            std::cerr << "[CgiManager] Timeout Warning: CGI PID " << task.pid
                      << " exceeded 10s timeout! Killing..." << std::endl;

            int clientFd = task.clientFd;

            // 打包 504 错误指令返回给 Reactor
            timeoutResults.push_back(CgiEventResult(CGI_ERROR, clientFd, 504));

            // 物理杀死子进程、关闭管道、在私有 map 里抹去记录
            this->forceKillAndClean(task);
        }
    }

    return timeoutResults;
}

/*
函数用途：当客户端 Socket 中途断开/异常销毁时，根据 clientFd 物理熔断并强杀对应的 CGI 任务
实现逻辑：
1. 遍历私有读管道账本 _read_fd_to_task_map。
2. 匹配 task.clientFd == clientFd。
3. 找到后立刻调用 forceKillAndClean(task) 强杀子进程并关闭管道，随即安全退出。
*/
void CgiManager::removeTaskByClientFd(int clientFd)
{
    if (clientFd < 0)
        return;

    // 💡 1. 遍历读管道账本（使用 C++98 安全迭代器范式）
    std::map<int, CgiTask>::iterator it = this->_read_fd_to_task_map.begin();
    while (it != this->_read_fd_to_task_map.end())
    {
        // 提前备份当前迭代器并让 it 先自增，严防 forceKillAndClean 内部 erase 导致迭代器野指针！
        std::map<int, CgiTask>::iterator current = it++;
        CgiTask &task = current->second;

        // 💡 2. 匹配到对应的客户端 Socket FD
        if (task.clientFd == clientFd)
        {
            std::cout << "[CgiManager] Client FD " << clientFd
                      << " disconnected early. Force killing CGI PID " << task.pid << std::endl;

            // 物理 kill PID、close(readFd/writeFd) 并擦除 map 账本
            this->forceKillAndClean(task);

            // 一个 clientFd 同时只能运行一个 CGI 任务，处理完直接退出
            return;
        }
    }
}