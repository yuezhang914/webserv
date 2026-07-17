/*
文件：srcs/Response/RequestHandler.cpp
用途：处理 GET、POST、DELETE 请求，以及目录的 index、autoindex、文件上传和删除操作。
清理说明：Response 的状态、headers 和 body 只能通过成员函数修改；本文件不重复检查 RequestParser 已保证的 HTTP version 和 body framing 语法。
*/

/*
使用的外部库说明：
<cerrno> : 用于获取系统底层操作失败时的具体错误码（如ENOENT代表文件不存在）。
<dirent.h> : 用于目录操作，包含打开目录(opendir)、读取目录内容(readdir)和关闭目录(closedir)的功能。
<fcntl.h> : 提供文件控制选项，比如以只读方式打开文件的标志位(O_RDONLY)。
<fstream> : 提供文件输入输出流，用于读取文件(ifstream)和写入文件(ofstream)。
<sstream> : 提供字符串流操作，用于高效地拼接字符串(ostringstream)。
<sys/stat.h> : 用于获取和检查文件或目录的属性（例如判断目标是普通文件还是文件夹）。
<unistd.h> : 提供底层的操作系统接口，如读取文件(read)、关闭文件(close)、检查权限(access)和删除文件(unlink)。
<vector> : 提供动态数组功能，在此文件中主要用作读取大文件时的内存缓冲区。
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

struct FileOperation
{
    std::string fileName;
    std::string filePath;
    Response response;

    /*
    函数用途：FileOperation 结构体的构造函数，用于初始化文件操作的状态对象。
    参数与变量：
    - request (传入参数)：解析后的客户端请求对象。
    实现逻辑：
    1. 通过初始化列表将 fileName 和 filePath 字符串清空。
    2. 使用传入的 request 对象初始化内部的 response 成员变量，继承请求的连接状态。
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
函数用途：将字符串中的所有英文字母转换为小写，用于进行不区分大小写的比较。
参数与变量：
- value (传入参数)：需要转换的原始字符串。
- result (局部变量)：用于存储转换结果的字符串副本。
- i (局部变量)：用于遍历字符串字符的索引。
实现逻辑：
1. 拷贝一份传入的字符串。
2. 循环遍历每个字符，若其 ASCII 值在大写字母 'A' 到 'Z' 之间，则加上 32（即 'a' - 'A'）转换为小写。
3. 返回转换后的字符串副本。
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
函数用途：对文本进行 HTML 转义，防止特殊符号破坏网页结构或导致跨站脚本攻击（XSS）。
参数与变量：
- text (传入参数)：需要转义的原始文本。
- output (局部变量)：存储转义后 HTML 字符串的对象。
- i (局部变量)：遍历索引。
实现逻辑：
1. 逐个字符遍历输入文本。
2. 若遇到 '&', '<', '>', '"', '\'' 这五个特定字符，追加对应的 HTML 实体代码（如 '&lt;'）。
3. 对于其他普通字符，直接追加到输出字符串中。
4. 返回转义完毕的字符串。
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
函数用途：将文件或目录名称编码为符合 URL 规范的路径片段，避免非法字符阻断路由解析。
参数与变量：
- name (传入参数)：需要编码的文件或目录名。
- hex (局部变量)：指向 16 进制字符表的常量指针。
- output (局部变量)：用于存储编码结果的字符串。
- c (局部变量)：当前正在处理的字符的无符号表示。
- unreserved (局部变量)：布尔值，判断当前字符是否属于无需编码的安全字符。
实现逻辑：
1. 遍历名称中的每一个字符。
2. 判断字符是否为字母、数字或特定的安全符号（'-', '.', '_', '~'）。
3. 若安全则直接保留；若不安全，则追加 '%' 并通过位移操作计算出该字符对应的两位十六进制表示。
4. 返回完成 URL 编码的字符串。
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
函数用途：验证文件名是否安全，拦截恶意路径构造或非法控制字符。
参数与变量：
- name (传入参数)：待检查的文件名字符串。
- c (局部变量)：当前处理的单个字符的无符号表示。
实现逻辑：
1. 若文件名为空、为当前目录 "."、为上级目录 ".."，或包含斜杠 '/' 及反斜杠 '\\'，则直接判定为不合法，返回 true。
2. 遍历字符串，若发现 ASCII 值小于 32 的控制字符或值为 127 的删除符，判定为不合法。
3. 若均未命中，判定文件名为合法，返回 false。
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
函数用途：统一路径格式，确保作为目录使用的路径字符串始终以斜杠 '/' 结尾。
参数与变量：
- path (传入参数及返回值)：待处理的路径字符串。
实现逻辑：
1. 检查路径是否非空且最后一个字符不是 '/'。
2. 如果满足条件，在字符串末尾拼接一个 '/'。
3. 返回处理后的路径。
*/
static std::string withEndingSlash(std::string path)
{
    if (!path.empty() && path[path.size() - 1] != '/')
        path += '/';
    return path;
}

