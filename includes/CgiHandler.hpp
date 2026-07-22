#ifndef CGI_HANDLER_HPP
#define CGI_HANDLER_HPP

#include "Webserv.hpp"
#include <string>
#include <unistd.h>    // 🟢 补上它，确保 pid_t 在所有平台都能直接被编译器认出来
#include <sys/types.h>

// 前置声明，完美切断头文件互相依赖的因果锁
class Request;

// 🚀【异步并网物理凭证】
struct CgiFds
{
    int read_fd;  // 主进程从这里读取 CGI 的 stdout
    int write_fd; // 主进程通过这里向 CGI 的 stdin 喂 POST body
    pid_t pid;    // 子进程 ID，供大管家非阻塞 waitpid 回收

    CgiFds() : read_fd(-1), write_fd(-1), pid(-1) {}
};

class CgiHandler
{
private:
    const Request &_request;
    std::string   _script_path;
    std::string    _interpreter_path; // ⚙️ 解释器物理路径（如 /usr/bin/python3，允许为空）

    // 🔒 内部工业私密车间：负责帮子进程构建 execve 需要的物理环境变量矩阵与参数
    char **_buildEnvironment() const;
    void   _freeEnvironment(char **env) const;

    // 禁止外部对 CGI 进行无意义的自主克隆，死守单例实体的安全
    CgiHandler(const CgiHandler &other);
    CgiHandler &operator=(const CgiHandler &other);

public:
    CgiHandler(const Request &request, const std::string &script_path, const std::string &interpreter_path);
    ~CgiHandler();

    // 🚀【异步核心大闸】：只开凿管道、fork 孵化进程，瞬间返回物理凭证，绝不阻塞！
    CgiFds async_launch();
};

#endif