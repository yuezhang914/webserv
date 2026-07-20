/*
文件：srcs/Response/Response.cpp
用途：实现 Response class，并在不改变 ServerManager 既有接口的前提下，把合法 Request 路由到 redirect、异步 CGI、GET、POST 或 DELETE。
CGI 接口边界：buildResponse() 继续返回 Response；CGI 分支用现有 ServerManager 已识别的内部 header 交付脚本路径，ServerManager 负责 poll 管道，parseCgiOutput() 负责把最终 stdout 转成 Response。
清理说明：不使用内部路径 header、重复状态行字段、旧同步执行接口或完整 HTTP 文本回解析适配器。
*/
#include "Response.hpp"
#include "ConfigRouteUtils.hpp"
#include "EffectiveRoute.hpp"
#include "Request.hpp"
#include "RequestHandler.hpp"

#include <cerrno>
#include <fcntl.h>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

/*
函数：buildAllowHeader
用途：为 405 Method Not Allowed 生成稳定顺序的 Allow header。
参数来源：allowMethods 来自 EffectiveRoute 合并后的 server/location allow_methods。
变量说明：
    - orderedMethods：本项目真正实现的三个方法及输出顺序。
    - result：逐步拼出的 header value，例如 "GET, POST"。
    - i：遍历 orderedMethods 的下标。
实现逻辑：
    1. 按 GET、POST、DELETE 的固定顺序检查集合。
    2. 找到允许的方法时，在非首项前加入逗号和空格。
    3. 返回可以直接交给 Response::setHeader("Allow", ...) 的字符串。
*/
static std::string buildAllowHeader(const std::set<std::string> &allowMethods)
{
    const char *orderedMethods[] = {"GET", "POST", "DELETE"};
    std::string result;
    size_t i = 0;
    while (i < 3)
    {
        if (allowMethods.find(orderedMethods[i]) != allowMethods.end())
        {
            if (!result.empty())
                result += ", ";
            result += orderedMethods[i];
        }
        ++i;
    }
    return result;
}

/*
函数：trimOws
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
static std::string trimOws(const std::string &value)
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
        std::string token = trimOws(value.substr(begin, end - begin));
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
函数：isConfiguredCgiPath
用途：判断 normalized request path 是否以当前 location 配置的 CGI 后缀结尾。
参数来源：
    - location：buildResponse() 通过 findMatchingLocation() 找到的最长匹配 location。
    - path：RequestParser 已经解码、规范化并去掉 query 的 request.getPath()。
变量说明：
    - it：遍历 location->cgi_extensions 的迭代器；key 是 .py/.sh 等后缀，value 是队友配置的解释器路径。
    - extension：当前检查的 CGI 后缀。
实现逻辑：
    1. 没有 location 或没有 cgi_extension 配置时返回 false。
    2. 逐个检查 path 最后是否完整等于某个扩展名。
    3. 只负责识别是否走 CGI；解释器 value 不在 Response 中使用，因为队友公开接口只接收 script_path。
*/
static bool isConfiguredCgiPath(const LocationConfig *location,
                                const std::string &path)
{
    if (location == NULL || location->cgi_extensions.empty())
        return false;
    std::map<std::string, std::string>::const_iterator it =
        location->cgi_extensions.begin();
    while (it != location->cgi_extensions.end())
    {
        const std::string &extension = it->first;
        if (!extension.empty() && path.size() >= extension.size()
            && path.compare(path.size() - extension.size(),
                extension.size(), extension) == 0)
            return true;
        ++it;
    }
    return false;
}

/*
函数：validateCgiScript
用途：在 fork/execve 前确认 EffectiveRoute 生成的 CGI 目标确实是可执行普通文件。
参数来源：scriptPath 来自 route.targetPath，即 root/alias 与 request.getPath() 合成后的真实磁盘路径。
变量说明：
    - info：stat() 填充的文件类型和权限信息。
实现逻辑：
    1. stat 失败时把不存在/中间目录不存在映射为 404，权限失败映射为 403，其余映射为 500。
    2. 目标不是普通文件时返回 403，避免目录、FIFO、socket 或设备进入 execve。
    3. access(X_OK) 失败时返回 403。
    4. 所有检查通过返回 PATH_OK，buildResponse() 随后返回 RESPONSE_BUILD_CGI 和该脚本路径。
*/
static int validateCgiScript(const std::string &scriptPath)
{
    struct stat info;
    if (stat(scriptPath.c_str(), &info) != 0)
    {
        if (errno == ENOENT || errno == ENOTDIR)
            return 404;
        if (errno == EACCES || errno == EPERM)
            return 403;
        return 500;
    }
    if (!S_ISREG(info.st_mode))
        return 403;
    if (access(scriptPath.c_str(), X_OK) != 0)
        return 403;
    return PATH_OK;
}

