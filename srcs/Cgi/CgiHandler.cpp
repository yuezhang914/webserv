#include "Webserv.hpp"

CgiHandler::CgiHandler(const std::string &script_path,
                       const std::string &interpreter_path,
                       const std::string &method,
                       const std::string &query,
                       const std::string &path,
                       const std::map<std::string, std::string> &headers,
                       const std::string &host,
                       const std::string &port,
                       const std::string &root)
    : _script_path(script_path),
      _interpreter_path(interpreter_path),
      _method(method),
      _query(query),
      _path(path),
      _headers(headers),
      _host(host),
      _port(port),
      _root(root) {}

CgiHandler::~CgiHandler() {}

static std::string directoryName(const std::string &path)
{
    size_t pos = path.find_last_of('/');
    if (pos == std::string::npos)
        return ".";
    if (pos == 0)
        return "/";
    return path.substr(0, pos);
}

static std::string baseName(const std::string &path)
{
    size_t pos = path.find_last_of('/');
    if (pos == std::string::npos)
        return path;
    return path.substr(pos + 1);
}

static bool configureParentPipeFd(int fd)
{
    int statusFlags = fcntl(fd, F_GETFL, 0);
    if (statusFlags < 0)
        return false;

    if (fcntl(fd, F_SETFL, statusFlags | O_NONBLOCK) < 0)
        return false;

    int descriptorFlags = fcntl(fd, F_GETFD, 0);
    if (descriptorFlags < 0)
        return false;

    if (fcntl(fd, F_SETFD, descriptorFlags | FD_CLOEXEC) < 0)
        return false;

    return true;
}

CgiFds CgiHandler::async_launch()
{
    CgiFds fds;

    int pipe_to_parent[2];
    int pipe_to_child[2];

    if (pipe(pipe_to_parent) < 0)
    {
        std::cerr << "[CGI] Error: pipe_to_parent failed." << std::endl;
        return fds;
    }
    if (pipe(pipe_to_child) < 0)
    {
        std::cerr << "[CGI] Error: pipe_to_child failed." << std::endl;
        close(pipe_to_parent[0]);
        close(pipe_to_parent[1]);
        return fds;
    }

    if (!configureParentPipeFd(pipe_to_parent[0]) || !configureParentPipeFd(pipe_to_child[1]))
    {
        std::cerr << "[CGI] Error: configureParentPipeFd failed." << std::endl;
        close(pipe_to_parent[0]);
        close(pipe_to_parent[1]);
        close(pipe_to_child[0]);
        close(pipe_to_child[1]);
        return fds;
    }

    fds.pid = fork();
    if (fds.pid < 0)
    {
        close(pipe_to_parent[0]);
        close(pipe_to_parent[1]);
        close(pipe_to_child[0]);
        close(pipe_to_child[1]);
        return fds;
    }

    if (fds.pid == 0) // ================== 子进程空间 ==================
    {
        close(pipe_to_parent[0]);
        close(pipe_to_child[1]);

        if (pipe_to_child[0] != STDIN_FILENO)
        {
            if (dup2(pipe_to_child[0], STDIN_FILENO) < 0)
            {
                close(pipe_to_child[0]);
                close(pipe_to_parent[1]);
                exit(127);
            }
            close(pipe_to_child[0]);
        }

        if (pipe_to_parent[1] != STDOUT_FILENO)
        {
            if (dup2(pipe_to_parent[1], STDOUT_FILENO) < 0)
            {
                close(pipe_to_parent[1]);
                exit(127);
            }
            close(pipe_to_parent[1]);
        }

        std::string scriptDirectory = directoryName(_script_path);
        std::string scriptName = baseName(_script_path);

        if (chdir(scriptDirectory.c_str()) != 0)
        {
            exit(127);
        }

        char **env = _buildEnvironment();
        if (env == NULL)
        {
            exit(127);
        }

        char *args[3];

        if (!_interpreter_path.empty())
        {
            args[0] = const_cast<char *>(_interpreter_path.c_str());
            args[1] = const_cast<char *>(scriptName.c_str());
            args[2] = NULL;

            ::execve(args[0], args, env);
        }
        else
        {
            std::string executable = "./" + scriptName;

            args[0] = const_cast<char *>(executable.c_str());
            args[1] = NULL;

            ::execve(args[0], args, env);
        }

        _freeEnvironment(env);
        exit(127);
    }

    // ================== 父进程大管家车间 ==================
    close(pipe_to_child[0]);
    close(pipe_to_parent[1]);

    fds.read_fd = pipe_to_parent[0];
    fds.write_fd = pipe_to_child[1];

    return fds;
}

/*
内部函数：构建符合 RFC 3875 标准的 CGI 环境变量矩阵
*/
char **CgiHandler::_buildEnvironment() const
{
    std::map<std::string, std::string> envMap;

    envMap["GATEWAY_INTERFACE"] = "CGI/1.1";
    envMap["SERVER_PROTOCOL"]    = "HTTP/1.1";
    envMap["REQUEST_METHOD"]     = _method;
    envMap["QUERY_STRING"]       = _query;

    envMap["SCRIPT_NAME"]     = _path;
    envMap["SCRIPT_FILENAME"] = _script_path;
    envMap["PATH_INFO"]       = "";

    envMap["SERVER_NAME"]   = _host;
    envMap["SERVER_PORT"]   = _port;
    envMap["DOCUMENT_ROOT"] = _root;

    // 自动透传所有转为小写/大写的 HTTP Header (根据 RFC 规范)
    for (std::map<std::string, std::string>::const_iterator it = _headers.begin();
         it != _headers.end(); ++it)
    {
        std::string key = it->first;
        std::string val = it->second;

        if (key == "content-type" || key == "Content-Type")
        {
            envMap["CONTENT_TYPE"] = val;
        }
        else if (key == "content-length" || key == "Content-Length")
        {
            envMap["CONTENT_LENGTH"] = val;
        }
        else
        {
            // 转换为 HTTP_ 开头并全大写
            std::string envKey = "HTTP_";
            for (size_t k = 0; k < key.size(); ++k)
            {
                if (key[k] == '-')
                    envKey += '_';
                else
                    envKey += static_cast<char>(std::toupper(key[k]));
            }
            envMap[envKey] = val;
        }
    }

    // 转换为 char** 矩阵 (与你原实现内存管理一致，采用 C 风格堆空间开辟)
    char **env = static_cast<char **>(std::malloc(sizeof(char *) * (envMap.size() + 1)));
    if (!env)
        return NULL;

    size_t i = 0;
    for (std::map<std::string, std::string>::const_iterator it = envMap.begin();
         it != envMap.end(); ++it)
    {
        std::string envStr = it->first + "=" + it->second;
        env[i] = static_cast<char *>(std::malloc(envStr.size() + 1));
        if (env[i])
            std::strcpy(env[i], envStr.c_str());
        ++i;
    }
    env[i] = NULL; // 哨兵

    return env;
}

void CgiHandler::_freeEnvironment(char **env) const
{
    if (env == NULL)
        return;

    size_t i = 0;
    while (env[i] != NULL)
    {
        std::free(env[i]);
        ++i;
    }
    std::free(env);
}