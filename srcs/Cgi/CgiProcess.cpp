#include "Webserv.hpp"

/*
函数：CgiProcess带参数的构造函数
用途：实体化一个CgiProcess 类
参数：script_path是解析出的cgi文件路径, post_body是request 传入的Post的请求体body
*/
CgiProcess::CgiProcess(const std::string &script_path, const std::string &post_body)
    : _script_path(script_path), _post_body(post_body) {}

/*
函数：CgiProcess析构函数
用途：删除一个GgiProcess对象
实现逻辑：在对象推出其实现范围时， 自动调用
*/
CgiProcess::~CgiProcess() {}

/*
函数：CgiEnv::_setupPipes
用途：创建两个管道， 一个管道从父进程输入要执行的文件路径及body， 子进程读取这些信息后，运行需要执行的文件，  另一个管道子进程输入运行结果， 父进程读取这个结果
参数：两个管道
实现逻辑：用函数pipe来创建管道。如果创建失败，需要返回false, 成功返回true.
*/
bool CgiProcess::_setupPipes(int parent_to_child[2], int child_to_parent[2])
{
    if (pipe(parent_to_child) < 0)
    {
        std::cerr << "[CGI Process Error] Failed to create input pipe." << std::endl;
        return false;
    }
    if (pipe(child_to_parent) < 0)
    {
        std::cerr << "[CGI Process Error] Failed to create output pipe." << std::endl;
        close(parent_to_child[0]);
        close(parent_to_child[1]);
        return false;
    }
    return true;
}

/*
函数：CgiEnv::_executeChild
用途：运行子进程
参数：两个管道，及环境变量数列
实现逻辑：重定向，并关闭不用的管道口， 把文件路径存入参数args的第一项中， 运行execve函数。如果运行失败，需要用_exit(127)退出。
*/
void CgiProcess::_executeChild(int parent_to_child[2], int child_to_parent[2], char **envp)
{
    dup2(parent_to_child[0], STDIN_FILENO);
    dup2(child_to_parent[1], STDOUT_FILENO);

    close(parent_to_child[0]);
    close(parent_to_child[1]);
    close(child_to_parent[0]);
    close(child_to_parent[1]);

    char *args[] = {
        const_cast<char *>(_script_path.c_str()),
        NULL};

    execve(args[0], args, envp);
    std::cerr << "\n[CGI CHILD EMERGENCY] execve failed!" << std::endl;
    std::cerr << "-> Executable path tried: [" << args[0] << "]" << std::endl;
    std::cerr << "-> System Reason (errno): " << strerror(errno) << " (code: " << errno << ")\n"
              << std::endl;
    _exit(127);
}

/*
函数：CgiEnv::_executeParent
用途：运行父进程
参数：两个管道，及子进程号
实现逻辑：关闭不需要的管道口，通过循环分批把post body 写入管道（parent_to_child),写完后把此管道的输出口关闭。
    同样通过循环把子进程的运行结果分批写入管道（child_to_parent), 写完后把此管道的输入口关闭。 然后等待子进程完毕后返回response结果
*/
std::string CgiProcess::_executeParent(pid_t pid, int parent_to_child[2], int child_to_parent[2])
{
    close(parent_to_child[0]);
    close(child_to_parent[1]);

    // 非阻塞分批写入 POST body 数据
    if (!_post_body.empty())
    {
        size_t total_written = 0;
        while (total_written < _post_body.size())
        {
            ssize_t bytes = ::write(parent_to_child[1], _post_body.data() + total_written, _post_body.size() - total_written);
            if (bytes <= 0)
            {
                break;
            }
            total_written += bytes;
        }
    }
    close(parent_to_child[1]);
    std::string response_body;
    char buffer[4096];
    while (true)
    {
        ssize_t bytes_read = ::read(child_to_parent[0], buffer, sizeof(buffer));
        if (bytes_read > 0)
        {
            response_body.append(buffer, bytes_read);
        }
        else if (bytes_read == 0)
        {
            break;
        }
        else
        {
            break;
        }
    }
    close(child_to_parent[0]);
    int status;
    waitpid(pid, &status, 0);

    if (WIFEXITED(status) && WEXITSTATUS(status) != 0)
    {
        std::cerr << "[CGI Process Warning] CGI script exited with error code: "
                  << WEXITSTATUS(status) << std::endl;
    }

    return response_body;
}

/*
函数：CgiEnv::run
用途：总的CGI 运行过程
参数：CgiEnv 对象
实现逻辑：建立两个管道， 建立子进程， 运行子进程， 运行父进程， 返回response结果
*/
std::string CgiProcess::run(const CgiEnv &env)
{
    int parent_to_child[2];
    int child_to_parent[2];

    if (!_setupPipes(parent_to_child, child_to_parent))
        return "";
    pid_t pid = fork();
    if (pid < 0)
    {
        std::cerr << "[CGI Process Error] Fork failed." << std::endl;
        close(parent_to_child[0]);
        close(parent_to_child[1]);
        close(child_to_parent[0]);
        close(child_to_parent[1]);
        return "";
    }
    if (pid == 0)
    {
        _executeChild(parent_to_child, child_to_parent, env.getEnvp());
    }
    return _executeParent(pid, parent_to_child, child_to_parent);
}