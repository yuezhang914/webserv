/*
文件：srcs/Response/RequestHandler.cpp
用途：处理普通 GET、POST、DELETE，以及目录 index、autoindex、上传文件名和删除 errno 映射。
边界：CGI 已在 Response::buildResponse() 中先于普通 method handler 分流，本文件不会执行脚本；也不重复解析 HTTP version、URI、Content-Length 或 chunked framing。
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
用途：只在本实现文件内保存一次 POST/DELETE 文件操作的文件名、真实路径和正在构造的 Response。
来源：handlePost()/handleDelete() 根据当前 Request 创建。
清理说明：不再保存与 request.getBody().size() 重复的 length，也不暴露到公共头文件。
*/
struct FileOperation
{
    std::string fileName;
    std::string filePath;
    Response response;

    /*
    函数：FileOperation::FileOperation
    用途：创建与当前 Request 连接策略一致的文件操作临时对象。
    参数来源：request 来自 handlePost()/handleDelete()。
    变量说明：fileName/filePath 初始化为空；response 调用 Response(request) 继承 Connection: close。
    实现逻辑：只初始化状态，不读写文件。
    */
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

/*
函数：toLowerAscii
用途：为 MIME 扩展名和 Content-Type 比较生成 ASCII 小写副本。
参数来源：value 来自文件扩展名或 Request 的 Content-Type header。
变量说明：result 是可修改副本；i 是字符下标。
实现逻辑：遍历全部字符，只把 A-Z 改为 a-z，其他字节保持不变。
*/
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
函数：badFileName
用途：拒绝不能安全作为单个上传/删除文件名的字符串。
参数来源：name 由 getFileName() 从 route.targetPath 最后一个 / 后提取。
变量说明：i/c 逐字节检查控制字符。
实现逻辑：空名、.、..、含 / 或反斜杠直接拒绝；再拒绝控制字符和 DEL；普通 a..b.txt 允许。
*/
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

/*
函数：withEndingSlash
用途：确保上传基础目录以 / 结尾，便于后续直接拼文件名。
参数来源：path 来自 joinPaths(root/alias, upload_path)。
变量说明：path 按值传入，可直接修改。
实现逻辑：非空且末尾不是 / 时追加 /，然后返回。
*/
static std::string withEndingSlash(std::string path)
{
    if (!path.empty() && path[path.size() - 1] != '/')
        path += '/';
    return path;
}

/*
函数：getMimeType
用途：根据最终真实文件路径扩展名选择 Content-Type。
参数来源：path 来自 route.targetPath 或实际命中的 indexPath。
变量说明：dot 是最后一个点位置；extension 是小写扩展名。
实现逻辑：没有扩展名返回 application/octet-stream；匹配常用类型；未知类型回退二进制流。
*/
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

/*
函数：handleGet
用途：返回普通静态文件，或把目录交给 index/autoindex 处理。
参数来源：request 来自 buildResponse()；route 已完成配置合并、targetPath 构造和 GET stat 验证。
变量说明：response 保存结果；input/buffer/content 读取普通文件。
实现逻辑：
    1. route.isDir 时调用 handleIndex()，并传递 URL path 和连接策略。
    2. 普通文件以 binary 模式打开，失败返回 500。
    3. 读完整内容并确认关闭流无错误。
    4. 设置 200、真实 MIME 和二进制 body。
*/
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

/*
函数：handlePost
用途：把 RequestParser 已还原的 body 写入配置 upload_path 下的唯一文件名。
参数来源：request/route 来自 buildResponse() 普通 POST 分支。
变量说明：file 保存临时文件状态；base 选择 root/alias；baseDirectory 是最终上传目录；directoryInfo 用 stat 验证目录。
实现逻辑：
    1. joinPaths(root/alias,upload_path) 并保证末尾 /。
    2. 上传目录不存在/非目录返回 409，不可写返回 403。
    3. 提取 URI 文件名、检查 Content-Type/framing、生成唯一名并写文件。
    4. 任一步失败直接返回 file.response；成功返回 200 File Created。
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

/*
函数：handleDelete
用途：删除 route.targetPath 指向的普通文件，并把错误映射为 HTTP 状态。
参数来源：request/route 来自 buildResponse() DELETE 分支。
变量说明：file 保存临时状态；targetInfo 判断目标类型。
实现逻辑：
    1. 提取并验证文件名。
    2. stat 失败按 errno 生成响应。
    3. 非普通文件返回 403。
    4. unlink 失败按 errno 映射；成功返回无 body 的 204。
*/
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

/*
函数：FileOperation::createDeleteResponse
用途：把 stat/unlink 保存的 errno 映射为稳定 HTTP Response。
参数来源：errorNumber 在失败后立即复制 errno；errorPages 来自 route.server。
变量说明：无额外局部变量；switch 直接写成员 response。
实现逻辑：分别处理不存在、权限、路径、只读文件系统、目录、过长、占用；未知错误返回 500。
*/
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

/*
函数：FileOperation::getFileName
用途：从真实 targetPath 取最后一个 path segment，作为上传文件名或 DELETE 名称。
参数来源：route 来自 handlePost()/handleDelete()，targetPath 已由 EffectiveRoute 生成。
变量说明：slash 是最后一个 / 的位置；name 是提取结果。
实现逻辑：提取名称后调用 badFileName()；非法时写 400 Response 并返回错误，合法时保存 fileName。
*/
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

/*
函数：FileOperation::validateUploadRequest
用途：检查普通上传支持的 media type，并确认 RequestParser 使用了明确 body framing。
参数来源：request/route 来自 handlePost()。
变量说明：contentType 暂存 Content-Type；headerValue 复用读取 CL/TE；两个 bool 表示 framing 字段存在性。
实现逻辑：
    1. 有 Content-Type 时调用 checkContentType()，不支持返回 415。
    2. 检查 Content-Length 或 Transfer-Encoding 至少存在一个。
    3. 二者都没有返回 411；其他情况允许继续。
*/
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

/*
函数：FileOperation::createFile
用途：以二进制方式把 body 写入不覆盖旧文件的唯一目标。
参数来源：body 来自 request.getBody()；baseDirectory 来自配置 upload_path；errorPages 来自 server。
变量说明：filePath 保存 generateUniqueFilename() 结果；output 是二进制 ofstream。
实现逻辑：生成唯一名、打开、按明确长度 write、检查写入、关闭并检查关闭；任一步失败生成 500。
*/
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

/*
函数：FileOperation::fileExists
用途：检查候选上传路径是否已经存在。
参数来源：fullPath 来自 generateUniqueFilename() 当前候选。
变量说明：pathInfo 接收 stat 结果。
实现逻辑：stat 成功返回 true，任何失败返回 false。
*/
bool FileOperation::fileExists(const std::string &fullPath) const
{
    struct stat pathInfo;
    return stat(fullPath.c_str(), &pathInfo) == 0;
}

/*
函数：FileOperation::generateUniqueFilename
用途：避免覆盖已有上传文件，依次生成 name_1.ext、name_2.ext。
参数来源：baseDirectory 已由 handlePost() 验证存在可写；fileName 已由 getFileName() 填充。
变量说明：name/extension 拆分主名和后缀；dot 定位扩展名；fullPath 是候选；counter 是后缀编号；candidate 负责格式化。
实现逻辑：先尝试原名；存在时循环增加 _N，直到 fileExists() 为 false。
*/
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

/*
函数：FileOperation::checkContentType
用途：判断普通原始 body 上传当前支持的 media type；multipart 未解析所以明确拒绝。
参数来源：contentType 来自 Request.getHeader("content-type")。
变量说明：value 是小写副本；semicolon 去参数；begin/end trim OWS。
实现逻辑：去掉 ; 后参数和两端 OWS，只允许 octet-stream、urlencoded、json、text/plain。
*/
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

/*
函数：requestActionFromMethod
用途：把 Request.getMethod() 字符串映射为 Response 模块内部 enum。
参数来源：method 来自 buildResponse() 的 Request。
变量说明：无局部变量。
实现逻辑：GET/POST/DELETE 分别返回对应动作，其他合法 token 返回 ACTION_UNSUPPORTED。
*/
RequestAction requestActionFromMethod(const std::string &method)
{
    if (method == "GET") return ACTION_GET;
    if (method == "POST") return ACTION_POST;
    if (method == "DELETE") return ACTION_DELETE;
    return ACTION_UNSUPPORTED;
}

/*
函数：isMethodAllowed
用途：检查已经实现的动作是否出现在 EffectiveRoute 最终 allow_methods 中。
参数来源：action 来自 requestActionFromMethod()；allowMethods 来自 server/location 合并。
变量说明：无局部变量。
实现逻辑：按动作查找对应字符串；未知动作返回 false。
*/
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
