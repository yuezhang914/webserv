/*
文件：srcs/Response/ResponseStatus.cpp
用途：集中维护状态码、reason phrase、状态是否允许 body，以及 Content-Length 与状态变化的一致性。
拆分说明：函数从原 Response.cpp 原样移动，不改变状态映射、强制无 body 状态或长度计算规则。
*/
#include "Response.hpp"

#include <sstream>

/*
函数：Response::setStatus
用途：修改状态码并保持 reason phrase、无 body 状态和受控 headers 一致。
参数来源：statusCode 来自 buildResponse、RequestHandler、错误映射或 CGI HTTP 文本。
变量说明：无额外局部变量。
实现逻辑：
    1. 保存状态码并通过 statusMessageFor() 同步短语。
    2. 对 1xx、204、304 清空 body 和 Content-Type。
    3. 根据错误状态更新连接策略。
    4. 重新计算 Content-Length 或删除它。
*/
void Response::setStatus(int statusCode)
{
    _statusCode = statusCode;
    _statusMessage = statusMessageFor(statusCode);
    if (!statusMayHaveBody(_statusCode))
    {
        _body.clear();
        _headers.erase("Content-Type");
    }
    updateConnectionHeader();
    updateContentLength();
}


/*
函数：Response::statusMessageFor
用途：把项目会生成的 HTTP 状态码映射成标准 reason phrase。
参数来源：statusCode 来自 setStatus()/createResponse()/CGI HTTP 状态行。
变量说明：无局部变量；switch 直接返回。
实现逻辑：
    1. 对已知状态返回固定短语。
    2. 未单列的 3xx 返回 Redirect。
    3. 其他未知码返回 Unknown，保证状态行仍可生成。
*/
std::string Response::statusMessageFor(int statusCode)
{
    switch (statusCode)
    {
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        case 300: return "Multiple Choices";
        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 303: return "See Other";
        case 304: return "Not Modified";
        case 307: return "Temporary Redirect";
        case 308: return "Permanent Redirect";
        case 400: return "Bad Request";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 408: return "Request Timeout";
        case 409: return "Conflict";
        case 411: return "Length Required";
        case 413: return "Payload Too Large";
        case 414: return "URI Too Long";
        case 415: return "Unsupported Media Type";
        case 423: return "Locked";
        case 431: return "Request Header Fields Too Large";
        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        case 502: return "Bad Gateway";
        case 504: return "Gateway Timeout";
        case 505: return "HTTP Version Not Supported";
    }
    if (statusCode >= 300 && statusCode <= 399)
        return "Redirect";
    return "Unknown";
}

/*
函数：Response::isErrorStatusCode
用途：判断状态是否进入自定义/默认错误页流程。
参数来源：statusCode 是当前 _statusCode。
变量说明：无局部变量。
实现逻辑：400 到 599 返回 true，其余返回 false。
*/
bool Response::isErrorStatusCode(int statusCode)
{
    return statusCode >= 400 && statusCode <= 599;
}

/*
函数：Response::statusMayHaveBody
用途：判断 HTTP 状态是否允许携带响应体。
参数来源：statusCode 来自当前 Response 状态或 CGI 状态行。
变量说明：无局部变量。
实现逻辑：1xx、204、304 返回 false，其余返回 true。
*/
bool Response::statusMayHaveBody(int statusCode)
{
    return !((statusCode >= 100 && statusCode < 200)
        || statusCode == 204 || statusCode == 304);
}

/*
函数：Response::sizeToString
用途：把 size_t 长度安全转为十进制字符串，供 Content-Length 使用。
参数来源：value 通常是 _body.size()。
变量说明：output 是执行数字格式化的 ostringstream。
实现逻辑：写入 value 后返回 output.str()。
*/
std::string Response::sizeToString(size_t value)
{
    std::ostringstream output;
    output << value;
    return output.str();
}


/*
函数：Response::updateContentLength
用途：让 Content-Length 始终等于真实 body 字节数。
参数来源：无参数；读取 _statusCode 和 _body.size()。
变量说明：无局部变量。
实现逻辑：无 body 状态删除 Content-Length；其他状态写入十进制长度。
*/
void Response::updateContentLength()
{
    if (!statusMayHaveBody(_statusCode))
    {
        _headers.erase("Content-Length");
        return;
    }
    setManagedHeader("Content-Length", sizeToString(_body.size()));
}

