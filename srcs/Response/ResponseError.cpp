/*
文件：srcs/Response/ResponseError.cpp
用途：统一创建响应，并负责加载自定义错误页、读取错误页文件和生成默认 HTML 错误页。
拆分说明：相关函数从原 Response.cpp 原样移动，不改变自定义错误页优先级或失败回退规则。
*/
#include "Response.hpp"

#include <fcntl.h>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

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

