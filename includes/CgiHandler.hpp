#ifndef CGI_HANDLER_HPP
#define CGI_HANDLER_HPP

#include "Webserv.hpp"
#include <string>


// 保持你原有的 Request 前置声明，防循环包含
class Request;

struct CgiFds
{
    int read_fd;  // 主进程从这里读取 CGI 的 stdout
    int write_fd; // 主进程通过这里向 CGI 的 stdin 喂 POST body
    pid_t pid;    // 子进程 ID，用于 WNOHANG 收尸

    CgiFds() : read_fd(-1), write_fd(-1), pid(-1) {}
};

class CgiHandler
{
private:
    const Request &_request;
    std::string _script_path;

    // 🔒 内部工业私密车间：负责帮子进程构建 execve 需要的物理环境变量矩阵
    char **_buildEnvironment() const;
    void _freeEnvironment(char **env) const;

public:
    CgiHandler(const Request &request, const std::string &script_path);
    ~CgiHandler();

    // 🚀【新增异步核心】：只孵化进程和管道，瞬间返回物理凭证，绝不阻塞！
    CgiFds async_launch();

    // 💡 保留你原有的同步接口（可选，用于兼容你之前的旧测试流）
    std::string execute();

    // 🎨 报文总装厂：把原始输出包装成满血 HTTP 回包
    std::string buildHttpResponse(const std::string &cgi_output) const;
};

#endif