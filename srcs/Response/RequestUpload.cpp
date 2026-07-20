/*
文件：srcs/Response/RequestUpload.cpp
用途：验证普通 POST 上传请求、选择上传目录、生成不覆盖旧文件的唯一名称，并把二进制 body 写入文件。
拆分说明：上传相关函数从原 RequestHandler.cpp 原样移动；multipart 仍明确不在本模块解析。
*/
#include "RequestHandlerInternal.hpp"

#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

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
    std::string value = requestHandlerToLowerAscii(contentType);
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

