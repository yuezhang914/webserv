#ifndef CGIHANDLER_HPP
#define CGIHANDLER_HPP

// 💡 物理管道 FD 与 PID 承载包
struct CgiFds
{
    int read_fd;  // 父进程读端 (CGI stdout)
    int write_fd; // 父进程写端 (CGI stdin)
    pid_t pid;    // 子进程 PID

    CgiFds() : read_fd(-1), write_fd(-1), pid(-1) {}
    CgiFds(int r, int w, pid_t p) : read_fd(r), write_fd(w), pid(p) {}
};

class CgiHandler
{
public:
    // 🎯 直接接收解耦后的纯数据（方法、路径、Query、Header 等），不需要包含 Request.hpp！
    CgiHandler(const std::string &script_path,
               const std::string &interpreter_path,
               const std::string &method,
               const std::string &query,
               const std::string &path,
               const std::map<std::string, std::string> &headers,
               const std::string &host = "localhost",
               const std::string &port = "8080",
               const std::string &root = "./www");
    ~CgiHandler();

    CgiFds async_launch();

private:
    std::string _script_path;
    std::string _interpreter_path;

    // 💡 用于构建 RFC 3875 环境变量的数据要素
    std::string _method;
    std::string _query;
    std::string _path;
    std::map<std::string, std::string> _headers;
    std::string _host;
    std::string _port;
    std::string _root;

    bool _setupPipes(int pipe_to_parent[2], int pipe_to_child[2]);
    void _executeChildProcess(int childReadFd, int parentWriteFd);
    char **_buildEnvironment() const;
    void _freeEnvironment(char **env) const;
};

#endif