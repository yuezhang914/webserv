/*
文件：srcs/Response/Response.cpp
用途：实现 Response 对象的构造、只读访问、body 修改、连接策略设置与最终 HTTP 字节序列化。
拆分说明：本文件只保留 Response 最基础的对象行为；header 规则、状态规则、错误页和 CGI 解析分别放入对应实现文件。
*/
/*
包含：Response.hpp
用途：取得 Response 类、HeaderMap 和成员函数声明。
*/
#include "Response.hpp"

/*
包含：Request.hpp
用途：构造 Response(request) 时读取 HTTP 版本和 Connection 策略。
*/
#include "Request.hpp"

/*
包含：<sstream>
用途：序列化状态行、headers 和十进制字段到输出缓冲区。
*/
#include <sstream>

/*
函数：Response::Response(bool)
用途：创建一个可立即序列化的 HTTP/1.1 200 OK 空响应。
参数来源：closeConnection 通常由目录/index 辅助函数传入，用来继承原请求的连接关闭策略；默认 false。
变量说明：成员全部在初始化列表中设置；没有额外局部变量。
实现逻辑：
    1. 初始化合法状态行和空 headers/body。
    2. 保存调用方给出的连接策略。
    3. 生成受控 Connection 与 Content-Length headers。
*/
Response::Response(bool closeConnection)
    : _version("HTTP/1.1"), _statusCode(200), _statusMessage("OK"),
      _headers(), _body(), _closeConnection(closeConnection)
{
    updateConnectionHeader();
    updateContentLength();
}

/*
函数：Response::Response(const Request &)
用途：创建与某个 Request 关联的 200 OK 空响应，并继承客户端 Connection: close 请求。
参数来源：request 来自 buildResponse() 或 RequestHandler；已由 RequestParser 填充 headers。
变量说明：无额外局部变量；requestWantsClose() 从 Request 中提取连接 token。
实现逻辑：
    1. 初始化合法状态、空 headers 和空 body。
    2. 调用 requestWantsClose() 得到初始关闭策略。
    3. 同步 Connection 和 Content-Length。
*/
Response::Response(const Request &request)
    : _version("HTTP/1.1"), _statusCode(200), _statusMessage("OK"),
      _headers(), _body(), _closeConnection(requestWantsClose(request))
{
    updateConnectionHeader();
    updateContentLength();
}

/*
函数：Response::getVersion
用途：只读返回状态行版本。
参数来源：无参数；读取构造函数初始化的 _version。
变量说明：无局部变量。
实现逻辑：直接返回 const 引用，避免复制且禁止外部修改。
*/
const std::string &Response::getVersion() const { return _version; }

/*
函数：Response::getStatusCode
用途：返回当前 HTTP 状态码。
参数来源：无参数；读取 setStatus()/createResponse()/CGI 适配器维护的 _statusCode。
变量说明：无局部变量。
实现逻辑：按值返回整数。
*/
int Response::getStatusCode() const { return _statusCode; }

/*
函数：Response::getStatusMessage
用途：只读返回状态码对应的 reason phrase。
参数来源：无参数；读取 _statusMessage。
变量说明：无局部变量。
实现逻辑：返回 const 引用。
*/
const std::string &Response::getStatusMessage() const { return _statusMessage; }

/*
函数：Response::getHeaders
用途：只读返回当前响应头集合，供 ServerManager、测试和调试读取。
参数来源：无参数；读取 _headers。
变量说明：无局部变量。
实现逻辑：返回 const 引用，外部不能绕过封装修改受控 headers。
*/
const Response::HeaderMap &Response::getHeaders() const { return _headers; }

/*
函数：Response::getBody
用途：只读返回二进制安全响应体。
参数来源：无参数；读取 _body。
变量说明：无局部变量。
实现逻辑：返回 const 引用，body 中的 NUL 不会截断。
*/
const std::string &Response::getBody() const { return _body; }

