/*
文件：srcs/Response/RequestDirectory.cpp
用途：处理目录 index 候选、autoindex 开关和安全目录列表 HTML 生成。
拆分说明：函数从原 RequestHandler.cpp 按“目录 GET”职责原样移动；不泄露磁盘路径，URL 编码与 HTML 转义规则保持不变。
*/
#include "RequestHandlerInternal.hpp"

#include <cerrno>
#include <dirent.h>
#include <fcntl.h>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

static Response createIndexResponse(int fd, const std::string &indexPath,
    bool closeConnection, const Response::ErrorPageMap &errorPages);
static Response handleAutoIndex(const EffectiveRoute &route,
    const std::string &requestPath, bool closeConnection);
static Response createAutoIndexResponse(const EffectiveRoute &route,
    const std::string &requestPath, bool closeConnection);

/*
函数：escapeHtml
用途：转义 autoindex 页面中作为 HTML 文本或 attribute value 输出的内容。
参数来源：text 来自 request.getPath()、目录项显示名或已经 URL 编码的 href。
变量说明：output 累积安全文本；i 遍历输入字符。
实现逻辑：把 &、<、>、双引号、单引号替换为实体，其余字符原样追加。
*/
static std::string escapeHtml(const std::string &text)
{
    std::string output;
    size_t i = 0;
    while (i < text.size())
    {
        if (text[i] == '&') output += "&amp;";
        else if (text[i] == '<') output += "&lt;";
        else if (text[i] == '>') output += "&gt;";
        else if (text[i] == '"') output += "&quot;";
        else if (text[i] == '\'') output += "&#39;";
        else output += text[i];
        ++i;
    }
    return output;
}

/*
函数：encodePathSegment
用途：把一个目录项名称编码为单个 URL path segment，防止 ?、#、空格、% 等改变链接语义。
参数来源：name 来自 readdir() 的 d_name，不包含父目录路径。
变量说明：hex 是十六进制表；output 是结果；i/c 遍历字节；unreserved 判断 RFC 常用安全字符。
实现逻辑：字母、数字、-._~ 原样保留；其余字节写成 %HH。
*/
static std::string encodePathSegment(const std::string &name)
{
    const char *hex = "0123456789ABCDEF";
    std::string output;
    size_t i = 0;
    while (i < name.size())
    {
        unsigned char c = static_cast<unsigned char>(name[i]);
        bool unreserved = (c >= 'A' && c <= 'Z')
            || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
            || c == '-' || c == '.' || c == '_' || c == '~';
        if (unreserved)
            output += static_cast<char>(c);
        else
        {
            output += '%';
            output += hex[(c >> 4) & 0x0F];
            output += hex[c & 0x0F];
        }
        ++i;
    }
    return output;
}


/*
函数：handleIndex
用途：按配置顺序寻找目录首页；没有可用首页时进入 autoindex。
参数来源：route 来自 handleGet()；requestPath 是用户可见 URL path；closeConnection 继承原 Request。
变量说明：response 是错误分支结果；i 遍历 index 候选；indexPath/fd/fileInfo 检查每个候选。
实现逻辑：
    1. joinPaths(targetPath,index[i]) 并 open。
    2. 不存在时继续下一个候选；权限等错误映射 403，其他错误 500。
    3. fstat 失败返回 500；不是普通文件则继续。
    4. 命中普通文件后调用 createIndexResponse()。
    5. 全部失败时调用 handleAutoIndex()。
*/
Response handleIndex(const EffectiveRoute &route,
                            const std::string &requestPath,
                            bool closeConnection)
{
    Response response(closeConnection);
    size_t i = 0;
    while (i < route.index.size())
    {
        std::string indexPath = joinPaths(route.targetPath, route.index[i]);
        int fd = open(indexPath.c_str(), O_RDONLY);
        if (fd < 0)
        {
            if (errno == ENOENT || errno == ENOTDIR)
            {
                ++i;
                continue;
            }
            if (errno == EACCES || errno == EPERM || errno == ELOOP
                || errno == ENAMETOOLONG)
                response.createResponse(403, "", route.server->error_pages);
            else
                response.createResponse(500, "", route.server->error_pages);
            return response;
        }

        struct stat fileInfo;
        if (fstat(fd, &fileInfo) != 0)
        {
            close(fd);
            response.createResponse(500, "", route.server->error_pages);
            return response;
        }
        if (!S_ISREG(fileInfo.st_mode))
        {
            close(fd);
            ++i;
            continue;
        }
        return createIndexResponse(fd, indexPath, closeConnection,
            route.server->error_pages);
    }
    return handleAutoIndex(route, requestPath, closeConnection);
}

