#ifndef CGI_HANDLER_HPP
#define CGI_HANDLER_HPP

#include <string>
#include <map>
#include "Request.hpp" // 引入你的请求类

class CgiHandler {
public:
    // 1. 构造函数：把我们需要解析的 Request 和对应的配置传进来
    CgiHandler(const Request &request, const std::string &script_path);
    ~CgiHandler();

    // 2. 核心对外物理接口：执行 CGI 并返回子进程吐出来的完整 HTTP 响应体/Body
    std::string execute();

private:
    const Request     &_request;     // 绑定当前请求
    std::string        _script_path; // 物理脚本路径（比如 "./cgi-bin/login.py"）
    
    // 留作下一步：存储环境变量的账本
    std::map<std::string, std::string> _env; 

    void _buildEnv();
    // 留作下一步：私有辅助函数，用来把 C++ 的 std::map 转换成 execve 认识的 char** envp
    char **_initEnv() const;

};

#endif