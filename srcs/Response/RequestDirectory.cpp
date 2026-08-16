#include "RequestHandlerInternal.hpp"
#include <cerrno>
#include <dirent.h>
#include <fcntl.h>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>
static Response createIndexResponse(int fd, const std::string &indexPath, bool closeConnection, const Response::ErrorPageMap &errorPages);
static Response createAutoIndexResponse(const EffectiveRoute &route, const std::string &requestPath, bool closeConnection);

// Escapes text so it can be used safely in HTML.
static std::string escapeHtml(const std::string &text)
{
    std::string output;
    size_t i = 0;
    while (i < text.size())
    {
        if (text[i] == '&')
            output += "&amp;";
        else if (text[i] == '<')
            output += "&lt;";
        else if (text[i] == '>')
            output += "&gt;";
        else if (text[i] == '"')
            output += "&quot;";
        else if (text[i] == '\'')
            output += "&#39;";
        else output += text[i];
        ++i;
    }
    return output;
}

// Encodes path segment.
static std::string encodePathSegment(const std::string &name)
{
    const char *hex = "0123456789ABCDEF";
    std::string output;
    size_t i = 0;
    while (i < name.size())
    {
        unsigned char c = static_cast<unsigned char>(name[i]);
        bool unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_' || c == '~';
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

// Handles index.
Response handleIndex(const EffectiveRoute &route, const std::string &requestPath, bool closeConnection)
{
    Response response(closeConnection);
    if (requestPath.empty() || requestPath[requestPath.size() - 1] != '/')
    {
        response.setStatus(301);
        response.setHeader("Location", requestPath + "/");
        return response;
    }
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
            if (errno == EACCES || errno == EPERM || errno == ELOOP || errno == ENAMETOOLONG)
                response.createResponse(403, "", route.server->error_pages);
            else response.createResponse(500, "", route.server->error_pages);
            return response;
        }
        struct stat fileInfo;
        if (stat(indexPath.c_str(), &fileInfo) != 0)
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
        return createIndexResponse(fd, indexPath, closeConnection, route.server->error_pages);
    }
    if (route.autoindex)
        return createAutoIndexResponse(route, requestPath, closeConnection);
    response.createResponse(404, "", route.server->error_pages);
    return response;
}

// Creates index response.
static Response createIndexResponse(int fd, const std::string &indexPath, bool closeConnection, const Response::ErrorPageMap &errorPages)
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

// Creates auto index response.
static Response createAutoIndexResponse(const EffectiveRoute &route, const std::string &requestPath, bool closeConnection)
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
