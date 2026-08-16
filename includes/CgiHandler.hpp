#ifndef CGIHANDLER_HPP
#define CGIHANDLER_HPP
struct CgiFds
{
    int read_fd;
    int write_fd;
    pid_t pid;
    // Creates a new CgiFds object.
    CgiFds() : read_fd(-1), write_fd(-1), pid(-1)
    {
    }
    // Creates a new CgiFds object.
    CgiFds(int r, int w, pid_t p) : read_fd(r), write_fd(w), pid(p)
    {
    }
};
class CgiHandler
{
public:
    CgiHandler(const std::string &script_path, const std::string &interpreter_path, const std::string &method, const std::string &query, const std::string &path, const std::map<std::string, std::string> &headers, const std::string &host = "localhost", const std::string &port = "8080", const std::string &root = "./www");
    // Cleans up this object and its owned resources.
    ~CgiHandler();
    // Starts a CGI process and returns its pipe file descriptors.
    CgiFds async_launch();
private:
    std::string _script_path;
    std::string _interpreter_path;
    std::string _method;
    std::string _query;
    std::string _path;
    std::map<std::string, std::string> _headers;
    std::string _host;
    std::string _port;
    std::string _root;
    // Sets up pipes.
    bool _setupPipes(int pipe_to_parent[2], int pipe_to_child[2]);
    // Runs child process.
    void _executeChildProcess(int childReadFd, int parentWriteFd);
    // Builds environment.
    char **_buildEnvironment() const;
    // Frees the CGI environment array.
    void _freeEnvironment(char **env) const;
};
#endif
