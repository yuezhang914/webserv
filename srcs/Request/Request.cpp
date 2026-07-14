/*
文件：srcs/Request/Request.cpp
用途：实现 Request 类的生命周期、只读访问接口和共享字符串工具。
说明：HTTP 语法解析、URI 安全检查和 body framing 全部封装在 RequestParser.cpp；本文件不读取 socket，也不生成 Response。
*/
#include "Request.hpp"
#include <cctype>

/*
函数：to_lower
用途：把字符串中的英文字母统一转换成小写。
参数来源：HTTP header 名、Transfer-Encoding、Connection、URI 安全检查或其他模块传入的字符串。
返回值：转换后的小写副本，不修改原始参数。
实现逻辑：复制字符串后逐字节转换；先转 unsigned char 再调用 std::tolower，避免有符号 char 导致未定义行为。
*/
std::string to_lower(const std::string &str)
{
    std::string result = str;
    size_t i = 0;
    while (i < result.size())
    {
        unsigned char c = static_cast<unsigned char>(result[i]);
        result[i] = static_cast<char>(std::tolower(c));
        ++i;
    }
    return result;
}

/*
构造函数：Request::Request
用途：创建尚未解析任何 HTTP 报文的空 Request。
初始化：所有 string/map 自动为空；_config 为 NULL；_closeConnection 使用安全默认 true。
*/
Request::Request()
    : _method(), _uri(), _version(), _headers(), _body(),
      _config(NULL), _closeConnection(true)
{
}

/*
拷贝构造函数：Request::Request
用途：创建另一个 Request 的完整副本。
参数来源：src 通常来自测试、容器或后续模块需要保留解析结果时传入。
所有权说明：_config 只是非拥有型指针，复制地址但不复制或接管 ServerConfig。
*/
Request::Request(const Request &src)
    : _method(src._method), _uri(src._uri), _version(src._version),
      _headers(src._headers), _body(src._body), _config(src._config),
      _closeConnection(src._closeConnection)
{
}

/*
赋值运算符：Request::operator=
用途：把 rhs 的解析结果复制到已经存在的 Request。
实现逻辑：先防止自赋值，再复制所有标准容器、字符串、非拥有型 config 指针和连接策略。
返回值：返回当前对象引用，支持连续赋值。
*/
Request &Request::operator=(const Request &rhs)
{
    if (this != &rhs)
    {
        _method = rhs._method;
        _uri = rhs._uri;
        _version = rhs._version;
        _headers = rhs._headers;
        _body = rhs._body;
        _config = rhs._config;
        _closeConnection = rhs._closeConnection;
    }
    return *this;
}

/*
析构函数：Request::~Request
用途：销毁 Request。
资源说明：string 和 map 会自动释放内部内存；_config 由 Config/ServerManager 拥有，因此这里绝不能 delete。
*/
Request::~Request()
{
}

/*
函数：Request::resetForParsing
用途：在解析一个新 request 前，清除旧解析结果并绑定当前 server 上下文。
参数来源：server 来自 RequestParser::parseBuffer() 的第三个参数。
访问限制：本函数是 private，只能由友元 RequestParser 调用。
*/
void Request::resetForParsing(const ServerConfig *server)
{
    _method.clear();
    _uri.clear();
    _version.clear();
    _headers.clear();
    _body.clear();
    _config = server;
    _closeConnection = true;
}

/* 函数：getMethod；返回解析后的 HTTP method 只读引用。 */
const std::string &Request::getMethod() const
{
    return _method;
}

/* 函数：getUri；返回原始 request-target 只读引用。 */
const std::string &Request::getUri() const
{
    return _uri;
}

/* 函数：getVersion；返回解析后的 HTTP version 只读引用。 */
const std::string &Request::getVersion() const
{
    return _version;
}

/* 函数：getHeaders；返回完整 header map 的 const 引用，供只读遍历。 */
const Request::HeaderMap &Request::getHeaders() const
{
    return _headers;
}

/* 函数：getBody；返回二进制安全 body 的只读引用。 */
const std::string &Request::getBody() const
{
    return _body;
}

/* 函数：getConfig；返回当前请求借用的 ServerConfig 指针，不转移所有权。 */
const ServerConfig *Request::getConfig() const
{
    return _config;
}

/*
函数：Request::getHeader
用途：大小写不敏感地读取一个请求头，同时明确区分“字段不存在”和“字段存在但值为空”。
参数：name 是查询名称；value 是输出字符串。
返回值：找到返回 true；未找到返回 false 并清空 value。
*/
bool Request::getHeader(const std::string &name, std::string &value) const
{
    HeaderMap::const_iterator it = _headers.find(to_lower(name));
    if (it == _headers.end())
    {
        value.clear();
        return false;
    }
    value = it->second;
    return true;
}

/* 函数：shouldCloseConnection；返回响应发送完成后是否关闭 client。 */
bool Request::shouldCloseConnection() const
{
    return _closeConnection;
}

/*
函数：setCloseConnection
用途：允许 ServerManager 在解析完成后写入最终 keep-alive/close 策略。
限制：本函数只能修改连接策略，不能篡改已经解析完成的 HTTP 数据。
*/
void Request::setCloseConnection(bool closeConnection)
{
    _closeConnection = closeConnection;
}