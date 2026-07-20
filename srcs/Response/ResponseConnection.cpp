/*
文件：srcs/Response/ResponseConnection.cpp
用途：解析 Request 的 Connection token，并根据请求策略和响应错误状态维护 Connection header。
拆分说明：相关函数从原 Response.cpp 原样移动，不改变 HTTP/1.1 默认 keep-alive 或强制关闭状态集合。
*/
#include "Response.hpp"
#include "Request.hpp"
#include "ResponseInternal.hpp"

/*
函数：responseTrimOws
用途：删除 HTTP header value 两端的 OWS，即普通空格和 tab。
参数来源：Response::loadCgiOutput() 从异步 CGI 原始 stdout 的 header block 中切出的 header value。
变量说明：
    - begin：第一个非 OWS 字符的位置。
    - end：最后一个非 OWS 字符之后的位置。
实现逻辑：
    1. 从左向右跳过空格和 tab。
    2. 从右向左跳过空格和 tab。
    3. substr 返回中间有效部分；全是空白时返回空字符串。
*/
std::string responseTrimOws(const std::string &value)
{
    size_t begin = 0;
    while (begin < value.size()
        && (value[begin] == ' ' || value[begin] == '\t'))
        ++begin;
    size_t end = value.size();
    while (end > begin
        && (value[end - 1] == ' ' || value[end - 1] == '\t'))
        --end;
    return value.substr(begin, end - begin);
}

/*
函数：headerValueContainsClose
用途：判断一个 Connection header token 列表中是否包含 close。
参数来源：Response(Request) 从客户端 Request 的 Connection value 中判断是否包含 close；CGI 输出中的 Connection 不会进入这里。
变量说明：
    - begin：当前 token 的起始位置。
    - comma：下一个逗号位置；不存在时表示最后一个 token。
    - end：当前 token 的结束位置。
    - token：去掉两端 OWS 后的小写 token。
实现逻辑：
    1. 按逗号逐段切分 Connection value。
    2. 删除每段两端的空格和 tab。
    3. 手动把 ASCII 大写字母转小写。
    4. 任一 token 等于 close 就返回 true，否则遍历结束返回 false。
*/
static bool headerValueContainsClose(const std::string &value)
{
    size_t begin = 0;
    while (begin <= value.size())
    {
        size_t comma = value.find(',', begin);
        size_t end = comma == std::string::npos ? value.size() : comma;
        std::string token = responseTrimOws(value.substr(begin, end - begin));
        size_t i = 0;
        while (i < token.size())
        {
            if (token[i] >= 'A' && token[i] <= 'Z')
                token[i] = static_cast<char>(token[i] - 'A' + 'a');
            ++i;
        }
        if (token == "close")
            return true;
        if (comma == std::string::npos)
            break;
        begin = comma + 1;
    }
    return false;
}


/*
函数：Response::requestWantsClose
用途：从 Request 的 Connection token 列表判断客户端是否要求关闭连接。
参数来源：request 来自 Response(Request) 构造函数；header 已由 RequestParser 统一为大小写不敏感访问。
变量说明：
    - value：完整 Connection header value。
    - begin/comma/end：逐个切分逗号 token 的边界。
实现逻辑：
    1. 没有 Connection header 时按 HTTP/1.1 默认保持连接。
    2. 按逗号切分并 trim OWS。
    3. 任一 token 小写后等于 close 就返回 true。
*/
bool Response::requestWantsClose(const Request &request)
{
    std::string value;
    if (!request.getHeader("connection", value))
        return false;
    return headerValueContainsClose(value);
}


/*
函数：Response::updateConnectionHeader
用途：根据显式连接策略和必须关闭的错误状态同步 Connection header。
参数来源：无参数；读取/必要时修改 _closeConnection 与 _statusCode。
变量说明：无额外局部变量；switch 检查固定状态集合。
实现逻辑：
    1. 调用方尚未要求关闭时，检查 400/408/409/411/413/414/431/505 等错误。
    2. 这些状态强制 _closeConnection=true。
    3. 写入 Connection: close 或 keep-alive。
*/
void Response::updateConnectionHeader()
{
    if (!_closeConnection)
    {
        switch (_statusCode)
        {
            case 400:
            case 408:
            case 409:
            case 411:
            case 413:
            case 414:
            case 431:
            case 505:
                _closeConnection = true;
                break;
            default:
                break;
        }
    }
    setManagedHeader("Connection",
        _closeConnection ? "close" : "keep-alive");
}

