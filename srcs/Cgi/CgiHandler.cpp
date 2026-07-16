

#include "Webserv.hpp"

// 在 CgiHandler.cpp 内部初始化环境变量账本
void CgiHandler::_buildEnv()
{
    // 1. Method ──► 映射为 REQUEST_METHOD
    this->_env["REQUEST_METHOD"] = this->_request.getMethod(); // "GET" 或 "POST"

    // 2. Path (带参数的 URL) ──► 映射为 QUERY_STRING
    // 如果 URL 是 /cgi-bin/test.py?name=wy
    // 那么我们要提取出 "?" 后面的 "name=wy" 塞进 QUERY_STRING 中
    this->_env["QUERY_STRING"] = this->_request.getQuery();

    // 3. File Name (具体执行的脚本物理路径) ──► 映射为 SCRIPT_FILENAME
    this->_env["SCRIPT_FILENAME"] = this->_script_path; // 比如 "/var/www/cgi-bin/test.py"

    // 4. 追加：对于 POST 请求，脚本需要知道拿多少数据
    if (this->_request.getMethod() == "POST") 
    {
        // 1. 直接用你写好的 getHeader 去查 "Content-Length" 字符串
        std::string len_str;
        if (this->_request.getHeader("Content-Length", len_str))
        {
            this->_env["CONTENT_LENGTH"] = len_str; // 找到了，直接装箱
        }
        else
        {
            this->_env["CONTENT_LENGTH"] = "0";     // 找不到默认为 0
        }

        // 2. 同样，直接用 getHeader 去查 "Content-Type" 
        std::string content_type; 
        if (this->_request.getHeader("Content-Type", content_type))
        {
            this->_env["CONTENT_TYPE"] = content_type;
        }
        else
        {
            this->_env["CONTENT_TYPE"] = ""; 
        }
    }
}

char **CgiHandler::_initEnv() const
{
    // 1. 申请外层指针阵列 (大小为 N + 1)
    char **envp = new char *[this->_env.size() + 1];

    size_t i = 0;
    // 2. 将 C++ 的 map 物理拼接并复制为 "KEY=VALUE" 格式的 C 字符串
    for (std::map<std::string, std::string>::const_iterator it = this->_env.begin();
         it != this->_env.end(); ++it)
    {
        std::string env_entry = it->first + "=" + it->second;

        // 使用 C 库函数 strdup 物理分配并复制这串字符
        envp[i] = strdup(env_entry.c_str());
        i++;
    }

    // 3. 压入黄金哨兵
    envp[i] = NULL;

    return envp;
}