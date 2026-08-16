#include "RequestHandlerInternal.hpp"
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

// Adds a final slash to a path when it is missing.
static std::string withEndingSlash(std::string path)
{
    if (!path.empty() && path[path.size() - 1] != '/')
        path += '/';
    return path;
}

// Handles post.
Response handlePost(const Request &request, const EffectiveRoute &route)
{
    FileOperation file(request);
    std::string base = route.use_alias ? route.alias : route.root;
    std::string baseDirectory = withEndingSlash(joinPaths(base, route.upload_path));
    struct stat directoryInfo;
    if (baseDirectory.empty() || stat(baseDirectory.c_str(), &directoryInfo) != 0 || !S_ISDIR(directoryInfo.st_mode))
    {
        file.response.createResponse(409, "Upload directory does not exist: " + baseDirectory, route.server->error_pages);
        return file.response;
    }
    if (access(baseDirectory.c_str(), W_OK) != 0)
    {
        file.response.createResponse(403, "Upload directory is not writable: " + baseDirectory, route.server->error_pages);
        return file.response;
    }
    if (file.getFileName(route) == FILE_OPERATION_ERROR || file.validateUploadRequest(request, route) == FILE_OPERATION_ERROR)
        return file.response;
    if (file.createFile(request.getBody(), baseDirectory, route.server->error_pages) == FILE_OPERATION_ERROR)
        return file.response;
    file.response.createResponse(201, "File created", route.server->error_pages);
    return file.response;
}

// Checks upload request.
int FileOperation::validateUploadRequest(const Request &request, const EffectiveRoute &route)
{
    std::string contentType;
    if (request.getHeader("content-type", contentType))
    {
        std::string mediaType = requestHandlerToLowerAscii(contentType);
        size_t semicolon = mediaType.find(';');
        if (semicolon != std::string::npos)
            mediaType = mediaType.substr(0, semicolon);
        size_t begin = 0;
        while (begin < mediaType.size() && (mediaType[begin] == ' ' || mediaType[begin] == '\t'))
            ++begin;
        size_t end = mediaType.size();
        while (end > begin && (mediaType[end - 1] == ' ' || mediaType[end - 1] == '\t'))
            --end;
        mediaType = mediaType.substr(begin, end - begin);
        if (mediaType == "multipart/form-data")
        {
            response.createResponse(415, "multipart/form-data is not supported", route.server->error_pages);
            return FILE_OPERATION_ERROR;
        }
    }
    std::string headerValue;
    bool hasContentLength = request.getHeader("content-length", headerValue);
    bool hasTransferEncoding = request.getHeader("transfer-encoding", headerValue);
    if (!hasContentLength && !hasTransferEncoding)
    {
        response.createResponse(411, "", route.server->error_pages);
        return FILE_OPERATION_ERROR;
    }
    return FILE_OPERATION_OK;
}

// Creates file.
int FileOperation::createFile(const std::string &body, const std::string &baseDirectory, const Response::ErrorPageMap &errorPages)
{
    filePath = generateUniqueFilename(baseDirectory);
    std::ofstream output(filePath.c_str(), std::ios::binary);
    if (!output)
    {
        response.createResponse(500, "Output file could not be opened", errorPages);
        return FILE_OPERATION_ERROR;
    }
    output.write(body.data(), static_cast<std::streamsize>(body.size()));
    if (!output)
    {
        response.createResponse(500, "Error while writing file", errorPages);
        return FILE_OPERATION_ERROR;
    }
    output.close();
    if (!output)
    {
        response.createResponse(500, "Error while closing file", errorPages);
        return FILE_OPERATION_ERROR;
    }
    return FILE_OPERATION_OK;
}

// Checks whether the target file exists.
bool FileOperation::fileExists(const std::string &fullPath) const
{
    struct stat pathInfo;
    return stat(fullPath.c_str(), &pathInfo) == 0;
}

// Builds a file name that does not overwrite an existing file.
std::string FileOperation::generateUniqueFilename(const std::string &baseDirectory) const
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