/*
函数用途：通过解析文件的扩展名，返回对应的 HTTP 响应 Content-Type (MIME type)。
参数与变量：
- path (传入参数)：完整的文件路径或文件名。
- dot (局部变量)：最后一个点号 '.' 的位置索引。
- extension (局部变量)：提取出并转化为小写的文件后缀名。
实现逻辑：
1. 寻找路径中最后一个 '.' 的位置，如果没找到，默认返回二进制流类型 "application/octet-stream"。
2. 提取 '.' 及之后的字符串并转为小写。
3. 根据硬编码的常见后缀列表进行匹配，返回对应的 MIME 类型。
4. 无法匹配时作为兜底，返回二进制流类型。
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
函数用途：处理针对常规文件的 HTTP GET 请求，将文件内容读取并封装为 Response 对象。
参数与变量：
- request (传入参数)：解析后的 HTTP 请求对象。
- route (传入参数/修改)：当前请求对应的系统路由及文件路径状态。
- response (局部变量)：将要返回的 HTTP 响应对象。
- input (局部变量)：以二进制只读方式打开的目标文件流。
- buffer (局部变量)：用于接收文件流数据的字符串缓冲区。
- content (局部变量)：读取出的全部文件数据。
实现逻辑：
1. 初始化 Response 对象。若路由判断目标是一个目录，将其移交给 handleIndex 函数处理。
2. 尝试打开目标文件的文件流。若失败，生成并返回 500 内部错误响应。
3. 将文件流内容一次性重定向输出到字符串缓冲区中。
4. 关闭文件流。若关闭出错，返回 500 响应。
5. 成功读取后，将响应状态设置为 200，并基于文件扩展名设置 Content-Type。
6. 将文件内容设为响应体，返回该 Response 对象。
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
函数用途：当请求的目标是目录时，按照配置优先级寻找默认的首页文件（如 index.html）。
参数与变量：
- route (传入参数)：包含首页配置列表和目标路径的路由对象。
- requestPath (传入参数)：原始 HTTP 请求的虚拟路径。
- closeConnection (传入参数)：布尔值，指示是否需要在处理完毕后关闭连接。
- response (局部变量)：错误发生时生成的响应对象。
- i (局部变量)：遍历首页配置列表的索引。
- indexPath (局部变量)：尝试拼接出的潜在首页文件的绝对路径。
- fd (局部变量)：调用系统 open() 返回的文件描述符。
- fileInfo (局部变量)：用于存储 fstat() 调用的文件元数据结构。
实现逻辑：
1. 循环遍历配置文件中定义的每一个首页文件名候选者。
2. 将目录路径与文件名拼接，调用系统底层的 open 尝试以只读方式打开。
3. 若打开失败，根据 errno 判断：若是文件不存在或路径无效，跳过并尝试下一个候选者；若是权限不足等错误，直接返回 403 响应；若是其他底层错误，返回 500。
4. 若成功打开，使用 fstat 获取文件信息，判断其是否为常规文件（排除是个同名目录的情况）。若不是，关闭文件描述符并继续尝试下一个。
5. 若确认为常规文件，将文件描述符传递给 createIndexResponse 进行内容读取。
6. 若循环结束仍未找到可用首页，将请求交由 handleAutoIndex 处理（尝试展示目录列表）。
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
函数用途：读取已经通过底层系统调用打开的首页文件，生成成功的 HTTP 响应报文。
参数与变量：
- fd (传入参数)：已经以只读模式打开的文件描述符。
- indexPath (传入参数)：文件的物理路径，用于判断 Content-Type。
- closeConnection (传入参数)：连接状态控制标志。
- errorPages (传入参数)：错误页面配置字典。
- response (局部变量)：封装的 HTTP 响应对象。
- bufferSize (局部常量)：设定的每次读取缓冲区大小（64KB）。
- buffer (局部变量)：基于 vector 分配在堆上的连续内存空间，作为读取缓冲区。
- bytesRead (局部变量)：记录单次系统调用 read() 实际读取到的字节数。
实现逻辑：
1. 将响应状态设为 200，并解析 indexPath 设置对应的 MIME 类型。
2. 使用系统函数 read 循环分块读取文件内容，将读到的数据直接追加到 Response 的数据体中。
3. 读取完毕后调用 close 关闭文件描述符。
4. 若读取过程中发生错误（bytesRead < 0），生成并返回 500 错误响应。
5. 返回完整的 Response 对象。
*/
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

