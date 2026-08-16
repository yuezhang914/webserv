#include "RequestHandlerInternal.hpp"
#include <cerrno>
#include <sys/stat.h>
#include <cstdio>

// Checks whether a file name is unsafe.
static bool badFileName(const std::string &name)
{
    if (name.empty() || name == "." || name == ".." || name.find('/') != std::string::npos || name.find('\\') != std::string::npos)
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

// Handles delete.
Response handleDelete(const Request &request, const EffectiveRoute &route)
{
    FileOperation file(request);
    struct stat targetInfo;
    file.filePath = route.targetPath;
    if (stat(file.filePath.c_str(), &targetInfo) != 0)
    {
        int errorNumber = errno;
        file.createDeleteResponse(errorNumber, route.server->error_pages);
        return file.response;
    }
    if (!S_ISREG(targetInfo.st_mode))
    {
        file.response.createResponse(403, "Delete Is Only Allowed on Regular Files", route.server->error_pages);
        return file.response;
    }
    if (file.getFileName(route) == FILE_OPERATION_ERROR)
        return file.response;
    if (std::remove(file.filePath.c_str()) != 0)
    {
        int errorNumber = errno;
        file.createDeleteResponse(errorNumber, route.server->error_pages);
        return file.response;
    }
    file.response.createResponse(204, "", route.server->error_pages);
    return file.response;
}

// Creates delete response.
void FileOperation::createDeleteResponse(int errorNumber, const Response::ErrorPageMap &errorPages)
{
    switch (errorNumber)
    {
    case ENOENT:
        response.createResponse(404, "Resource was not found", errorPages);
        break;
    case EACCES:
        response.createResponse(403, "Operation is not allowed", errorPages);
        break;
    case ENOTDIR:
        response.createResponse(404, "Path invalid", errorPages);
        break;
    case EROFS:
        response.createResponse(403, "Server cannot modify resource", errorPages);
        break;
    case EPERM:
        response.createResponse(403, "Operation not permitted", errorPages);
        break;
    case EISDIR:
        response.createResponse(403, "Delete not allowed on directory", errorPages);
        break;
    case ENAMETOOLONG:
        response.createResponse(414, "", errorPages);
        break;
    case EBUSY:
        response.createResponse(423, "Resource is locked or in use", errorPages);
        break;
    default:
        response.createResponse(500, "Unexpected failure", errorPages);
        break;
    }
}

// Returns file name.
int FileOperation::getFileName(const EffectiveRoute &route)
{
    size_t slash = route.targetPath.find_last_of('/');
    std::string name = slash == std::string::npos ? route.targetPath : route.targetPath.substr(slash + 1);
    if (badFileName(name))
    {
        response.createResponse(400, "A valid filename must be provided in the URI", route.server->error_pages);
        return FILE_OPERATION_ERROR;
    }
    fileName = name;
    return FILE_OPERATION_OK;
}
