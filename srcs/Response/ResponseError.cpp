#include "Response.hpp"
#include <fcntl.h>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

// Creates response.
void Response::createResponse(unsigned int code, const std::string &bodyText, const ErrorPageMap &errorPages)
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

// Loads custom error page.
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

// Reads opened file into body.
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

// Sets default error page.
void Response::setDefaultErrorPage()
{
    std::ostringstream body;
    body << "<!DOCTYPE html><html><head><title>" << _statusCode << " " << _statusMessage << "</title></head><body><h1>" << _statusCode << " " << _statusMessage << "</h1></body></html>";
    _body = body.str();
    _headers["Content-Type"] = "text/html";
}
