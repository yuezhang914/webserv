#include "SessionResponseInternal.hpp"
#include "Request.hpp"
#include <string>

// Changes form text to lower-case ASCII.
static std::string sessionFormToLowerAscii(const std::string &value)
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

// Removes optional spaces from form text.
static std::string sessionFormTrimOws(const std::string &value)
{
    size_t begin = 0;
    while (begin < value.size() && (value[begin] == ' ' || value[begin] == '\t'))
        ++begin;
    size_t end = value.size();
    while (end > begin && (value[end - 1] == ' ' || value[end - 1] == '\t'))
        --end;
    return value.substr(begin, end - begin);
}

// Changes one form hexadecimal character into a number.
static int sessionFormHexValue(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

// Decodes session form component.
static bool decodeSessionFormComponent(const std::string &encoded, std::string &decoded)
{
    decoded.clear();
    std::string result;
    size_t i = 0;
    while (i < encoded.size())
    {
        unsigned char c = 0;
        if (encoded[i] == '+')
        {
            c = ' ';
            ++i;
        }
        else if (encoded[i] == '%')
        {
            if (i + 2 >= encoded.size())
                return false;
            int high = sessionFormHexValue(encoded[i + 1]);
            int low = sessionFormHexValue(encoded[i + 2]);
            if (high < 0 || low < 0)
                return false;
            c = static_cast<unsigned char>((high << 4) | low);
            i += 3;
        }
        else
        {
            c = static_cast<unsigned char>(encoded[i]);
            ++i;
        }
        if (c < 32 || c == 127)
            return false;
        result += static_cast<char>(c);
    }
    decoded.swap(result);
    return true;
}

// Gets session form user.
static bool extractSessionFormUser(const std::string &body, std::string &userName)
{
    userName.clear();
    bool found = false;
    size_t pos = 0;
    while (pos <= body.size())
    {
        size_t end = body.find('&', pos);
        if (end == std::string::npos)
            end = body.size();
        std::string field = body.substr(pos, end - pos);
        size_t equal = field.find('=');
        if (equal == std::string::npos)
            return false;
        std::string decodedName;
        std::string decodedValue;
        if (!decodeSessionFormComponent(field.substr(0, equal), decodedName) || !decodeSessionFormComponent(field.substr(equal + 1), decodedValue))
            return false;
        if (decodedName == "user")
        {
            if (found)
                return false;
            userName = decodedValue;
            found = true;
        }
        if (end == body.size())
            break;
        pos = end + 1;
    }
    if (!found)
        userName.clear();
    return found;
}

// Gets session login user name.
int extractSessionLoginUserName(const Request &request, std::string &userName)
{
    userName.clear();
    std::string contentType;
    if (!request.getHeader("content-type", contentType))
    {
        userName = request.getBody();
        return 200;
    }
    size_t semicolon = contentType.find(';');
    std::string mediaType = sessionFormTrimOws(contentType.substr(0, semicolon));
    mediaType = sessionFormToLowerAscii(mediaType);
    if (mediaType == "text/plain")
    {
        userName = request.getBody();
        return 200;
    }
    if (mediaType == "application/x-www-form-urlencoded")
    {
        if (extractSessionFormUser(request.getBody(), userName))
            return 200;
        userName.clear();
        return 400;
    }
    return 415;
}