/*
函数用途：前置检查目录列表功能是否被配置允许开启。
参数与变量：
- route (传入参数)：包含 autoindex 开关状态的路由配置。
- requestPath (传入参数)：原始 HTTP 请求的虚拟路径。
- closeConnection (传入参数)：连接状态控制标志。
- response (局部变量)：验证失败时生成的 403 错误响应对象。
实现逻辑：
1. 检查路由对象内的 autoindex 布尔值。
2. 若未开启，生成 403 状态的响应对象，附带文本提示，并返回。
3. 若开启，调用 createAutoIndexResponse 执行实际的目录遍历和 HTML 生成操作。
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
函数用途：读取服务器物理目录内容，动态生成一个可供浏览器浏览和点击的 HTML 网页结构。
参数与变量：
- route (传入参数)：包含目标物理目录路径的路由配置。
- requestPath (传入参数)：原始 HTTP 请求路径，用于网页标题显示。
- closeConnection (传入参数)：连接状态控制标志。
- response (局部变量)：封装生成的 HTTP 响应对象。
- directory (局部变量)：由 opendir 返回的目录流指针。
- displayPath (局部变量)：格式化后用于在 HTML 页面中展示的当前路径。
- body (局部变量)：拼接整个 HTML 网页代码的字符串变量。
- entry (局部变量)：由 readdir 返回的指向单个目录项结构体的指针。
- name (局部变量)：当前遍历到的子文件或子目录的名称。
- pathInfo (局部变量)：用于存储当前子项目 stat() 信息的结构体。
- currentPath (局部变量)：当前子项目的绝对物理路径。
- isDirectory (局部变量)：布尔值，标识当前项目是否为目录。
- displayName (局部变量)：网页上展示的文件名（如果是目录会加上斜杠）。
- href (局部变量)：经过 URL 编码的安全链接地址。
实现逻辑：
1. 调用 opendir 打开目标路径的目录流。若失败则返回 500。
2. 规范化展示路径（缺省补充 '/'）并开始拼接 HTML 的 DOCTYPE、head、title 以及 body 头部的标签。
3. 在 while 循环中调用 readdir 逐个读取目录项。
4. 跳过代表当前目录的 "." 项。
5. 对每个遍历到的项目调用 stat 获取系统信息。忽略无法 stat 或既不是普通文件也不是目录的特殊文件（如设备节点）。
6. 根据类型格式化展示名称，并使用 encodePathSegment 生成安全的 URL href。
7. 将每一项封装为 HTML 列表 `<li>` 和超链接 `<a>` 拼接到 body 变量中。
8. 循环结束，关闭目录流，补全 HTML 闭合标签。
9. 设置响应状态 200，指定类型为 "text/html"，写入生成的 HTML 字符串并返回。
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
函数用途：处理 HTTP POST 请求，将客户端的上传数据作为文件写入服务器磁盘。
参数与变量：
- request (传入参数)：解析完毕包含上传数据的请求对象。
- route (传入参数)：包含根目录及上传路径(upload_path)设定的路由对象。
- file (局部变量)：实例化 FileOperation 结构体，用于统筹文件保存相关的逻辑与状态记录。
- base (局部变量)：判断路由类型，提取实际的基础目录（alias 或是 root）。
- baseDirectory (局部变量)：拼接基础目录和 upload_path 后形成的最终保存目录，并确保带有结尾斜杠。
- directoryInfo (局部变量)：存储 stat() 获取的目录状态信息。
实现逻辑：
1. 实例化文件操作管理对象并拼接出目标存储目录。
2. 使用 stat 检查目标存储目录在物理磁盘上是否存在，且必须是目录类型；否则组装 409 冲突错误并返回。
3. 调用 access 系统函数校验当前程序对该目录是否具备 W_OK（写操作）权限，无权限则返回 403。
4. 依次调用封装好的内部验证与操作：获取合法文件名、校验请求头规范、将请求体数据以独占不覆盖的方式写入磁盘文件。这其中任何一步失败均会直接返回内部携带错误码的响应。
5. 全部验证写入成功后，生成 200 OK（或201）状态，携带成功文本并返回。
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
函数用途：处理 HTTP DELETE 请求，调用系统接口移除物理磁盘上的常规文件。
参数与变量：
- request (传入参数)：解析后的客户端 HTTP 请求。
- route (传入参数)：包含已计算好的物理目标路径 targetPath。
- file (局部变量)：实例化 FileOperation，用于调用公共的方法和存储响应。
- targetInfo (局部变量)：通过 stat() 系统调用获取的目标文件属性。
实现逻辑：
1. 校验目标文件名是否合法，不合法直接返回对应的错误响应。
2. 将路由计算出的物理路径赋给操作对象，并使用 stat 函数查询文件信息。
3. 若 stat 执行失败，根据失败的系统底层错误码映射成 HTTP 响应并返回。
4. 使用 S_ISREG 宏判断目标是否为常规文件，系统安全策略禁止通过 DELETE 请求删除目录，若违规返回 403。
5. 调用底层 unlink 函数执行文件的断链/删除操作，如果失败，映射错误码并返回。
6. 操作成功则生成 204 No Content（无内容）响应报文并返回。
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
函数用途：映射底层的系统级错误码 (errno) 为标准的 HTTP 错误响应代码及信息。
参数与变量：
- errorNumber (传入参数)：底层操作失败时由系统抛出的错误标识值。
- errorPages (传入参数)：配置文件中设定的自定义错误页面字典。
实现逻辑：
1. 使用 switch 匹配具体的系统错误常量。
2. 例如，匹配到 ENOENT（实体不存在）及 ENOTDIR 时触发 404；匹配到 EACCES、EPERM、EROFS 等权限阻拦时触发 403；诸如此类。
3. 对于未知错误情况走 default 分支，统一响应 500 内部服务错误。
4. 每次均调用内部封装好的 createResponse 生成完整的错误响应结构。
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
函数用途：从路由的目标路径字符串中剥离提取出单纯的文件名部分，并验证该文件名的合法性。
参数与变量：
- route (传入参数)：当前连接计算出的物理路由设定。
- slash (局部变量)：记录字符串中最后一个 '/' 的索引位置。
- name (局部变量)：剥离出的纯文件名字符串。
实现逻辑：
1. 利用 find_last_of 寻找目标路径中最后一个斜杠的位置。
2. 如果找不到斜杠，直接将整个路径视为文件名；否则提取斜杠后面的字符串作为文件名。
3. 调用 badFileName() 函数校验该文件名是否存在控制字符及目录逃逸符号。如果不合法，修改状态为 400，返回错误状态标识。
4. 验证通过后将其存储在当前对象的 fileName 变量中，返回正确标志位。
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
函数用途：对上传请求体进行防御性检查，确保请求携带了被支持的类型，并指明了正文的长度边界。
参数与变量：
- request (传入参数)：请求对象字典。
- route (传入参数)：用于发生错误时获取错误页面配置。
- contentType (局部变量)：从头信息中提取出的数据格式字符串。
- headerValue (局部变量)：用于接收长度相关头的临时存放变量。
- hasContentLength (局部变量)：布尔值，是否包含 "content-length"。
- hasTransferEncoding (局部变量)：布尔值，是否包含 "transfer-encoding"。
实现逻辑：
1. 尝试获取 "content-type" 请求头。如果存在，调用 checkContentType 确认服务器是否支持此格式。不支持则组装 415 响应并中止返回错误标识。
2. 尝试获取 "content-length" 和 "transfer-encoding" 字段。
3. 按照 HTTP 协议，如果客户端未以这两种机制之一显式声明数据长度边界，拒绝接收，返回 411 响应及错误标识。
4. 通过检查则返回正常标志位。
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
函数用途：执行真正的文件输出流建立及二进制内容落地写入磁盘。
参数与变量：
- body (传入参数)：待写入的请求正文字符串数据。
- baseDirectory (传入参数)：合法的上传目标根目录。
- errorPages (传入参数)：自定义错误页面配置。
- output (局部变量)：执行底层写入操作的文件输出流 ofstream 对象。
实现逻辑：
1. 调用 generateUniqueFilename 产生一个能保证不覆盖旧数据的全新文件名完整路径。
2. 实例化 ofstream，以 ios::binary 模式打开该文件进行输出。
3. 如果文件无法打开，构建 500 响应报文并返回。
4. 调用 write() 将内存中的字符串数据成块写入磁盘。
5. 写完检查流的状态位，若出现落盘错误，构建 500 响应。
6. 安全地 close() 关闭文件流。关闭出错同样抛出 500。
7. 所有 I/O 操作均无误则返回成功标志。
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
函数用途：简易的文件探测工具，检查特定的完整路径在底层操作系统中是否已存在。
参数与变量：
- fullPath (传入参数)：用于检查存在与否的绝对或相对文件路径。
- pathInfo (局部变量)：接收 stat 函数探测信息的结构体。
实现逻辑：
1. 直接调用 stat 函数。
2. 若其返回 0，代表路径存在，返回 true；反之返回 false。
*/
bool FileOperation::fileExists(const std::string &fullPath) const
{
    struct stat pathInfo;
    return stat(fullPath.c_str(), &pathInfo) == 0;
}

