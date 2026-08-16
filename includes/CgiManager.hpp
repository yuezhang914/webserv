#ifndef CGIMANAGER_HPP
#define CGIMANAGER_HPP
struct CgiTask
{
    int clientFd;
    int readFd;
    int writeFd;
    pid_t pid;
    const std::string *inputBody;
    size_t bodyBytesSent;
    std::string outputBuffer;
    std::time_t lastActivity;
    // Creates a new CgiTask object.
    CgiTask() : clientFd(-1), readFd(-1), writeFd(-1), pid(-1), inputBody(NULL), bodyBytesSent(0), lastActivity(0)
    {
    }
};
enum CgiStatus
{
    CGI_CONTINUE, CGI_FINISHED, CGI_ERROR
};
struct CgiEventResult
{
    CgiStatus status;
    int clientFd;
    int statusCode;
    std::string rawOutput;
    // Creates a new CgiEventResult object.
    CgiEventResult(CgiStatus s = CGI_CONTINUE, int cFd = -1, int code = 200, const std::string &out = "") : status(s), clientFd(cFd), statusCode(code), rawOutput(out)
    {
    }
};
class CgiManager
{
public:
    // Creates a new CgiManager object.
    CgiManager();
    // Cleans up this object and its owned resources.
    ~CgiManager();
    // Starts one CGI task and stores its pipe information.
    bool launchTask(int clientFd, const std::string &scriptPath, const std::string &interpreterPath, const std::string &method, const std::string &query, const std::string &path, const std::map<std::string, std::string> &headers, const std::string &reqBody, const std::string &host, const std::string &port, const std::string &root, int &outReadFd, int &outWriteFd);
    // Reads available output from one CGI pipe.
    CgiEventResult handlePipeRead(int cgiReadFd);
    // Writes request body data to one CGI pipe.
    CgiEventResult handlePipeWrite(int cgiWriteFd);
    // Checks CGI tasks and stops tasks that took too long.
    std::vector<CgiEventResult> checkTimeout();
    // Collects CGI child processes that have finished.
    void reapChildren();
    // Removes task by client fd.
    void removeTaskByClientFd(int clientFd);
    // Checks whether write task.
    bool hasWriteTask(int cgiWriteFd) const;
    // Stops all tasks.
    void stopAllTasks();
private:
    std::map<int, CgiTask> _read_fd_to_task_map;
    std::map<int, int> _write_fd_to_read_fd_map;
    // Stops one CGI task and cleans its resources.
    void forceKillAndClean(CgiTask &task);
};
#endif
