#include "Webserv.hpp"

// Creates a new CgiHandler object.
CgiHandler::CgiHandler(const std::string &script_path, const std::string &interpreter_path, const std::string &method, const std::string &query, const std::string &path, const std::map<std::string, std::string> &headers, const std::string &host, const std::string &port, const std::string &root) : _script_path(script_path), _interpreter_path(interpreter_path), _method(method), _query(query), _path(path), _headers(headers), _host(host), _port(port), _root(root)
{
}

// Cleans up this object and its owned resources.
CgiHandler::~CgiHandler()
{
}

// Builds environment.
char **CgiHandler::_buildEnvironment() const
{
    std::map<std::string, std::string> envMap;
    std::string scriptName = _path;
    std::string pathInfo = "";
    for (std::map<std::string, std::string>::const_iterator it = _headers.begin(); it != _headers.end(); ++it)
    {
        std::string lowerKey = it->first;
        for (size_t k = 0; k < lowerKey.size(); ++k)
            lowerKey[k] = static_cast<char>(std::tolower(lowerKey[k]));
        if (lowerKey == "x-internal-cgi-script-name")
            scriptName = it->second;
        else if (lowerKey == "x-internal-cgi-path-info")
            pathInfo = it->second;
    }
    envMap["GATEWAY_INTERFACE"] = "CGI/1.1";
    envMap["SERVER_PROTOCOL"] = "HTTP/1.1";
    envMap["REQUEST_METHOD"] = _method;
    envMap["QUERY_STRING"] = _query;
    envMap["SCRIPT_NAME"] = scriptName;
    envMap["SCRIPT_FILENAME"] = _script_path;
    if (!pathInfo.empty())
    {
        envMap["PATH_INFO"] = pathInfo;
        envMap["PATH_TRANSLATED"] = _root + pathInfo;
    }
    else
    {
        envMap["PATH_INFO"] = _path;
        envMap["PATH_TRANSLATED"] = _script_path;
    }
    envMap["SERVER_NAME"] = _host;
    envMap["SERVER_PORT"] = _port;
    envMap["DOCUMENT_ROOT"] = _root;
    envMap["REQUEST_URI"] = _path;
    envMap["SCRIPT_URL"] = scriptName;
    const char *sysPath = std::getenv("PATH");
    if (sysPath != NULL)
        envMap["PATH"] = sysPath;
    else envMap["PATH"] = "/usr/local/bin:/usr/bin:/bin";
    for (std::map<std::string, std::string>::const_iterator it = _headers.begin(); it != _headers.end(); ++it)
    {
        std::string key = it->first;
        std::string val = it->second;
        std::string lowerKey = key;
        for (size_t k = 0; k < lowerKey.size(); ++k)
            lowerKey[k] = static_cast<char>(std::tolower(lowerKey[k]));
        if (lowerKey == "x-internal-cgi-script-name" || lowerKey == "x-internal-cgi-path-info" || lowerKey == "x-internal-cgi-path" || lowerKey == "x-internal-cgi-interpreter" || lowerKey == "x-internal-cgi-document-root")
            continue;
        if (lowerKey == "content-type")
            envMap["CONTENT_TYPE"] = val;
        else if (lowerKey == "content-length")
            envMap["CONTENT_LENGTH"] = val;
        else
        {
            std::string envKey = "HTTP_";
            for (size_t k = 0; k < key.size(); ++k)
            {
                if (key[k] == '-')
                    envKey += '_';
                else envKey += static_cast<char>(std::toupper(key[k]));
            }
            envMap[envKey] = val;
        }
    }
    char **env = static_cast<char **>(std::malloc(sizeof(char *) * (envMap.size() + 1)));
    if (!env)
        return NULL;
    size_t i = 0;
    for (std::map<std::string, std::string>::const_iterator it = envMap.begin(); it != envMap.end(); ++it)
    {
        std::string envStr = it->first + "=" + it->second;
        env[i] = static_cast<char *>(std::malloc(envStr.size() + 1));
        if (env[i])
            std::strcpy(env[i], envStr.c_str());
        ++i;
    }
    env[i] = NULL;
    return env;
}