/*
函数用途：保证上传逻辑不覆盖服务器中既有文件，动态在文件名中追加序号生成唯一名称。
参数与变量：
- baseDirectory (传入参数)：目标存储目录字符串。
- name (局部变量)：拆分出的不带扩展名的基础名称。
- extension (局部变量)：拆分出的保留原始点号的文件扩展名。
- dot (局部变量)：文件名中最后一个 '.' 的索引位置。
- fullPath (局部变量)：拼接验证所用的完整目标路径。
- counter (局部变量)：自增循环序号，起步为 1。
- candidate (局部变量)：通过 ostringstream 动态格式化数字及文本的中间对象。
实现逻辑：
1. 寻找文件名称中最后出现的 '.' 并分割，取得主体 "name" 及扩展名 "extension"（例如将 "data.txt" 拆解为 "data" 和 ".txt"）。
2. 将目录名与原始文件名拼接后，置于 while 循环中调用 fileExists 探测其存在性。
3. 如果发生重名冲突，利用 ostringstream 将 基础名、下划线、自增序号 counter 及扩展名重新组合（如生成 "data_1.txt"）。
4. 更新测试路径并在下一次循环中测试，直到探测不到已有文件时，退出循环并返回该条无冲突路径。
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
函数用途：解析并过滤客户端发起上传时声明的 Content-Type 字段，仅放行被支持的数据结构。
参数与变量：
- contentType (传入参数)：请求报头中原始的类型字段字符串。
- value (局部变量)：提取主类型且小写化后的操作字符串。
- semicolon (局部变量)：字符串中分号的位置索引，用于剥离后附参数（如 charset 等）。
- begin / end (局部变量)：用于修剪字符串两端空白的索引指针。
实现逻辑：
1. 转化为小写后，寻找分号并进行截断，舍弃附加在主类型后面的参数说明。
2. 建立两端索引，过滤并剔除剩余字符串两端的空格及制表符。
3. 将精简后的纯净类型字符串与系统支持清单（二进制、表单数据、JSON及纯文本）进行强匹配比对。
4. 匹配成功返回 OK；如果客户端提交的是目前尚未具备解析能力的复杂格式（如 multipart 表单），则拒绝之并返回 ERROR 标识。
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
函数用途：将代表 HTTP 方法的普通字符串转换为系统内部的强类型枚举，方便在逻辑判断中使用。
参数与变量：
- method (传入参数)：包含 HTTP 方法名称的字符串对象（如 "GET"）。
实现逻辑：
1. 依次将传入的字符串与框架支持的核心方法字符串（"GET"、"POST"、"DELETE"）进行全等比较。
2. 成功匹配时，返回预定义好的强类型动作枚举值。
3. 不在支持列表内的方法，统一定义为不支持（ACTION_UNSUPPORTED），以此作为后续触发 501 响应的信号源。
*/
RequestAction requestActionFromMethod(const std::string &method)
{
    if (method == "GET") return ACTION_GET;
    if (method == "POST") return ACTION_POST;
    if (method == "DELETE") return ACTION_DELETE;
    return ACTION_UNSUPPORTED;
}

/*
函数用途：验证当前请求所意图执行的 HTTP 动作是否被配置文件里该路由的白名单策略所允许。
参数与变量：
- action (传入参数)：代表当前请求动作的强类型枚举值。
- allowMethods (传入参数)：从服务器和路由配置项中合并而来的允许方法字符串集合(set 容器)。
实现逻辑：
1. 针对传入枚举所属的分支（GET、POST 或 DELETE）进行判定。
2. 利用 set 容器的 find() 函数，以时间复杂度 O(log n) 查找对应的大写方法字符串。
3. 若查找的迭代器不等于容器的 end()，说明找到了对应权限，返回 true。找不到或不在上述三个核心动作分支内则返回 false 阻断请求。
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