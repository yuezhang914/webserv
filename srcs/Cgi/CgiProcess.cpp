#include "CgiProcess.hpp"
#include <iostream>
#include <unistd.h>
#include <sys/wait.h>
#include <cstring>
#include <cerrno>

CgiProcess::CgiProcess(const std::string &script_path, const std::string &post_body)
    : _script_path(script_path), _post_body(post_body) {}

CgiProcess::~CgiProcess() {}

bool CgiProcess::_setupPipes(int parent_to_child[2], int child_to_parent[2]) {
    if (pipe(parent_to_child) < 0) {
        std::cerr << "[CGI Process Error] Failed to create input pipe." << std::endl;
        return false;
    }
    if (pipe(child_to_parent) < 0) {
        std::cerr << "[CGI Process Error] Failed to create output pipe." << std::endl;
        close(parent_to_child[0]);
        close(parent_to_child[1]);
        return false;
    }
    return true;
}

void CgiProcess::_executeChild(int parent_to_child[2], int child_to_parent[2], char** envp) {
    // 重定向标准输入输出，并绑定标准错误，让崩溃信息能写进管道
    dup2(parent_to_child[0], STDIN_FILENO);
    dup2(child_to_parent[1], STDOUT_FILENO);
    dup2(child_to_parent[1], STDERR_FILENO);

    close(parent_to_child[0]);
    close(parent_to_child[1]);
    close(child_to_parent[0]);
    close(child_to_parent[1]);

    char *args[] = {
        const_cast<char *>(_script_path.c_str()),
        NULL
    };

    execve(args[0], args, envp);

    // 如果执行到这一行，说明 execve 失败
    std::cerr << "\n[🚨 CGI CHILD EMERGENCY] execve failed!" << std::endl;
    std::cerr << "-> Executable path tried: [" << args[0] << "]" << std::endl;
    std::cerr << "-> System Reason (errno): " << strerror(errno) << " (code: " << errno << ")\n" << std::endl;
    _exit(127);
}

std::string CgiProcess::_executeParent(pid_t pid, int parent_to_child[2], int child_to_parent[2]) {
    close(parent_to_child[0]);
    close(child_to_parent[1]);

    // 非阻塞分批写入 POST body 数据
    if (!_post_body.empty()) {
        size_t total_written = 0;
        while (total_written < _post_body.size()) {
            ssize_t bytes = ::write(parent_to_child[1], _post_body.data() + total_written, _post_body.size() - total_written);
            if (bytes <= 0) {
                break;
            }
            total_written += bytes;
        }
    }
    // 必须在此处掐断写端以发送 EOF，否则子进程的 read 永远在死等
    close(parent_to_child[1]);

    // 读取子进程吐出的结果
    std::string response_body;
    char buffer[4096];
    while (true) {
        ssize_t bytes_read = ::read(child_to_parent[0], buffer, sizeof(buffer));
        if (bytes_read > 0) {
            response_body.append(buffer, bytes_read);
        } else if (bytes_read == 0) {
            break;
        } else {
            break;
        }
    }
    close(child_to_parent[0]);

    // 等待子进程，释放系统僵尸资源
    int status;
    waitpid(pid, &status, 0);

    if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
        std::cerr << "[CGI Process Warning] CGI script exited with error code: "
                  << WEXITSTATUS(status) << std::endl;
    }

    return response_body;
}

std::string CgiProcess::run(const CgiEnv &env) {
    int parent_to_child[2];
    int child_to_parent[2];

    if (!_setupPipes(parent_to_child, child_to_parent)) {
        return "";
    }

    pid_t pid = fork();
    if (pid < 0) {
        std::cerr << "[CGI Process Error] Fork failed." << std::endl;
        close(parent_to_child[0]);
        close(parent_to_child[1]);
        close(child_to_parent[0]);
        close(child_to_parent[1]);
        return "";
    }

    if (pid == 0) {
        _executeChild(parent_to_child, child_to_parent, env.getEnvp());
    }

    return _executeParent(pid, parent_to_child, child_to_parent);
}