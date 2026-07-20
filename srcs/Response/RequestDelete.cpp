/*
文件：srcs/Response/RequestDelete.cpp
用途：验证 DELETE 目标文件名、执行普通文件删除，并把 stat/unlink errno 映射成稳定 HTTP 响应。
拆分说明：删除相关函数从原 RequestHandler.cpp 原样移动，不允许删除目录或非普通文件。
*/
#include "RequestHandlerInternal.hpp"

#include <cerrno>
#include <sys/stat.h>
#include <unistd.h>

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

