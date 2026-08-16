#include "Response.hpp"

// Returns header.
bool Response::getHeader(const std::string &name, std::string &value) const
{
    HeaderMap::const_iterator it = _headers.find(canonicalHeaderName(name));
    if (it == _headers.end())
    {
        value.clear();
        return false;
    }
    value = it->second;
    return true;
}

// Sets header.
void Response::setHeader(const std::string &name, const std::string &value)
{
    if (!isValidHeaderName(name) || !isValidHeaderValue(value) || isManagedHeader(name))
        return;
    _headers[canonicalHeaderName(name)] = value;
}

// Removes header.
void Response::removeHeader(const std::string &name)
{
    if (isManagedHeader(name))
        return;
    _headers.erase(canonicalHeaderName(name));
}

// Changes ASCII letters in the text to lower case.
std::string Response::toLowerAscii(const std::string &value)
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

// Changes a header name to the form used by this project.
std::string Response::canonicalHeaderName(const std::string &name)
{
    std::string result = toLowerAscii(name);
    bool uppercaseNext = true;
    size_t i = 0;
    while (i < result.size())
    {
        if (uppercaseNext && result[i] >= 'a' && result[i] <= 'z')
            result[i] = static_cast<char>(result[i] - 'a' + 'A');
        uppercaseNext = result[i] == '-';
        ++i;
    }
    return result;
}

// Checks whether the response header is managed by this class.
bool Response::isManagedHeader(const std::string &name)
{
    std::string lower = toLowerAscii(name);
    return lower == "content-length" || lower == "connection";
}

// Checks whether valid header name.
bool Response::isValidHeaderName(const std::string &name)
{
    if (name.empty())
        return false;
    const std::string symbols("!#$%&'*+-.^_`|~");
    size_t i = 0;
    while (i < name.size())
    {
        unsigned char c = static_cast<unsigned char>(name[i]);
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || symbols.find(c) != std::string::npos))
            return false;
        ++i;
    }
    return true;
}

// Checks whether valid header value.
bool Response::isValidHeaderValue(const std::string &value)
{
    size_t i = 0;
    while (i < value.size())
    {
        unsigned char c = static_cast<unsigned char>(value[i]);
        if ((c < 32 && c != '\t') || c == 127)
            return false;
        ++i;
    }
    return true;
}

// Sets managed header.
void Response::setManagedHeader(const std::string &name, const std::string &value)
{
    _headers[canonicalHeaderName(name)] = value;
}