/*
函数：buildResponse
用途：保留其他模块已经使用的 Response 返回接口，完成路由、方法、路径和 CGI 分发判断。
参数来源：request 由 RequestParser 完整解析并由 ServerManager 传入。
变量说明：
    - server/location/route：分别表示当前 server 配置、最长匹配 location 和合并后的有效路由。
    - response：当前请求的默认响应，继承 Request 的 Connection 策略。
    - action/pathStatus/cgiPathStatus：分别保存方法枚举、普通路径检查结果和 CGI 脚本检查结果。
实现逻辑：
    1. 检查 ServerConfig 并找到最长匹配 location，合并 EffectiveRoute。
    2. 优先处理 redirect，再检查方法是否实现以及是否被配置允许。
    3. 生成真实磁盘路径；CGI 路径先验证存在、普通文件和执行权限。
    4. CGI 请求不在 Response 内运行，只把脚本路径写入现有 ServerManager 已识别的 X-Internal-CGI-Path 内部 header。
    5. 非 CGI 请求按 GET、POST、DELETE 返回最终 Response。
接口边界：返回类型保持 Response，避免要求 ServerManager 改用新的分发结果类型。
*/
Response buildResponse(const Request &request)
{
    const ServerConfig *server = request.getConfig();
    Response response(request);
    if (server == NULL)
    {
        Response::ErrorPageMap noErrorPages;
        response.createResponse(500, "", noErrorPages);
        return response;
    }

    const LocationConfig *location =
        findMatchingLocation(request.getPath(), server->locations);
    EffectiveRoute route;
    bool routeReady = location != NULL
        ? route.createEffectiveRoute(server, location)
        : route.createEffectiveRoute(server);
    if (!routeReady)
    {
        response.createResponse(500, "", server->error_pages);
        return response;
    }

    if (location != NULL && route.redirect_status >= 300
        && route.redirect_status <= 399 && !route.redirect_url.empty())
    {
        response.setStatus(route.redirect_status);
        response.setHeader("Location", route.redirect_url);
        if (route.redirect_status != 304)
        {
            response.setHeader("Content-Type", "text/html");
            response.setBody("<!DOCTYPE html><html><head><title>Redirect</title></head><body>Redirecting</body></html>");
        }
        return response;
    }

    RequestAction action = requestActionFromMethod(request.getMethod());
    if (action == ACTION_UNSUPPORTED)
    {
        response.createResponse(501, "", route.server->error_pages);
        return response;
    }
    if (!isMethodAllowed(action, route.allow_methods))
    {
        response.createResponse(405, "", route.server->error_pages);
        response.setHeader("Allow", buildAllowHeader(route.allow_methods));
        return response;
    }

    int pathStatus = route.createEffectivePath(request.getPath(), action);
    if (isConfiguredCgiPath(location, request.getPath()))
    {
        int cgiPathStatus = validateCgiScript(route.targetPath);
        if (cgiPathStatus != PATH_OK)
        {
            response.createResponse(cgiPathStatus, "",
                route.server->error_pages);
            return response;
        }
        response.setStatus(200);
        response.setHeader("X-Internal-CGI-Path", route.targetPath);
        return response;
    }

    if (pathStatus != PATH_OK)
    {
        response.createResponse(pathStatus, "", route.server->error_pages);
        return response;
    }
    if (action == ACTION_GET)
        return handleGet(request, route);
    if (action == ACTION_POST)
        return handlePost(request, route);
    return handleDelete(request, route);
}

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
函数：Response::getHeader
用途：以大小写不敏感方式读取一个响应头。
参数来源：name 由 ServerManager、测试或调用方提供；value 是调用方准备接收结果的输出变量。
变量说明：
    - it：在 canonical header map 中查找后的迭代器。
实现逻辑：
    1. canonicalHeaderName() 统一 name。
    2. 不存在时清空 value 并返回 false。
    3. 存在时复制 header value 并返回 true。
