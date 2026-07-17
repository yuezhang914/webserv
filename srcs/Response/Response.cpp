/*
文件：srcs/Response/Response.cpp
用途：实现封装后的 Response class，以及 Request 到 GET/POST/DELETE Response 的唯一普通分发入口。
清理说明：旧 public struct 字段、重复版本检查、全局状态辅助函数和公开错误页内部接口均已删除。
一致性说明：Content-Length、Connection、无 body 状态和 header 大小写均由 Response 内部统一维护。
*/
#include "Response.hpp"
#include "EffectiveRoute.hpp"
#include "RequestHandler.hpp"
#include "Request.hpp"
#include "ConfigRouteUtils.hpp"

#include <fcntl.h>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

/* 把有效路由允许的方法按固定顺序生成 405 response 的 Allow header。 */
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
函数：isCGIRequest
用途：根据 normalized path 和 location 的 CGI 扩展名配置判断请求是否需要 CGI。
说明：这里只识别 CGI；执行模块尚未接入时 buildResponse() 返回安全的 501，避免脚本源码被静态 GET 泄露。
*/
static bool isCGIRequest(const LocationConfig *location,
                         const std::string &path)
{
    if (location == NULL || location->cgi_extensions.empty())
        return false;
    std::map<std::string, std::string>::const_iterator it =
        location->cgi_extensions.begin();
    while (it != location->cgi_extensions.end())
    {
        const std::string &extension = it->first;
        size_t pos = path.find(extension);
        while (pos != std::string::npos)
        {
            size_t extensionEnd = pos + extension.size();
            if (extensionEnd == path.size() || path[extensionEnd] == '/')
                return true;
            pos = path.find(extension, pos + 1);
        }
        ++it;
    }
    return false;
}

/*
函数：buildResponse
用途：把完整、合法的 Request 转成普通 HTTP Response。
实现顺序：匹配 location、拦截未接入 CGI、生成 EffectiveRoute、处理 redirect/方法规则，再分发 GET/POST/DELETE。
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
    if (isCGIRequest(location, request.getPath()))
    {
        response.createResponse(501,
            "CGI execution is not implemented yet.", server->error_pages);
        return response;
    }

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

Response::Response(bool closeConnection)
    : _version("HTTP/1.1"), _statusCode(200), _statusMessage("OK"),
      _headers(), _body(), _closeConnection(closeConnection)
{
    updateConnectionHeader();
    updateContentLength();
}

Response::Response(const Request &request)
    : _version("HTTP/1.1"), _statusCode(200), _statusMessage("OK"),
      _headers(), _body(), _closeConnection(requestWantsClose(request))
{
    updateConnectionHeader();
    updateContentLength();
}

const std::string &Response::getVersion() const { return _version; }
int Response::getStatusCode() const { return _statusCode; }
const std::string &Response::getStatusMessage() const { return _statusMessage; }
const Response::HeaderMap &Response::getHeaders() const { return _headers; }
const std::string &Response::getBody() const { return _body; }
bool Response::shouldCloseConnection() const { return _closeConnection; }

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

void Response::setHeader(const std::string &name, const std::string &value)
{
    if (!isValidHeaderName(name) || !isValidHeaderValue(value)
        || isManagedHeader(name))
        return;
    _headers[canonicalHeaderName(name)] = value;
}

void Response::removeHeader(const std::string &name)
{
    if (isManagedHeader(name))
        return;
    _headers.erase(canonicalHeaderName(name));
}

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

void Response::appendBody(const std::string &data)
{
    if (!statusMayHaveBody(_statusCode))
        return;
    _body += data;
    updateContentLength();
}

void Response::appendBody(const char *data, size_t length)
{
    if (!statusMayHaveBody(_statusCode))
        return;
    if (data != NULL && length != 0)
        _body.append(data, length);
    updateContentLength();
}

void Response::clearBody()
{
    _body.clear();
    updateContentLength();
}

void Response::setCloseConnection(bool closeConnection)
{
    _closeConnection = closeConnection;
    updateConnectionHeader();
}

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

bool Response::requestWantsClose(const Request &request)
{
    std::string value;
    if (!request.getHeader("connection", value))
        return false;
    size_t begin = 0;
    while (begin <= value.size())
    {
        size_t comma = value.find(',', begin);
        size_t end = comma == std::string::npos ? value.size() : comma;
        while (begin < end && (value[begin] == ' ' || value[begin] == '\t'))
            ++begin;
        while (end > begin && (value[end - 1] == ' ' || value[end - 1] == '\t'))
            --end;
        if (toLowerAscii(value.substr(begin, end - begin)) == "close")
            return true;
        if (comma == std::string::npos)
            break;
        begin = comma + 1;
    }
    return false;
}

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
        case 504: return "Gateway Timeout";
        case 505: return "HTTP Version Not Supported";
    }
    if (statusCode >= 300 && statusCode <= 399)
        return "Redirect";
    return "Unknown";
}

bool Response::isErrorStatusCode(int statusCode)
{
    return statusCode >= 400 && statusCode <= 599;
}

bool Response::statusMayHaveBody(int statusCode)
{
    return !((statusCode >= 100 && statusCode < 200)
        || statusCode == 204 || statusCode == 304);
}

std::string Response::sizeToString(size_t value)
{
    std::ostringstream output;
    output << value;
    return output.str();
}

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

bool Response::isManagedHeader(const std::string &name)
{
    std::string lower = toLowerAscii(name);
    return lower == "content-length" || lower == "connection";
}

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

void Response::setManagedHeader(const std::string &name,
                                const std::string &value)
{
    _headers[canonicalHeaderName(name)] = value;
}

void Response::updateContentLength()
{
    if (!statusMayHaveBody(_statusCode))
    {
        _headers.erase("Content-Length");
        return;
    }
    setManagedHeader("Content-Length", sizeToString(_body.size()));
}

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

