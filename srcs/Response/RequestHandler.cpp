/*
文件：srcs/Response/RequestHandler.cpp
用途：处理 GET、POST、DELETE、目录 index、autoindex、上传和删除。
封装说明：Response 的状态、headers 和 body 只能通过成员函数修改；本文件不重复检查 RequestParser 已保证的 HTTP version 和 body framing 语法。
*/
#include "RequestHandler.hpp"

#include <cerrno>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

static Response handleIndex(const EffectiveRoute &route,
    const std::string &requestPath, bool closeConnection);
static Response createIndexResponse(int fd, const std::string &indexPath,
    bool closeConnection, const Response::ErrorPageMap &errorPages);
static Response handleAutoIndex(const EffectiveRoute &route,
    const std::string &requestPath, bool closeConnection);
static Response createAutoIndexResponse(const EffectiveRoute &route,
    const std::string &requestPath, bool closeConnection);

enum FileOperationStatus
{
    FILE_OPERATION_OK = 0,
    FILE_OPERATION_ERROR = -1
};

/*
结构体：FileOperation
用途：只在本实现文件内部保存 POST/DELETE 文件操作的临时状态和错误 Response。
清理说明：不再保存由 Request.body.size() 重复得到的 length；上传目录和唯一文件名都由同一条路径计算。
*/
struct FileOperation
{
    std::string fileName;
    std::string filePath;
    Response response;

    explicit FileOperation(const Request &request)
        : fileName(), filePath(), response(request) {}

    int getFileName(const EffectiveRoute &route);
    int validateUploadRequest(const Request &request,
        const EffectiveRoute &route);
    int createFile(const std::string &body,
        const std::string &baseDirectory,
        const Response::ErrorPageMap &errorPages);
    bool fileExists(const std::string &fullPath) const;
    std::string generateUniqueFilename(
        const std::string &baseDirectory) const;
    void createDeleteResponse(int errorNumber,
        const Response::ErrorPageMap &errorPages);
    int checkContentType(const std::string &contentType) const;
};

/* 把 ASCII 字母转成小写，供 MIME type 和 Content-Type 比较使用。 */
static std::string toLowerAscii(const std::string &value)
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

/* 对 autoindex 中的显示路径和文件名做 HTML 转义。 */
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

/* 把目录项名称编码为 URL path segment，避免 ?、#、空格等改变链接含义。 */
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

/* 拒绝空名、点目录、路径分隔符和控制字符；普通文件名内部的连续点允许。 */
static bool badFileName(const std::string &name)
{
    if (name.empty() || name == "." || name == ".."
        || name.find('/') != std::string::npos
        || name.find('\\') != std::string::npos)
        return true;
    size_t i = 0;
    while (i < name.size())
    {
        unsigned char c = static_cast<unsigned char>(name[i]);
        if (c < 32 || c == 127)
            return true;
        ++i;
    }
    return false;
}

/* 确保目录路径以斜杠结尾。 */
static std::string withEndingSlash(std::string path)
{
    if (!path.empty() && path[path.size() - 1] != '/')
        path += '/';
    return path;
}

/* 根据真实文件路径扩展名返回常用 MIME type。 */
static std::string getMimeType(const std::string &path)
{
    size_t dot = path.find_last_of('.');
    if (dot == std::string::npos)
        return "application/octet-stream";
    std::string extension = toLowerAscii(path.substr(dot));
    if (extension == ".html" || extension == ".htm") return "text/html";
    if (extension == ".css") return "text/css";
    if (extension == ".js") return "application/javascript";
    if (extension == ".json") return "application/json";
    if (extension == ".txt") return "text/plain";
    if (extension == ".png") return "image/png";
    if (extension == ".jpg" || extension == ".jpeg") return "image/jpeg";
    if (extension == ".gif") return "image/gif";
    if (extension == ".svg") return "image/svg+xml";
    if (extension == ".pdf") return "application/pdf";
    return "application/octet-stream";
}

