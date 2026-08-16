#include "RequestParser.hpp"
#include <vector>

// Checks whether bad uri char.
bool RequestParser::has_bad_uri_char(const std::string& uri)
{
    size_t i = 0;
    while (i < uri.size())
    {
        unsigned char c = static_cast<unsigned char>(uri[i]);
        if (c < 32 || c == 127 || c == '\\')
            return true;
        ++i;
    }
    return false;
}

// Changes one hexadecimal character into its number value.
int RequestParser::hex_value(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

// Checks whether invalid percent encoding.
bool RequestParser::has_invalid_percent_encoding(const std::string& uri)
{
    size_t i = 0;
    while (i < uri.size())
    {
        if (uri[i] != '%')
        {
            ++i;
            continue;
        }
        if (i + 2 >= uri.size())
            return true;
        if (hex_value(uri[i + 1]) < 0 || hex_value(uri[i + 2]) < 0)
            return true;
        i += 3;
    }
    return false;
}

// Decodes path.
int RequestParser::decode_path(const std::string& encoded, std::string& decoded)
{
    decoded.clear();
    size_t i = 0;
    while (i < encoded.size())
    {
        unsigned char value;
        if (encoded[i] == '%')
        {
            if (i + 2 >= encoded.size())
                return REQUEST_ERROR;
            int high = hex_value(encoded[i + 1]);
            int low = hex_value(encoded[i + 2]);
            if (high < 0 || low < 0)
                return REQUEST_ERROR;
            value = static_cast<unsigned char>(high * 16 + low);
            i += 3;
        }
        else
        {
            value = static_cast<unsigned char>(encoded[i]);
            ++i;
        }
        if (value < 32 || value == 127 || value == 0 || value == '\\')
            return REQUEST_ERROR;
        decoded.push_back(static_cast<char>(value));
    }
    return REQUEST_OK;
}

// Makes a standard form of path.
int RequestParser::normalize_path(const std::string& decoded, std::string& normalized)
{
    normalized.clear();
    if (decoded.empty() || decoded[0] != '/')
        return REQUEST_ERROR;
    bool keep_trailing_slash = decoded.size() > 1 && (decoded[decoded.size() - 1] == '/' || (decoded.size() >= 2 && decoded.compare(decoded.size() - 2, 2, "/.") == 0) || (decoded.size() >= 3 && decoded.compare(decoded.size() - 3, 3, "/..") == 0));
    std::vector<std::string> segments;
    size_t start = 1;
    while (start <= decoded.size())
    {
        size_t slash = decoded.find('/', start);
        size_t end = slash == std::string::npos ? decoded.size() : slash;
        std::string segment = decoded.substr(start, end - start);
        if (segment.empty() || segment == ".")
        {
        }
        else if (segment == "..")
        {
            if (segments.empty())
                return REQUEST_ERROR;
            segments.pop_back();
        }
        else segments.push_back(segment);
        if (slash == std::string::npos)
            break;
        start = slash + 1;
    }
    normalized = "/";
    size_t i = 0;
    while (i < segments.size())
    {
        if (normalized.size() > 1)
            normalized += "/";
        normalized += segments[i];
        ++i;
    }
    if (keep_trailing_slash && normalized != "/")
        normalized += "/";
    return REQUEST_OK;
}

// Splits and normalize uri.
int RequestParser::split_and_normalize_uri(const std::string& uri, std::string& path, std::string& query)
{
    path.clear();
    query.clear();
    if (uri.empty() || uri.find('#') != std::string::npos)
        return REQUEST_ERROR;
    if (has_bad_uri_char(uri) || has_invalid_percent_encoding(uri))
        return REQUEST_ERROR;
    size_t query_pos = uri.find('?');
    std::string encoded_path = uri.substr(0, query_pos);
    if (query_pos != std::string::npos)
        query = uri.substr(query_pos + 1);
    if (encoded_path.empty() || encoded_path[0] != '/')
        return REQUEST_ERROR;
    std::string decoded;
    if (decode_path(encoded_path, decoded) != REQUEST_OK)
        return REQUEST_ERROR;
    return normalize_path(decoded, path);
}