/*
函数：Response::shouldCloseConnection
用途：告诉 ServerManager 在发送完成后是否关闭客户端连接。
参数来源：无参数；读取 _closeConnection。
变量说明：无局部变量。
实现逻辑：按值返回布尔状态。
*/
bool Response::shouldCloseConnection() const { return _closeConnection; }


/*
函数：Response::setBody
用途：整体替换响应体并同步 Content-Length。
参数来源：body 来自静态文件、错误文本、目录页或测试数据。
变量说明：无局部变量。
实现逻辑：
    1. 当前状态禁止 body 时保持空 body。
    2. 允许时按 std::string 明确长度复制全部字节。
    3. 更新 Content-Length。
*/
void Response::setBody(const std::string &body)
{
    if (!statusMayHaveBody(_statusCode))
    {
        _body.clear();
        updateContentLength();
        return;
    }
    _body = body;
    updateContentLength();
}

/*
函数：Response::appendBody(const std::string &)
用途：把一个字符串片段追加到响应体末尾。
参数来源：data 通常来自逐块生成的页面或调用方已有 std::string。
变量说明：无局部变量。
实现逻辑：禁止 body 的状态直接返回；否则追加全部字节并更新长度。
*/
void Response::appendBody(const std::string &data)
{
    if (!statusMayHaveBody(_statusCode))
        return;
    _body += data;
    updateContentLength();
}

/*
函数：Response::appendBody(const char *, size_t)
用途：按明确长度追加二进制数据，允许片段中包含 NUL。
参数来源：data/length 来自 read() 缓冲区和实际 bytesRead。
变量说明：无局部变量。
实现逻辑：
    1. 禁止 body 的状态直接返回。
    2. 指针非 NULL 且长度非零时调用 string::append(data, length)。
    3. 同步 Content-Length。
*/
void Response::appendBody(const char *data, size_t length)
{
    if (!statusMayHaveBody(_statusCode))
        return;
    if (data != NULL && length != 0)
        _body.append(data, length);
    updateContentLength();
}

/*
函数：Response::clearBody
用途：清空响应体并同步消息边界 header。
参数来源：无参数；通常在状态切换或重新构造响应时调用。
变量说明：无局部变量。
实现逻辑：清空 _body 后调用 updateContentLength()。
*/
void Response::clearBody()
{
    _body.clear();
    updateContentLength();
}

/*
函数：Response::setCloseConnection
用途：修改发送完成后的连接策略，并同步 Connection header。
参数来源：closeConnection 来自 ServerManager 策略、Request 的 Connection token 或 CGI 适配结果。
变量说明：无局部变量。
实现逻辑：保存布尔值，然后调用 updateConnectionHeader()。
*/
void Response::setCloseConnection(bool closeConnection)
{
    _closeConnection = closeConnection;
    updateConnectionHeader();
}

/*
函数：Response::responseToString
用途：把封装后的状态、headers 和二进制 body 序列化成 ServerManager 可发送的完整 HTTP 字节串。
参数来源：无参数；读取当前 Response 的全部私有成员。
变量说明：
    - output：累积状态行、headers、空行和 body 的 ostringstream。
    - it：遍历 _headers 的只读迭代器。
实现逻辑：
    1. 写入状态行和 CRLF。
    2. 逐个写入规范化 header。
    3. 写入空行结束 header 区域。
    4. 用 output.write(data,size) 写入二进制 body，避免 NUL 截断。
*/
std::string Response::responseToString() const
{
    std::ostringstream output;
    output << _version << " " << _statusCode << " "
           << _statusMessage << "\r\n";
    HeaderMap::const_iterator it = _headers.begin();
    while (it != _headers.end())
    {
        output << it->first << ": " << it->second << "\r\n";
        ++it;
    }
    output << "\r\n";
    output.write(_body.data(), static_cast<std::streamsize>(_body.size()));
    return output.str();
}