*/
bool Response::getHeader(const std::string &name, std::string &value) const
{
    HeaderMap::const_iterator it = _headers.find(canonicalHeaderName(name));
    if (it == _headers.end())
    {
        value.clear();
        return false;
    }
    value = it->second;
    return true;
}

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
函数：Response::setHeader
用途：设置普通响应头，同时阻止非法 header 和外部覆盖受控字段。
参数来源：name/value 来自 redirect、MIME 判断、CGI 适配或业务处理函数。
变量说明：无额外局部变量。
实现逻辑：
    1. 校验 header name 是 HTTP token。
    2. 校验 value 不含 CR/LF、非法控制字符或 DEL。
    3. 拒绝 Content-Length 和 Connection，它们只能由 Response 内部维护。
    4. 统一名称大小写后写入 map；同名 header 会覆盖而不会重复。
*/
void Response::setHeader(const std::string &name, const std::string &value)
{
    if (!isValidHeaderName(name) || !isValidHeaderValue(value)
        || isManagedHeader(name))
        return;
    _headers[canonicalHeaderName(name)] = value;
}

/*
函数：Response::removeHeader
用途：删除普通响应头，但保护 Content-Length 和 Connection。
参数来源：name 由状态切换或业务逻辑提供。
变量说明：无局部变量。
实现逻辑：受控 header 直接忽略；其他名称规范化后从 map 删除。
*/
void Response::removeHeader(const std::string &name)
{
    if (isManagedHeader(name))
        return;
    _headers.erase(canonicalHeaderName(name));
}

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
函数：Response::createResponse
用途：统一创建成功、错误或特殊状态响应，并加载自定义/默认错误页。
参数来源：
    - code：buildResponse/RequestHandler 得到的 HTTP 状态码。
    - bodyText：调用方希望作为普通文本 body 的说明。
    - errorPages：当前 ServerConfig 的 error_pages。
变量说明：无额外局部变量；内部直接重置当前对象。
实现逻辑：
    1. 清空旧 headers/body 并设置新状态。
    2. 允许 body 且 bodyText 非空时保存 text/plain body。
    3. 4xx/5xx 优先读取自定义错误页，失败时生成默认 HTML。
    4. 对无 body 状态再次清理 Content-Type/body。
    5. 最终同步 Connection 和 Content-Length。
*/
void Response::createResponse(unsigned int code, const std::string &bodyText,
                              const ErrorPageMap &errorPages)
{
    _headers.clear();
    _body.clear();
    setStatus(static_cast<int>(code));
    if (statusMayHaveBody(_statusCode) && !bodyText.empty())
    {
        _body = bodyText;
        _headers["Content-Type"] = "text/plain";
    }

    if (isErrorStatusCode(_statusCode))
    {
        if (!loadCustomErrorPage(errorPages))
            setDefaultErrorPage();
    }
    if (!statusMayHaveBody(_statusCode))
    {
        _body.clear();
        _headers.erase("Content-Type");
    }
    updateConnectionHeader();
    updateContentLength();
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
函数：Response::toLowerAscii
用途：只转换 ASCII A-Z，供大小写不敏感的 header/token 比较。
参数来源：value 来自 header name 或 Connection token。
变量说明：result 是可修改副本；i 是字符下标。
实现逻辑：遍历全部字符，只把 A-Z 加偏移改为 a-z，其他字节保持不变。
*/
std::string Response::toLowerAscii(const std::string &value)
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
函数：Response::canonicalHeaderName
用途：把任意大小写 header name 统一成 Content-Type 形式的 map key。
参数来源：name 来自 setHeader/getHeader/removeHeader 或 CGI header。
变量说明：result 是先全小写后的副本；uppercaseNext 表示当前字符是否位于开头或连字符后；i 是下标。
实现逻辑：先转小写，再把首字母及每个 '-' 后的字母转为大写。
*/
std::string Response::canonicalHeaderName(const std::string &name)
{
    std::string result = toLowerAscii(name);
    bool uppercaseNext = true;
    size_t i = 0;
    while (i < result.size())
    {
        if (uppercaseNext && result[i] >= 'a' && result[i] <= 'z')
            result[i] = static_cast<char>(result[i] - 'a' + 'A');
        uppercaseNext = result[i] == '-';
        ++i;
    }
    return result;
}

/*
函数：Response::isManagedHeader
用途：识别只能由 Response 内部维护的消息边界/连接 headers。
参数来源：name 来自 setHeader/removeHeader 或 CGI 导入。
变量说明：lower 是 ASCII 小写名称。
实现逻辑：Content-Length 或 Connection 返回 true，其余 false。
*/
bool Response::isManagedHeader(const std::string &name)
{
    std::string lower = toLowerAscii(name);
    return lower == "content-length" || lower == "connection";
}

/*
函数：Response::isValidHeaderName
用途：验证 response header name 是否为合法 HTTP token，防止坏格式和 header 注入。
参数来源：name 来自业务代码或 CGI response。
变量说明：symbols 是 token 允许的特殊字符；i/c 用于逐字节检查。
实现逻辑：空名称拒绝；每个字符必须是字母、数字或 symbols 中的字符。
*/
bool Response::isValidHeaderName(const std::string &name)
{
    if (name.empty())
        return false;
    const std::string symbols("!#$%&'*+-.^_`|~");
    size_t i = 0;
    while (i < name.size())
    {
        unsigned char c = static_cast<unsigned char>(name[i]);
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z')
            || (c >= 'a' && c <= 'z') || symbols.find(c) != std::string::npos))
            return false;
        ++i;
    }
    return true;
}

