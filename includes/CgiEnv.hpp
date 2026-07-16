#ifndef CGI_ENV_HPP
#define CGI_ENV_HPP

#include "Request.hpp"
#include <map>
#include <string>

class CgiEnv {
private:
    std::map<std::string, std::string> _envMap;
    char**                             _envp;

    void _buildMap(const Request &request, const std::string &script_path);
    void _convertToCStyle();
    void _clear();

    // 禁用拷贝构造和赋值运算符，防止堆内存被双重释放
    CgiEnv(const CgiEnv &);
    CgiEnv &operator=(const CgiEnv &);

public:
    CgiEnv(const Request &request, const std::string &script_path);
    ~CgiEnv();
    char** getEnvp() const;
};

#endif