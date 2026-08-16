#include "Response.hpp"
#include <sstream>

// Sets status.
void Response::setStatus(int statusCode)
{
    _statusCode = statusCode;
    _statusMessage = statusMessageFor(statusCode);
    if (!statusMayHaveBody(_statusCode))
    {
        _body.clear();
        _headers.erase("Content-Type");
    }
    updateConnectionHeader();
    updateContentLength();
}

// Returns the text message for an HTTP status code.
std::string Response::statusMessageFor(int statusCode)
{
    switch (statusCode)
    {
    case 200:
        return "OK";
    case 201:
        return "Created";
    case 204:
        return "No Content";
    case 300:
        return "Multiple Choices";
    case 301:
        return "Moved Permanently";
    case 302:
        return "Found";
    case 303:
        return "See Other";
    case 304:
        return "Not Modified";
    case 307:
        return "Temporary Redirect";
    case 308:
        return "Permanent Redirect";
    case 400:
        return "Bad Request";
    case 403:
        return "Forbidden";
    case 404:
        return "Not Found";
    case 405:
        return "Method Not Allowed";
    case 408:
        return "Request Timeout";
    case 409:
        return "Conflict";
    case 411:
        return "Length Required";
    case 413:
        return "Payload Too Large";
    case 414:
        return "URI Too Long";
    case 415:
        return "Unsupported Media Type";
    case 423:
        return "Locked";
    case 431:
        return "Request Header Fields Too Large";
    case 500:
        return "Internal Server Error";
    case 501:
        return "Not Implemented";
    case 502:
        return "Bad Gateway";
    case 504:
        return "Gateway Timeout";
    case 505:
        return "HTTP Version Not Supported";
    }
    if (statusCode >= 300 && statusCode <= 399)
        return "Redirect";
    return "Unknown";
}

// Checks whether the status code is an error status.
bool Response::isErrorStatusCode(int statusCode)
{
    return statusCode >= 400 && statusCode <= 599;
}

// Checks whether this HTTP status may have a body.
bool Response::statusMayHaveBody(int statusCode)
{
    return !((statusCode >= 100 && statusCode < 200) || statusCode == 204 || statusCode == 304);
}

// Changes a size value into decimal text.
std::string Response::sizeToString(size_t value)
{
    std::ostringstream output;
    output << value;
    return output.str();
}

// Updates content length.
void Response::updateContentLength()
{
    if (!statusMayHaveBody(_statusCode))
    {
        _headers.erase("Content-Length");
        return;
    }
    setManagedHeader("Content-Length", sizeToString(_body.size()));
}