/* 返回静态普通文件；目录交给 index/autoindex 分支。 */
Response handleGet(const Request &request, EffectiveRoute &route)
{
    Response response(request);
    if (route.isDir)
        return handleIndex(route, request.getPath(),
            response.shouldCloseConnection());

    std::ifstream input(route.targetPath.c_str(), std::ios::binary);
    if (!input)
    {
        response.createResponse(500, "Failed to open file",
            route.server->error_pages);
        return response;
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    std::string content = buffer.str();
    input.close();
    if (!input)
    {
        response.createResponse(500, "Error closing file",
            route.server->error_pages);
        return response;
    }

    response.setStatus(200);
    response.setHeader("Content-Type", getMimeType(route.targetPath));
    response.setBody(content);
    return response;
}

/* 依次尝试多个 index 候选；都不可用时进入 autoindex。 */
static Response handleIndex(const EffectiveRoute &route,
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

/* 从已打开的 index 文件读取二进制内容，并按真实 index 文件扩展名设置 MIME。 */
static Response createIndexResponse(int fd, const std::string &indexPath,
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

/* autoindex 关闭时返回 403；开启时生成目录列表。 */
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

/* 读取目录项；页面显示 URL path，链接使用 URL 编码，不暴露磁盘真实路径。 */
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

/*
函数：handlePost
用途：验证真正的 upload_path 和 Content-Type，把 RequestParser 已还原的 body 写入唯一文件名。
*/
Response handlePost(const Request &request, const EffectiveRoute &route)
{
    FileOperation file(request);
    std::string base = route.use_alias ? route.alias : route.root;
    std::string baseDirectory = withEndingSlash(
        joinPaths(base, route.upload_path));

    struct stat directoryInfo;
    if (baseDirectory.empty()
        || stat(baseDirectory.c_str(), &directoryInfo) != 0
        || !S_ISDIR(directoryInfo.st_mode))
    {
        file.response.createResponse(409,
            "Upload Directory: " + baseDirectory + " Does not Exist",
            route.server->error_pages);
        return file.response;
    }
    if (access(baseDirectory.c_str(), W_OK) != 0)
    {
        file.response.createResponse(403,
            "Upload Directory: " + baseDirectory + " is not Writable",
            route.server->error_pages);
        return file.response;
    }
    if (file.getFileName(route) == FILE_OPERATION_ERROR
        || file.validateUploadRequest(request, route) == FILE_OPERATION_ERROR
        || file.createFile(request.getBody(), baseDirectory,
            route.server->error_pages) == FILE_OPERATION_ERROR)
        return file.response;

    file.response.createResponse(200, "File Created",
        route.server->error_pages);
    return file.response;
}

/* 删除普通文件；目录和其他非普通文件明确拒绝。 */
Response handleDelete(const Request &request, const EffectiveRoute &route)
{
    FileOperation file(request);
    if (file.getFileName(route) == FILE_OPERATION_ERROR)
        return file.response;

    file.filePath = route.targetPath;
    struct stat targetInfo;
    if (stat(file.filePath.c_str(), &targetInfo) != 0)
    {
        file.createDeleteResponse(errno, route.server->error_pages);
        return file.response;
    }
    if (!S_ISREG(targetInfo.st_mode))
    {
        file.response.createResponse(403,
            "Delete Is Only Allowed on Regular Files",
            route.server->error_pages);
        return file.response;
    }
    if (unlink(file.filePath.c_str()) != 0)
    {
        file.createDeleteResponse(errno, route.server->error_pages);
        return file.response;
    }
    file.response.createResponse(204, "", route.server->error_pages);
    return file.response;
}

/* 把 stat/unlink 的 errno 映射成稳定 HTTP response。 */
void FileOperation::createDeleteResponse(int errorNumber,
    const Response::ErrorPageMap &errorPages)
{
    switch (errorNumber)
    {
        case ENOENT: response.createResponse(404, "Resource was not found", errorPages); break;
        case EACCES: response.createResponse(403, "Operation is not allowed", errorPages); break;
        case ENOTDIR: response.createResponse(404, "Path invalid", errorPages); break;
        case EROFS: response.createResponse(403, "Server cannot modify resource", errorPages); break;
        case EPERM: response.createResponse(403, "Operation not permitted", errorPages); break;
        case EISDIR: response.createResponse(403, "Delete not allowed on directory", errorPages); break;
        case ENAMETOOLONG: response.createResponse(414, "", errorPages); break;
        case EBUSY: response.createResponse(423, "Resource is locked or in use", errorPages); break;
        default: response.createResponse(500, "Unexpected failure", errorPages); break;
    }
}

/* 从真实目标路径提取安全文件名。 */
int FileOperation::getFileName(const EffectiveRoute &route)
{
    size_t slash = route.targetPath.find_last_of('/');
    std::string name = slash == std::string::npos
        ? route.targetPath : route.targetPath.substr(slash + 1);
    if (badFileName(name))
    {
        response.createResponse(400,
            "A valid filename must be provided in the URI",
            route.server->error_pages);
        return FILE_OPERATION_ERROR;
    }
    fileName = name;
    return FILE_OPERATION_OK;
}

/* 检查上传 Content-Type，并确认 RequestParser 已使用明确 body framing。 */
int FileOperation::validateUploadRequest(const Request &request,
                                         const EffectiveRoute &route)
{
    std::string contentType;
    if (request.getHeader("content-type", contentType)
        && checkContentType(contentType) == FILE_OPERATION_ERROR)
    {
        response.createResponse(415, "Unsupported Content-Type",
            route.server->error_pages);
        return FILE_OPERATION_ERROR;
    }

    std::string headerValue;
    bool hasContentLength = request.getHeader("content-length", headerValue);
    bool hasTransferEncoding = request.getHeader("transfer-encoding",
        headerValue);
    if (!hasContentLength && !hasTransferEncoding)
    {
        response.createResponse(411, "", route.server->error_pages);
        return FILE_OPERATION_ERROR;
    }
    return FILE_OPERATION_OK;
}

/* 把 body 以二进制方式写入不覆盖旧文件的目标路径。 */
int FileOperation::createFile(const std::string &body,
                              const std::string &baseDirectory,
                              const Response::ErrorPageMap &errorPages)
{
    filePath = generateUniqueFilename(baseDirectory);
    std::ofstream output(filePath.c_str(), std::ios::binary);
    if (!output)
    {
        response.createResponse(500, "Outfile could not be opened",
            errorPages);
        return FILE_OPERATION_ERROR;
    }
    output.write(body.data(), static_cast<std::streamsize>(body.size()));
    if (!output)
    {
        response.createResponse(500, "Error while writing file",
            errorPages);
        return FILE_OPERATION_ERROR;
    }
    output.close();
    if (!output)
    {
        response.createResponse(500, "Error while closing file",
            errorPages);
        return FILE_OPERATION_ERROR;
    }
    return FILE_OPERATION_OK;
}

bool FileOperation::fileExists(const std::string &fullPath) const
{
    struct stat pathInfo;
    return stat(fullPath.c_str(), &pathInfo) == 0;
}

/* 现有文件不覆盖，依次生成 name_1.ext、name_2.ext。 */
std::string FileOperation::generateUniqueFilename(
    const std::string &baseDirectory) const
{
    std::string name = fileName;
    std::string extension;
    size_t dot = fileName.find_last_of('.');
    if (dot != std::string::npos)
    {
        name = fileName.substr(0, dot);
        extension = fileName.substr(dot);
    }

    std::string fullPath = baseDirectory + fileName;
    int counter = 1;
    while (fileExists(fullPath))
    {
        std::ostringstream candidate;
        candidate << baseDirectory << name << "_" << counter << extension;
        fullPath = candidate.str();
        ++counter;
    }
    return fullPath;
}

/* 当前上传实现接受的 media type；multipart 尚未解析，因此明确拒绝。 */
int FileOperation::checkContentType(const std::string &contentType) const
{
    std::string value = toLowerAscii(contentType);
    size_t semicolon = value.find(';');
    if (semicolon != std::string::npos)
        value = value.substr(0, semicolon);
    size_t begin = 0;
    while (begin < value.size()
        && (value[begin] == ' ' || value[begin] == '\t'))
        ++begin;
    size_t end = value.size();
    while (end > begin
        && (value[end - 1] == ' ' || value[end - 1] == '\t'))
        --end;
    value = value.substr(begin, end - begin);
    if (value == "application/octet-stream"
        || value == "application/x-www-form-urlencoded"
        || value == "application/json" || value == "text/plain")
        return FILE_OPERATION_OK;
    return FILE_OPERATION_ERROR;
}

RequestAction requestActionFromMethod(const std::string &method)
{
    if (method == "GET") return ACTION_GET;
    if (method == "POST") return ACTION_POST;
    if (method == "DELETE") return ACTION_DELETE;
    return ACTION_UNSUPPORTED;
}

bool isMethodAllowed(RequestAction action,
                     const std::set<std::string> &allowMethods)
{
    if (action == ACTION_GET)
        return allowMethods.find("GET") != allowMethods.end();
    if (action == ACTION_POST)
        return allowMethods.find("POST") != allowMethods.end();
    if (action == ACTION_DELETE)
        return allowMethods.find("DELETE") != allowMethods.end();
    return false;
}