/*
函数：createIndexResponse
用途：从已打开的 index 文件按块读取 body，并按实际文件扩展名设置 MIME。
参数来源：fd/indexPath 来自 handleIndex() 命中的候选；closeConnection 来自原 Request；errorPages 来自 server。
变量说明：response 是成功结果；bufferSize/buffer/bytesRead 负责循环读取。
实现逻辑：
    1. 创建 200 Response 并设置真实 MIME。
    2. 循环 read，每块按实际长度 appendBody。
    3. 关闭 fd。
    4. read 失败时返回新的 500 Response，否则返回成功响应。
*/
static Response createIndexResponse(int fd,
                                    const std::string &indexPath,
                                    bool closeConnection,
                                    const Response::ErrorPageMap &errorPages)
{
    Response response(closeConnection);
    response.setStatus(200);
    response.setHeader("Content-Type", getMimeType(indexPath));

    const size_t bufferSize = 64 * 1024;
    std::vector<char> buffer(bufferSize);
    ssize_t bytesRead = 0;
    while ((bytesRead = read(fd, &buffer[0], bufferSize)) > 0)
        response.appendBody(&buffer[0], static_cast<size_t>(bytesRead));
    close(fd);
    if (bytesRead < 0)
    {
        Response errorResponse(closeConnection);
        errorResponse.createResponse(500, "", errorPages);
        return errorResponse;
    }
    return response;
}

/*
函数：handleAutoIndex
用途：根据 route.autoindex 决定返回 403 或生成目录列表。
参数来源：route/requestPath/closeConnection 来自 handleIndex()。
变量说明：关闭时局部 response 保存 403；开启时无额外变量。
实现逻辑：autoindex=false 生成错误响应；true 调用 createAutoIndexResponse()。
*/
static Response handleAutoIndex(const EffectiveRoute &route,
                                const std::string &requestPath,
                                bool closeConnection)
{
    if (!route.autoindex)
    {
        Response response(closeConnection);
        response.createResponse(403,
            "AutoIndex Not allowed by Default (add rule in config file)",
            route.server->error_pages);
        return response;
    }
    return createAutoIndexResponse(route, requestPath, closeConnection);
}

/*
函数：createAutoIndexResponse
用途：读取目录项并生成不泄露磁盘路径、链接经过 URL 编码的 HTML 列表。
参数来源：route 提供真实目录 targetPath；requestPath 是用户 URL；closeConnection 继承原 Request。
变量说明：directory 是 DIR*；displayPath/body 构建页面；entry/pathInfo/currentPath/isDirectory/displayName/href 处理每个目录项。
实现逻辑：
    1. opendir 失败返回 500。
    2. 页面标题只使用 requestPath，并做 HTML escape。
    3. readdir 跳过 .；stat 每项，只保留普通文件和目录。
    4. 显示文本做 HTML escape，href 先按 path segment URL 编码再 escape。
    5. 关闭目录，设置 200 text/html 和完整 body。
*/
static Response createAutoIndexResponse(const EffectiveRoute &route,
                                        const std::string &requestPath,
                                        bool closeConnection)
{
    Response response(closeConnection);
    DIR *directory = opendir(route.targetPath.c_str());
    if (directory == NULL)
    {
        response.createResponse(500, "", route.server->error_pages);
        return response;
    }

    std::string displayPath = requestPath.empty() ? "/" : requestPath;
    if (displayPath[displayPath.size() - 1] != '/')
        displayPath += '/';
    std::string body = "<!DOCTYPE html>\n<html>\n<head><title>Index of ";
    body += escapeHtml(displayPath);
    body += "</title></head>\n<body>\n<h1>Index of ";
    body += escapeHtml(displayPath);
    body += "</h1>\n<ul>\n";

    struct dirent *entry = NULL;
    while ((entry = readdir(directory)) != NULL)
    {
        std::string name = entry->d_name;
        if (name == ".")
            continue;
        struct stat pathInfo;
        std::string currentPath = joinPaths(route.targetPath, name);
        if (stat(currentPath.c_str(), &pathInfo) != 0)
            continue;
        bool isDirectory = S_ISDIR(pathInfo.st_mode);
        if (!isDirectory && !S_ISREG(pathInfo.st_mode))
            continue;
        std::string displayName = isDirectory ? name + "/" : name;
        std::string href = encodePathSegment(name);
        if (isDirectory)
            href += "/";
        body += "<li><a href=\"";
        body += escapeHtml(href);
        body += "\">";
        body += escapeHtml(displayName);
        body += "</a></li>\n";
    }
    closedir(directory);
    body += "</ul>\n</body>\n</html>\n";

    response.setStatus(200);
    response.setHeader("Content-Type", "text/html");
    response.setBody(body);
    return response;
}

