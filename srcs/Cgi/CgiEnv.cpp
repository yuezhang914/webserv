#include "Webserv.hpp"

/*
函数：CgiEnv带参数的构造函数
用途：实体化一个CgiEnv 类
参数：request 是 解析出的request 对象， script_path是解析出的cgi文件路径
实现逻辑： 通过类内部的函数_buildMap， 调取request的内容， 并把相关的环境变量用map的格式存到_envMap， 然后通过函数_convertToCStyle,
  把_envMap转换成数组的形式存到_envp中， 供以后的execve调用。 
*/
CgiEnv::CgiEnv(const Request &request, const std::string &script_path) : _envp(NULL)
{
    _buildMap(request, script_path);
    _convertToCStyle();
}


/*
函数：CgiEnv析构函数
用途：消除一个CgiEnv 对象
实现逻辑： 在退出实现范畴时， 自动销毁对象， 不造成内存泄漏 
*/
CgiEnv::~CgiEnv()
{
    _clear();
}

/*
函数：CgiEnv::getEnvp
用途：获取CgiEnv对象的私有变量_envp
*/
char **CgiEnv::getEnvp() const
{
    return _envp;
}

/*
函数：CgiEnv::_buildMap
用途：通过request 的信息建立map 数据__envMap
参数：request 是 解析出的request 对象， script_path是解析出的cgi文件路径
实现逻辑： 通过request的getter, 获得相关的环境变量， 并存到_envMap中
*/
void CgiEnv::_buildMap(const Request &request, const std::string &script_path)
{
    _envMap["REQUEST_METHOD"] = request.getMethod();
    _envMap["QUERY_STRING"] = request.getQuery();
    _envMap["SCRIPT_FILENAME"] = script_path;

    if (request.getMethod() == "POST")
    {
        std::string len_str;
        if (request.getHeader("Content-Length", len_str))
            _envMap["CONTENT_LENGTH"] = len_str;
        else
            _envMap["CONTENT_LENGTH"] = "0";
        std::string content_type;
        if (request.getHeader("Content-Type", content_type))
            _envMap["CONTENT_TYPE"] = content_type;
        else
            _envMap["CONTENT_TYPE"] = "";
    }
}

/*
函数：CgiEnv::_convertToCStyle
用途：把_envMap 转换成可以用于execve的C类型的数列
实现逻辑：分配地质给数列， 把_envMap中的每一项转变成key=value的形式， 拷贝并存到_envp中， 最后用NULL 作为结束
*/
void CgiEnv::_convertToCStyle()
{
    _envp = new char *[_envMap.size() + 1];
    size_t i = 0;
    for (std::map<std::string, std::string>::const_iterator it = _envMap.begin();
         it != _envMap.end(); ++it)
    {
        std::string env_entry = it->first + "=" + it->second;
        _envp[i] = strdup(env_entry.c_str());
        i++;
    }
    _envp[i] = NULL;
}

/*
函数：CgiEnv::_clear
用途：删除_envp数列及其所占用的内存，重设_envp为NULL，以免出现野指针
实现逻辑：遍历_envp的每一项， 用free释放， 最后释放整个数列， 并重置为NULL
*/
void CgiEnv::_clear()
{
    if (_envp != NULL)
    {
        for (size_t i = 0; _envp[i] != NULL; ++i)
        {
            free(_envp[i]);
        }
        delete[] _envp;
        _envp = NULL;
    }
}