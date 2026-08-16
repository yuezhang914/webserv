#include "RequestParser.hpp"

// Checks whether the character is valid in an HTTP token.
bool RequestParser::is_token_char(char c)
{
    unsigned char uc = static_cast<unsigned char>(c);
    if ((uc >= 'A' && uc <= 'Z') || (uc >= 'a' && uc <= 'z') || (uc >= '0' && uc <= '9'))
        return true;
    return c == '!' || c == '#' || c == '$' || c == '%' || c == '&' || c == '\'' || c == '*' || c == '+' || c == '-' || c == '.' || c == '^' || c == '_' || c == '`' || c == '|' || c == '~';
}

// Checks whether valid token.
bool RequestParser::is_valid_token(const std::string& token)
{
    if (token.empty())
        return false;
    for (size_t i = 0; i < token.size(); ++i)
    {
        if (!is_token_char(token[i]))
            return false;
    }
    return true;
}

// Checks whether invalid line endings.
bool RequestParser::has_invalid_line_endings(const std::string& text, size_t start, size_t end, bool allow_trailing_cr)
{
    size_t i = start;
    while (i < end)
    {
        if (text[i] == '\n')
        {
            if (i == start || text[i - 1] != '\r')
                return true;
        }
        else if (text[i] == '\r')
        {
            if (i + 1 >= end)
                return !allow_trailing_cr;
            if (text[i + 1] != '\n')
                return true;
            ++i;
        }
        ++i;
    }
    return false;
}

// Checks whether valid http version syntax.
bool RequestParser::is_valid_http_version_syntax(const std::string& version)
{
    return version.size() == 8 && version.compare(0, 5, "HTTP/") == 0 && version[5] >= '0' && version[5] <= '9' && version[6] == '.' && version[7] >= '0' && version[7] <= '9';
}

// Parses request line.
int RequestParser::parse_request_line(const std::string& request_line, Request& req)
{
    if (request_line.empty() || request_line.find('\t') != std::string::npos)
        return REQUEST_ERROR;
    size_t first_space = request_line.find(' ');
    if (first_space == std::string::npos || first_space == 0)
        return REQUEST_ERROR;
    size_t second_space = request_line.find(' ', first_space + 1);
    if (second_space == std::string::npos || second_space == first_space + 1 || second_space + 1 >= request_line.size())
        return REQUEST_ERROR;
    if (request_line.find(' ', second_space + 1) != std::string::npos)
        return REQUEST_ERROR;
    req._method = request_line.substr(0, first_space);
    req._uri = request_line.substr(first_space + 1, second_space - first_space - 1);
    req._version = request_line.substr(second_space + 1);
    if (!is_valid_token(req._method))
        return REQUEST_ERROR;
    if (!is_valid_http_version_syntax(req._version))
        return REQUEST_ERROR;
    if (req._version != "HTTP/1.1")
        return REQUEST_VERSION_NOT_SUPPORTED;
    return split_and_normalize_uri(req._uri, req._path, req._query);
}
