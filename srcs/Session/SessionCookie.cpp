#include "SessionCookie.hpp"
#include <sstream>
static const size_t MAX_COOKIE_HEADER_LENGTH = 8192;
static const size_t MAX_COOKIE_NAME_LENGTH = 128;
static const size_t MAX_COOKIE_VALUE_LENGTH = 4096;
static const size_t MAX_COOKIE_PATH_LENGTH = 1024;
static const size_t MAX_SET_COOKIE_LENGTH = 8192;

// Removes extra spaces from ows.
std::string SessionCookie::trimOws(const std::string &value)
{
    size_t begin = 0;
    while (begin < value.size() && (value[begin] == ' ' || value[begin] == '\t'))
        ++begin;
    size_t end = value.size();
    while (end > begin && (value[end - 1] == ' ' || value[end - 1] == '\t'))
        --end;
    return value.substr(begin, end - begin);
}

// Checks whether the character is valid in an HTTP token.
bool SessionCookie::isTokenChar(char c)
{
    const std::string symbols("!#$%&'*+-.^_`|~");
    unsigned char value = static_cast<unsigned char>(c);
    return (value >= '0' && value <= '9') || (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z') || symbols.find(c) != std::string::npos;
}

// Checks whether valid cookie name.
bool SessionCookie::isValidCookieName(const std::string &name)
{
    if (name.empty() || name.size() > MAX_COOKIE_NAME_LENGTH)
        return false;
    size_t i = 0;
    while (i < name.size())
    {
        if (!isTokenChar(name[i]))
            return false;
        ++i;
    }
    return true;
}

// Checks whether valid cookie value.
bool SessionCookie::isValidCookieValue(const std::string &value)
{
    if (value.size() > MAX_COOKIE_VALUE_LENGTH)
        return false;
    size_t i = 0;
    while (i < value.size())
    {
        unsigned char c = static_cast<unsigned char>(value[i]);
        bool allowed = c == 0x21 || (c >= 0x23 && c <= 0x2B) || (c >= 0x2D && c <= 0x3A) || (c >= 0x3C && c <= 0x5B) || (c >= 0x5D && c <= 0x7E);
        if (!allowed)
            return false;
        ++i;
    }
    return true;
}

// Parses cookie header.
bool SessionCookie::parseCookieHeader(const std::string &headerValue, CookieMap &cookies)
{
    cookies.clear();
    if (headerValue.empty())
        return true;
    if (headerValue.size() > MAX_COOKIE_HEADER_LENGTH)
        return false;
    try
    {
        CookieMap parsed;
        size_t begin = 0;
        while (begin < headerValue.size())
        {
            size_t semicolon = headerValue.find(';', begin);
            size_t end = semicolon == std::string::npos ? headerValue.size() : semicolon;
            std::string pair = trimOws(headerValue.substr(begin, end - begin));
            if (pair.empty())
                return false;
            size_t equal = pair.find('=');
            if (equal == std::string::npos || equal == 0)
                return false;
            std::string name = pair.substr(0, equal);
            std::string value = pair.substr(equal + 1);
            if (!isValidCookieName(name) || !isValidCookieValue(value))
                return false;
            std::pair<CookieMap::iterator, bool> insertResult = parsed.insert(std::make_pair(name, value));
            if (!insertResult.second)
                return false;
            if (semicolon == std::string::npos)
                break;
            begin = semicolon + 1;
            if (begin == headerValue.size())
                return false;
        }
        cookies.swap(parsed);
        return true;
    }
    catch (...)
    {
        cookies.clear();
        return false;
    }
}

// Returns cookie.
bool SessionCookie::getCookie(const std::string &headerValue, const std::string &name, std::string &value)
{
    value.clear();
    if (!isValidCookieName(name))
        return false;
    CookieMap cookies;
    if (!parseCookieHeader(headerValue, cookies))
        return false;
    CookieMap::const_iterator it = cookies.find(name);
    if (it == cookies.end())
        return false;
    try
    {
        value = it->second;
        return true;
    }
    catch (...)
    {
        value.clear();
        return false;
    }
}

// Checks whether valid path.
bool SessionCookie::isValidPath(const std::string &path)
{
    if (path.empty() || path[0] != '/' || path.size() > MAX_COOKIE_PATH_LENGTH)
        return false;
    size_t i = 0;
    while (i < path.size())
    {
        unsigned char c = static_cast<unsigned char>(path[i]);
        if (c < 32 || c == 127 || c == ';')
            return false;
        ++i;
    }
    return true;
}

// Checks whether valid same site.
bool SessionCookie::isValidSameSite(const std::string &sameSite, bool secure)
{
    if (sameSite.empty() || sameSite == "Lax" || sameSite == "Strict")
        return true;
    if (sameSite == "None")
        return secure;
    return false;
}

// Changes an unsigned long value into decimal text.
std::string SessionCookie::unsignedLongToString(unsigned long value)
{
    std::ostringstream output;
    output << value;
    return output.str();
}

// Builds set cookie.
bool SessionCookie::buildSetCookie(const std::string &name, const std::string &value, const std::string &path, unsigned long maxAge, bool httpOnly, const std::string &sameSite, bool secure, std::string &headerValue)
{
    headerValue.clear();
    if (!isValidCookieName(name) || !isValidCookieValue(value) || !isValidPath(path) || !isValidSameSite(sameSite, secure))
        return false;
    try
    {
        std::string result = name + "=" + value;
        result += "; Path=" + path;
        result += "; Max-Age=" + unsignedLongToString(maxAge);
        if (httpOnly)
            result += "; HttpOnly";
        if (!sameSite.empty())
            result += "; SameSite=" + sameSite;
        if (secure)
            result += "; Secure";
        if (result.size() > MAX_SET_COOKIE_LENGTH)
            return false;
        headerValue = result;
        return true;
    }
    catch (...)
    {
        headerValue.clear();
        return false;
    }
}

// Builds expired cookie.
bool SessionCookie::buildExpiredCookie(const std::string &name, const std::string &path, bool httpOnly, const std::string &sameSite, bool secure, std::string &headerValue)
{
    headerValue.clear();
    try
    {
        std::string base;
        if (!buildSetCookie(name, "", path, 0, httpOnly, sameSite, secure, base))
            return false;
        base += "; Expires=Thu, 01 Jan 1970 00:00:00 GMT";
        if (base.size() > MAX_SET_COOKIE_LENGTH)
            return false;
        headerValue = base;
        return true;
    }
    catch (...)
    {
        headerValue.clear();
        return false;
    }
}
