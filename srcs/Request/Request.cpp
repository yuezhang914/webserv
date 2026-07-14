/*
文件：srcs/Request/Request.cpp
用途：实现 Request 的初始化、重置与只读访问接口。
说明：HTTP 语法、URI 和 body framing 全部由 RequestParser 处理；本文件不读取 socket，也不保存连接策略。
*/
#include "Request.hpp"

/*
函数：Request::toLowerAscii
用途：只转换 HTTP 语法使用的 ASCII 大写字母，结果不受系统 locale 影响。
参数：value 是 header name、Transfer-Encoding 值或其他 HTTP ASCII 字符串。
返回：转换后的小写副本，不修改原字符串。
*/
std::string Request::toLowerAscii(const std::string &value)
{
    std::string result = value;
    size_t i = 0;
    while (i < result.size())
    {
        if (result[i] >= 'A' && result[i] <= 'Z')
            result[i] = static_cast<char>(result[i] - 'A' + 'a');
        ++i;
    }
    return result;
}

/*
构造函数：Request::Request
用途：创建尚未解析请求的空对象。
初始化：string/map 自动为空；_config 为 NULL。
*/
Request::Request()
    : _config(NULL)
{
}

/*
函数：Request::resetForParsing
用途：同一个 Request 对象解析新请求前，清除上一次的全部解析结果。
参数：server 是 RequestParser::parseBuffer() 接收到的非拥有型配置指针。
*/
void Request::resetForParsing(const ServerConfig *server)
{
    _method.clear();
    _uri.clear();
    _path.clear();
    _query.clear();
    _version.clear();
    _headers.clear();
    _body.clear();
    _config = server;
}

/* 函数：getMethod；返回解析后的 method。 */
const std::string &Request::getMethod() const
{
    return _method;
}

/* 函数：getUri；返回 request-line 中未经改写的原始 request-target。 */
const std::string &Request::getUri() const
{
    return _uri;
}

/* 函数：getPath；返回解码并规范化后的安全路径。 */
const std::string &Request::getPath() const
{
    return _path;
}

/* 函数：getQuery；返回原始 query，不包含开头问号。 */
const std::string &Request::getQuery() const
{
    return _query;
}

/* 函数：getVersion；返回解析后的 HTTP version。 */
const std::string &Request::getVersion() const
{
    return _version;
}

/* 函数：getHeaders；返回完整 header map 的 const 引用。 */
const Request::HeaderMap &Request::getHeaders() const
{
    return _headers;
}

/* 函数：getBody；返回二进制安全 body 的 const 引用。 */
const std::string &Request::getBody() const
{
    return _body;
}

/* 函数：getConfig；返回非拥有型 ServerConfig 指针。 */
const ServerConfig *Request::getConfig() const
{
    return _config;
}

/*
函数：Request::getHeader
用途：通过内部统一的小写 key 查找 header，避免调用方直接操作 map。
*/
bool Request::getHeader(const std::string &name, std::string &value) const
{
    HeaderMap::const_iterator it = _headers.find(toLowerAscii(name));
    if (it == _headers.end())
    {
        value.clear();
        return false;
    }
    value = it->second;
    return true;
}