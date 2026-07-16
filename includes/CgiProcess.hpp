#ifndef CGI_PROCESS_HPP
#define CGI_PROCESS_HPP

#include "CgiEnv.hpp"
#include <string>
#include <sys/types.h>

class CgiProcess {
private:
    std::string _script_path;
    std::string _post_body;

    bool _setupPipes(int parent_to_child[2], int child_to_parent[2]);
    void _executeChild(int parent_to_child[2], int child_to_parent[2], char** envp);
    std::string _executeParent(pid_t pid, int parent_to_child[2], int child_to_parent[2]);

    // 禁用拷贝与赋值
    CgiProcess(const CgiProcess &);
    CgiProcess &operator=(const CgiProcess &);

public:
    CgiProcess(const std::string &script_path, const std::string &post_body);
    ~CgiProcess();

    std::string run(const CgiEnv &env);
};

#endif