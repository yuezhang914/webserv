#include "Response.hpp"
#include "Request.hpp"
#include "ResponseInternal.hpp"

// Removes optional spaces from both ends of a header value.
std::string responseTrimOws(const std::string &value)
{
    size_t begin = 0;
    while (begin < value.size() && (value[begin] == ' ' || value[begin] == '\t'))
        ++begin;
    size_t end = value.size();
    while (end > begin && (value[end - 1] == ' ' || value[end - 1] == '\t'))
        --end;
    return value.substr(begin, end - begin);
}

// Checks whether a Connection header contains close.
static bool headerValueContainsClose(const std::string &value)
{
    size_t begin = 0;
    while (begin <= value.size())
    {
        size_t comma = value.find(',', begin);
        size_t end = comma == std::string::npos ? value.size() : comma;
        std::string token = responseTrimOws(value.substr(begin, end - begin));
        size_t i = 0;
        while (i < token.size())
        {
            if (token[i] >= 'A' && token[i] <= 'Z')
                token[i] = static_cast<char>(token[i] - 'A' + 'a');
            ++i;
        }
        if (token == "close")
            return true;
        if (comma == std::string::npos)
            break;
        begin = comma + 1;
    }
    return false;
}

// Checks whether the request asks to close the connection.
bool Response::requestWantsClose(const Request &request)
{
    std::string value;
    if (!request.getHeader("connection", value))
        return false;
    return headerValueContainsClose(value);
}

// Updates connection header.
void Response::updateConnectionHeader()
{
    if (!_closeConnection)
    {
        switch (_statusCode)
        {
        case 400:
        case 408:
        case 409:
        case 411:
        case 413:
        case 414:
        case 431:
        case 505:
            _closeConnection = true;
            break;
        default:
            break;
        }
    }
    setManagedHeader("Connection", _closeConnection ? "close" : "keep-alive");
}