// Frees the CGI environment array.
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

// Returns the directory part of a path.
static std::string directoryName(const std::string &path)
{
    size_t pos = path.find_last_of('/');
    if (pos == std::string::npos)
        return ".";
    if (pos == 0)
        return "/";
    return path.substr(0, pos);
}

// Returns the file name part of a path.
static std::string baseName(const std::string &path)
{
    size_t pos = path.find_last_of('/');
    if (pos == std::string::npos)
        return path;
    return path.substr(pos + 1);
}

// Sets a parent CGI pipe to non-blocking mode.
static bool configureParentPipeFd(int fd)
{
    if (fcntl(fd, F_SETFL, O_NONBLOCK) < 0)
        return false;
    return true;
}

// Sets up pipes.
bool CgiHandler::_setupPipes(int pipe_to_parent[2], int pipe_to_child[2])
{
    if (pipe(pipe_to_parent) < 0)
    {
        std::cerr << "[CGI] Error: pipe_to_parent failed." << std::endl;
        return false;
    }
    if (pipe(pipe_to_child) < 0)
    {
        std::cerr << "[CGI] Error: pipe_to_child failed." << std::endl;
        close(pipe_to_parent[0]);
        close(pipe_to_parent[1]);
        return false;
    }
    if (!configureParentPipeFd(pipe_to_parent[0]) || !configureParentPipeFd(pipe_to_child[1]))
    {
        std::cerr << "[CGI] Error: configureParentPipeFd failed." << std::endl;
        close(pipe_to_parent[0]);
        close(pipe_to_parent[1]);
        close(pipe_to_child[0]);
        close(pipe_to_child[1]);
        return false;
    }
    return true;
}

// Runs child process.
void CgiHandler::_executeChildProcess(int childReadFd, int parentWriteFd)
{
    if (childReadFd != STDIN_FILENO)
    {
        if (dup2(childReadFd, STDIN_FILENO) < 0)
        {
            close(childReadFd);
            close(parentWriteFd);
            ::exit(127);
        }
        close(childReadFd);
    }
    if (parentWriteFd != STDOUT_FILENO)
    {
        if (dup2(parentWriteFd, STDOUT_FILENO) < 0)
        {
            ::exit(127);
        }
        close(parentWriteFd);
    }
    std::string scriptDirectory = directoryName(_script_path);
    std::string scriptName = baseName(_script_path);
    if (chdir(scriptDirectory.c_str()) != 0)
        ::exit(127);
    char **env = _buildEnvironment();
    if (env == NULL)
        ::exit(127);
    for (int i = 3; i < FD_SETSIZE; ++i)
        ::close(i);
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
    std::cerr << "[CGI] execve failed: " << strerror(errno) << std::endl;
    _freeEnvironment(env);
    ::exit(127);
}

// Starts a CGI process and returns its pipe file descriptors.
CgiFds CgiHandler::async_launch()
{
    CgiFds fds;
    int pipe_to_parent[2];
    int pipe_to_child[2];
    if (!_setupPipes(pipe_to_parent, pipe_to_child))
        return fds;
    fds.pid = fork();
    if (fds.pid < 0)
    {
        close(pipe_to_parent[0]);
        close(pipe_to_parent[1]);
        close(pipe_to_child[0]);
        close(pipe_to_child[1]);
        return fds;
    }
    if (fds.pid == 0)
    {
        close(pipe_to_parent[0]);
        close(pipe_to_child[1]);
        this->_executeChildProcess(pipe_to_child[0], pipe_to_parent[1]);
    }
    close(pipe_to_child[0]);
    close(pipe_to_parent[1]);
    fds.read_fd = pipe_to_parent[0];
    fds.write_fd = pipe_to_child[1];
    return fds;
}
