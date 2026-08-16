#include "Webserv.hpp"

// Creates a new CgiManager object.
CgiManager::CgiManager()
{
}

// Cleans up this object and its owned resources.
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

// Starts one CGI task and stores its pipe information.
bool CgiManager::launchTask(int clientFd, const std::string &scriptPath, const std::string &interpreterPath, const std::string &method, const std::string &query, const std::string &path, const std::map<std::string, std::string> &headers, const std::string &reqBody, const std::string &host, const std::string &port, const std::string &root, int &outReadFd, int &outWriteFd)
{
    outReadFd = -1;
    outWriteFd = -1;
    if (clientFd < 0 || scriptPath.empty())
        return false;
    CgiHandler cgi(scriptPath, interpreterPath, method, query, path, headers, host, port, root);
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

// Reads available output from one CGI pipe.
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
    {
        std::cerr << "[CgiManager] Error: read from CGI pipe failed." << std::endl;
        this->forceKillAndClean(task);
        return CgiEventResult(CGI_ERROR, clientFd, 500);
    }
}

// Writes request body data to one CGI pipe.
CgiEventResult CgiManager::handlePipeWrite(int cgiWriteFd)
{
    std::map<int, int>::iterator writeIt = this->_write_fd_to_read_fd_map.find(cgiWriteFd);
    if (writeIt == this->_write_fd_to_read_fd_map.end())
    {
        ::close(cgiWriteFd);
        return CgiEventResult(CGI_CONTINUE);
    }
    std::map<int, CgiTask>::iterator readIt = this->_read_fd_to_task_map.find(writeIt->second);
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
    {
        std::cerr << "[CgiManager] Error: write to CGI pipe failed." << std::endl;
        this->forceKillAndClean(task);
        return CgiEventResult(CGI_ERROR, clientFd, 500);
    }
}

// Stops one CGI task and cleans its resources.
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

// Collects CGI child processes that have finished.
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

// Checks CGI tasks and stops tasks that took too long.
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
            std::cerr << "[CgiManager] Timeout Warning: CGI PID " << task.pid << " had no pipe progress for 10s! Killing..." << std::endl;
            int clientFd = task.clientFd;
            timeoutResults.push_back(CgiEventResult(CGI_ERROR, clientFd, 504));
            this->forceKillAndClean(task);
        }
    }
    return timeoutResults;
}

// Removes task by client fd.
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
            DEBUG_LOG("[CgiManager] Client FD " << clientFd << " disconnected early. Force killing CGI PID " << task.pid);
            this->forceKillAndClean(task);
            return;
        }
    }
}

// Checks whether write task.
bool CgiManager::hasWriteTask(int cgiWriteFd) const
{
    return this->_write_fd_to_read_fd_map.find(cgiWriteFd) != this->_write_fd_to_read_fd_map.end();
}

// Stops all tasks.
void CgiManager::stopAllTasks()
{
    while (!this->_read_fd_to_task_map.empty())
        this->forceKillAndClean(this->_read_fd_to_task_map.begin()->second);
    this->_write_fd_to_read_fd_map.clear();
}
