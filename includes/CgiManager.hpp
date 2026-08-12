// includes/CgiManager.hpp
#ifndef CGIMANAGER_HPP
#define CGIMANAGER_HPP

// 纯粹的数据任务
struct CgiTask
{
    int clientFd;
    int readFd;
    int writeFd;
    pid_t pid;

    const std::string *inputBody; // 非拥有型指针：引用 Connection::request 中的 POST Data，避免 100MB body 深拷贝
    size_t bodyBytesSent;          // 已喂给 CGI stdin 的字节数
    std::string outputBuffer;      // CGI 产出的原始 Response 字节流
    std::time_t lastActivity;      // 最近一次成功读/写 CGI 管道的时间；用于检测“无进展”超时

    CgiTask()
        : clientFd(-1), readFd(-1), writeFd(-1), pid(-1),
          inputBody(NULL), bodyBytesSent(0), lastActivity(0) {}
};

// CGI 事件响应结果
enum CgiStatus
{
    CGI_CONTINUE,
    CGI_FINISHED,
    CGI_ERROR
};

struct CgiEventResult
{
    CgiStatus status;
    int clientFd;
    int statusCode;        // 500, 502, 504
    std::string rawOutput; // 拿到的完整 CGI 原始输出

    CgiEventResult(CgiStatus s = CGI_CONTINUE, int cFd = -1, int code = 200, const std::string &out = "")
        : status(s), clientFd(cFd), statusCode(code), rawOutput(out) {}
};

class CgiManager
{
public:
    CgiManager();
    ~CgiManager();

    // 100% 纯粹接口：只接收 clientFd、脚本路径、环境变量数组、Body 字符串
   bool launchTask(
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
    int &outWriteFd);

    CgiEventResult handlePipeRead(int cgiReadFd);
    CgiEventResult handlePipeWrite(int cgiWriteFd);

    std::vector<CgiEventResult> checkTimeout(); // 巡检超时
    void reapChildren();                         // 非阻塞回收 PID
    void removeTaskByClientFd(int clientFd);     // 当客户端异常断开时调用，物理销毁任务与杀掉 PID
    bool hasWriteTask(int cgiWriteFd) const;
    void stopAllTasks();

private:
    std::map<int, CgiTask> _read_fd_to_task_map;       // 唯一拥有任务状态：cgiReadFd -> CgiTask
    std::map<int, int> _write_fd_to_read_fd_map;       // 轻量反查：cgiWriteFd -> cgiReadFd，绝不复制大 body

    void forceKillAndClean(CgiTask &task);
};

#endif