/*
函数：Response::isValidHeaderValue
用途：拒绝 response header value 中的 CR/LF、非法控制字符和 DEL。
参数来源：value 来自业务代码或 CGI header。
变量说明：i/c 用于逐字节检查。
实现逻辑：允许可见字节和 tab；其他 0-31 控制字符及 127 返回 false。
*/
bool Response::isValidHeaderValue(const std::string &value)
{
    size_t i = 0;
    while (i < value.size())
    {
        unsigned char c = static_cast<unsigned char>(value[i]);
        if ((c < 32 && c != '\t') || c == 127)
            return false;
        ++i;
    }
    return true;
}

/*
函数：Response::setManagedHeader
用途：供 Response 内部写入 Content-Length 或 Connection，绕过公开接口的保护。
参数来源：name/value 只来自 updateContentLength()、updateConnectionHeader() 和内部适配流程。
变量说明：无局部变量。
实现逻辑：规范化名称后直接写入 _headers。
*/
void Response::setManagedHeader(const std::string &name,
                                const std::string &value)
{
    _headers[canonicalHeaderName(name)] = value;
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

/*
函数：Response::loadCustomErrorPage
用途：为当前错误状态尝试读取 ServerConfig 指定的自定义 HTML 文件。
参数来源：errorPages 来自 route.server->error_pages；key 是当前 _statusCode。
变量说明：
    - it：查找当前状态码的迭代器。
    - fd：open() 得到的只读文件描述符。
    - fileInfo：stat() 返回的文件类型信息。
实现逻辑：
    1. 没配置当前状态码时返回 false。
    2. 打开文件并确认它是普通文件。
    3. 清空旧 body，通过 readOpenedFileIntoBody() 读取。
    4. 成功时设置 text/html 并返回 true；任何失败返回 false，由调用方生成默认页。
*/
bool Response::loadCustomErrorPage(const ErrorPageMap &errorPages)
{
    ErrorPageMap::const_iterator it = errorPages.find(_statusCode);
    if (it == errorPages.end())
        return false;
    int fd = open(it->second.c_str(), O_RDONLY);
    if (fd < 0)
        return false;
    struct stat fileInfo;
    if (stat(it->second.c_str(), &fileInfo) != 0 || !S_ISREG(fileInfo.st_mode))
    {
        close(fd);
        return false;
    }
    _body.clear();
    if (!readOpenedFileIntoBody(fd))
    {
        _body.clear();
        return false;
    }
    _headers["Content-Type"] = "text/html";
    return true;
}

/*
函数：Response::readOpenedFileIntoBody
用途：把已经打开的普通文件按块读入二进制安全 body，并保证 fd 被关闭。
参数来源：fd 来自 loadCustomErrorPage() 的 open()。
变量说明：
    - bufferSize：单次读取 64 KiB。
    - buffer：可重复使用的字节数组。
    - bytesRead：每次 read() 的实际结果。
实现逻辑：循环 read 并按 bytesRead 追加；退出后关闭 fd；负值表示失败。
*/
bool Response::readOpenedFileIntoBody(int fd)
{
    const size_t bufferSize = 64 * 1024;
    std::vector<char> buffer(bufferSize);
    ssize_t bytesRead = 0;
    while ((bytesRead = read(fd, &buffer[0], bufferSize)) > 0)
        _body.append(&buffer[0], static_cast<size_t>(bytesRead));
    close(fd);
    return bytesRead >= 0;
}

/*
函数：Response::setDefaultErrorPage
用途：在没有可用自定义错误页时生成最小 HTML 错误页。
参数来源：无参数；读取当前 _statusCode 和 _statusMessage。
变量说明：body 是拼接 HTML 的 ostringstream。
实现逻辑：生成 title 和 h1，保存到 _body，并设置 Content-Type: text/html。
*/
void Response::setDefaultErrorPage()
{
    std::ostringstream body;
    body << "<!DOCTYPE html><html><head><title>"
         << _statusCode << " " << _statusMessage
         << "</title></head><body><h1>"
         << _statusCode << " " << _statusMessage
         << "</h1></body></html>";
    _body = body.str();
    _headers["Content-Type"] = "text/html";
}

/*
函数：Response::loadCgiOutput
用途：把异步 CGI 管道收集到的原始 stdout 解析成当前 Response 的状态、普通 headers 和 body。
参数来源：cgiOutput 由 ServerManager 对 CgiFds.read_fd 执行非阻塞 read()，直到 EOF 后汇总得到。
变量说明：
    - delimiterPos/delimiterLength：CGI headers 与 body 的分隔位置和长度。
    - headerBlock/body：分离后的 header 文本和二进制 body。
    - importedHeaders：暂存已验证且允许透传的 CGI headers。
    - importedStatus/hasStatus：Status 特殊头解析出的状态码及是否已经出现。
    - cursor/lineEnd/line/colon/name/value：逐行解析 header 的边界和字段。
    - lowerName/statusStream/reason：大小写判断及 Status value 解析工具。
实现逻辑：
    1. 空输出直接失败；没有空行分隔时把全部输出当作 200 text/html body。
    2. 有 header block 时逐行要求 name:value 结构，并校验 header name/value。
    3. Status 只能出现一次，状态码必须在 100 到 599；reason phrase 只做格式消费，最终由 Response 的统一映射生成。
    4. Content-Length、Transfer-Encoding、Connection 属于服务器管理字段，全部忽略；其他合法 header 暂存并规范化名称。
    5. 清空旧响应数据，恢复解析出的状态；允许 body 时导入 headers 和 body，没有 Content-Type/Location 时默认 text/html。
    6. 对 1xx、204、304 自动丢弃 body 和 Content-Type；最后按真实 body 长度更新 Content-Length，并保留原 Request 的连接策略。
*/
bool Response::loadCgiOutput(const std::string &cgiOutput)
{
    if (cgiOutput.empty())
        return false;

    size_t delimiterPos = cgiOutput.find("\r\n\r\n");
    size_t delimiterLength = 4;
    if (delimiterPos == std::string::npos)
    {
        delimiterPos = cgiOutput.find("\n\n");
        delimiterLength = 2;
    }

    bool inheritedClose = _closeConnection;
    if (delimiterPos == std::string::npos)
    {
        _statusCode = 200;
        _statusMessage = statusMessageFor(_statusCode);
        _headers.clear();
        _body = cgiOutput;
        _closeConnection = inheritedClose;
        _headers["Content-Type"] = "text/html";
        updateConnectionHeader();
        updateContentLength();
        return true;
    }

    std::string headerBlock = cgiOutput.substr(0, delimiterPos);
    std::string body = cgiOutput.substr(delimiterPos + delimiterLength);
    HeaderMap importedHeaders;
    int importedStatus = 200;
    bool hasStatus = false;

    size_t cursor = 0;
    while (cursor <= headerBlock.size())
    {
        size_t lineEnd = headerBlock.find('\n', cursor);
        if (lineEnd == std::string::npos)
            lineEnd = headerBlock.size();
        std::string line = headerBlock.substr(cursor, lineEnd - cursor);
        if (!line.empty() && line[line.size() - 1] == '\r')
            line.erase(line.size() - 1);
        if (line.empty())
            return false;

        size_t colon = line.find(':');
        if (colon == std::string::npos)
            return false;
        std::string name = line.substr(0, colon);
        std::string value = trimOws(line.substr(colon + 1));
        if (!isValidHeaderName(name) || !isValidHeaderValue(value))
            return false;

        std::string lowerName = toLowerAscii(name);
        if (lowerName == "status")
        {
            if (hasStatus)
                return false;
            std::istringstream statusStream(value);
            if (!(statusStream >> importedStatus)
                || importedStatus < 100 || importedStatus > 599)
                return false;
            std::string reason;
            std::getline(statusStream, reason);
            reason = trimOws(reason);
            hasStatus = true;
        }
        else if (lowerName != "content-length"
            && lowerName != "transfer-encoding"
            && lowerName != "connection")
        {
            importedHeaders[canonicalHeaderName(name)] = value;
        }

        if (lineEnd == headerBlock.size())
            break;
        cursor = lineEnd + 1;
    }

    _statusCode = importedStatus;
    _statusMessage = statusMessageFor(_statusCode);
    _headers.clear();
    _body.clear();
    _closeConnection = inheritedClose;

    HeaderMap::const_iterator it = importedHeaders.begin();
    while (it != importedHeaders.end())
    {
        if (statusMayHaveBody(_statusCode)
            || toLowerAscii(it->first) != "content-type")
            _headers[it->first] = it->second;
        ++it;
    }

    if (statusMayHaveBody(_statusCode))
    {
        if (_headers.find("Content-Type") == _headers.end()
            && _headers.find("Location") == _headers.end())
            _headers["Content-Type"] = "text/html";
        _body = body;
    }
    else
        _headers.erase("Content-Type");

    updateConnectionHeader();
    updateContentLength();
    return true;
}

/*
函数：Response::parseCgiOutput
用途：保留 ServerManager 已经调用的公开成员接口，把完整 CGI stdout 解析并覆盖当前 Response。
参数来源：cgiOutput 由 ServerManager 从 CGI 输出管道累计到连接或 CGI 任务缓冲区后传入。
变量说明：
    - noErrorPages：当成员函数没有 Request/ServerConfig 可读取时，用于生成 502 的安全空错误页映射。
实现逻辑：
    1. 调用私有 loadCgiOutput() 完成 Status、普通 headers 和二进制 body 的严格解析。
    2. 成功时 loadCgiOutput() 已清空旧内部 header，并按真实 body 更新 Content-Length。
    3. 失败时使用空错误页映射生成 502 Bad Gateway。
    4. 最后强制 Connection: close，避免复用 CGI 输出已经异常的连接。
*/
void Response::parseCgiOutput(const std::string &cgiOutput)
{
    if (loadCgiOutput(cgiOutput))
        return;

    ErrorPageMap noErrorPages;
    createResponse(502, "Invalid CGI response", noErrorPages);
    setCloseConnection(true);
}

/*
函数：buildCgiResponse
用途：在异步 CGI stdout 读取完成后，构造最终可发送的 Response。
参数来源：
    - request：ServerManager 启动 CGI 时保存的原请求，用于继承 Connection 策略和读取 ServerConfig 错误页。
    - cgiOutput：从 CgiFds.read_fd 收集的完整原始 stdout，不包含服务器预制 HTTP 状态行。
变量说明：
    - response：以原 Request 构造的目标响应。
    - server：Request 持有的 ServerConfig 指针，用于 502 自定义错误页。
    - noErrorPages：Request 缺少配置时的安全空映射。
实现逻辑：
    1. 创建继承 request close 策略的 Response。
    2. 调用私有 loadCgiOutput() 解析 CGI stdout；成功时直接返回。
    3. 空输出或格式错误时生成 502 Bad Gateway。
    4. 502 后显式关闭连接，避免客户端继续复用一个 CGI 状态不确定的连接。
*/
Response buildCgiResponse(const Request &request,
                          const std::string &cgiOutput)
{
    Response response(request);
    if (response.loadCgiOutput(cgiOutput))
        return response;

    const ServerConfig *server = request.getConfig();
    if (server != NULL)
        response.createResponse(502, "Invalid CGI response",
            server->error_pages);
    else
    {
        Response::ErrorPageMap noErrorPages;
        response.createResponse(502, "Invalid CGI response",
            noErrorPages);
    }
    response.setCloseConnection(true);
    return response;
}
