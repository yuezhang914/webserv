#include "RequestParser.hpp"

// Parses content length.
int RequestParser::parse_content_length(const std::string& value, unsigned long body_limit, size_t& content_length)
{
    if (value.empty())
        return REQUEST_ERROR;
    size_t start = 0;
    while (start < value.size() && (value[start] == ' ' || value[start] == '\t'))
        ++start;
    size_t end = value.size();
    while (end > start && (value[end - 1] == ' ' || value[end - 1] == '\t'))
        --end;
    if (start >= end)
        return REQUEST_ERROR;
    for (size_t i = start; i < end; ++i)
    {
        if (value[i] < '0' || value[i] > '9')
            return REQUEST_ERROR;
    }
    if (end - start > 20)
    {
        size_t non_zero = start;
        while (non_zero < end && value[non_zero] == '0')
            ++non_zero;
        if (end - non_zero > 20)
            return REQUEST_BODY_TOO_LARGE;
    }
    unsigned long result = 0;
    unsigned long max_ulong = static_cast<unsigned long>(-1);
    for (size_t i = start; i < end; ++i)
    {
        unsigned long digit = static_cast<unsigned long>(value[i] - '0');
        if (result > (max_ulong - digit) / 10)
            return REQUEST_BODY_TOO_LARGE;
        result = result * 10 + digit;
        if (result > body_limit)
            return REQUEST_BODY_TOO_LARGE;
    }
    const size_t max_size = static_cast<size_t>(-1);
    if (result > static_cast<unsigned long>(max_size))
        return REQUEST_BODY_TOO_LARGE;
    content_length = static_cast<size_t>(result);
    return REQUEST_OK;
}

// Checks whether the request uses chunked transfer encoding.
int RequestParser::is_chunked_transfer_encoding(const Request& req, bool& has_te, bool& is_chunked)
{
    has_te = false;
    is_chunked = false;
    std::map<std::string, std::string>::const_iterator it = req._headers.find("transfer-encoding");
    if (it == req._headers.end())
        return REQUEST_OK;
    has_te = true;
    std::string value = Request::toLowerAscii(trim_ows(it->second));
    if (value != "chunked")
        return REQUEST_ERROR;
    is_chunked = true;
    return REQUEST_OK;
}
