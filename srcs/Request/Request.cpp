#include "Request.hpp"

// Changes ASCII letters in the text to lower case.
std::string Request::toLowerAscii(const std::string &value)
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

// Creates a new Request object.
Request::Request() : _config(NULL)
{
}

// Resets for parsing.
void Request::resetForParsing(const ServerConfig *server)
{
    _method.clear();
    _uri.clear();
    _path.clear();
    _query.clear();
    _version.clear();
    _headers.clear();
    _body.clear();
    _config = server;
}

// Returns method.
const std::string &Request::getMethod() const
{
    return _method;
}

// Returns uri.
const std::string &Request::getUri() const
{
    return _uri;
}

// Returns path.
const std::string &Request::getPath() const
{
    return _path;
}

// Returns query.
const std::string &Request::getQuery() const
{
    return _query;
}

// Returns version.
const std::string &Request::getVersion() const
{
    return _version;
}

// Returns headers.
const Request::HeaderMap &Request::getHeaders() const
{
    return _headers;
}

// Returns body.
const std::string &Request::getBody() const
{
    return _body;
}

// Returns config.
const ServerConfig *Request::getConfig() const
{
    return _config;
}

// Returns header.
bool Request::getHeader(const std::string &name, std::string &value) const
{
    HeaderMap::const_iterator it = _headers.find(toLowerAscii(name));
    if (it == _headers.end())
    {
        value.clear();
        return false;
    }
    value = it->second;
    return true;
}
