#include "RequestParser.hpp"

// Removes extra spaces from ows.
std::string RequestParser::trim_ows(const std::string& value)
{
    size_t first = value.find_first_not_of(" \t");
    if (first == std::string::npos)
        return "";
    size_t last = value.find_last_not_of(" \t");
    return value.substr(first, last - first + 1);
}

// Checks whether valid decimal port.
bool RequestParser::is_valid_decimal_port(const std::string& port)
{
    if (port.empty())
        return false;
    unsigned long value = 0;
    size_t i = 0;
    while (i < port.size())
    {
        if (port[i] < '0' || port[i] > '9')
            return false;
        value = value * 10 + static_cast<unsigned long>(port[i] - '0');
        if (value > 65535)
            return false;
        ++i;
    }
    return value > 0;
}

// Checks whether valid reg name.
bool RequestParser::is_valid_reg_name(const std::string& host)
{
    if (host.empty())
        return false;
    bool has_alnum = false;
    size_t i = 0;
    while (i < host.size())
    {
        unsigned char c = static_cast<unsigned char>(host[i]);
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
        {
            has_alnum = true;
            ++i;
            continue;
        }
        if (host[i] == '-' || host[i] == '.' || host[i] == '_' || host[i] == '~' || host[i] == '!' || host[i] == '$' || host[i] == '&' || host[i] == '\'' || host[i] == '(' || host[i] == ')' || host[i] == '*' || host[i] == '+' || host[i] == ',' || host[i] == ';' || host[i] == '=')
        {
            ++i;
            continue;
        }
        if (host[i] == '%')
        {
            if (i + 2 >= host.size() || hex_value(host[i + 1]) < 0 || hex_value(host[i + 2]) < 0)
                return false;
            i += 3;
            continue;
        }
        return false;
    }
    return has_alnum;
}

// Checks whether the text is a valid IPv4 address.
bool RequestParser::is_valid_ipv4_address(const std::string& address)
{
    size_t start = 0;
    int part_count = 0;
    while (start <= address.size())
    {
        size_t dot = address.find('.', start);
        size_t end = dot == std::string::npos ? address.size() : dot;
        if (end == start || end - start > 3)
            return false;
        if (end - start > 1 && address[start] == '0')
            return false;
        unsigned long value = 0;
        size_t i = start;
        while (i < end)
        {
            if (address[i] < '0' || address[i] > '9')
                return false;
            value = value * 10 + static_cast<unsigned long>(address[i] - '0');
            if (value > 255)
                return false;
            ++i;
        }
        ++part_count;
        if (dot == std::string::npos)
            break;
        start = dot + 1;
    }
    return part_count == 4;
}

// Counts IPv6 groups on one side of the address.
bool RequestParser::count_ipv6_side_groups(const std::string& side, bool allow_ipv4, size_t& group_count)
{
    group_count = 0;
    if (side.empty())
        return true;
    size_t start = 0;
    while (start <= side.size())
    {
        size_t colon = side.find(':', start);
        size_t end = colon == std::string::npos ? side.size() : colon;
        if (end == start)
            return false;
        std::string group = side.substr(start, end - start);
        if (group.find('.') != std::string::npos)
        {
            if (!allow_ipv4 || colon != std::string::npos || !is_valid_ipv4_address(group))
                return false;
            group_count += 2;
        }
        else
        {
            if (group.size() > 4)
                return false;
            size_t i = 0;
            while (i < group.size())
            {
                if (hex_value(group[i]) < 0)
                    return false;
                ++i;
            }
            ++group_count;
        }
        if (colon == std::string::npos)
            break;
        start = colon + 1;
    }
    return true;
}

// Checks whether valid ip literal.
bool RequestParser::is_valid_ip_literal(const std::string& literal)
{
    if (literal.empty())
        return false;
    size_t compressed = literal.find("::");
    if (compressed != std::string::npos && literal.find("::", compressed + 1) != std::string::npos)
        return false;
    size_t left_groups = 0;
    size_t right_groups = 0;
    if (compressed == std::string::npos)
    {
        if (!count_ipv6_side_groups(literal, true, left_groups))
            return false;
        return left_groups == 8;
    }
    std::string left = literal.substr(0, compressed);
    std::string right = literal.substr(compressed + 2);
    if (!count_ipv6_side_groups(left, false, left_groups))
        return false;
    if (!count_ipv6_side_groups(right, true, right_groups))
        return false;
    return left_groups + right_groups < 8;
}

// Checks whether invalid header value char.
bool RequestParser::has_invalid_header_value_char(const std::string& value)
{
    size_t i = 0;
    while (i < value.size())
    {
        unsigned char c = static_cast<unsigned char>(value[i]);
        if ((c < 32 && c != '\t') || c == 127)
            return true;
        ++i;
    }
    return false;
}

// Checks whether valid host value.
bool RequestParser::is_valid_host_value(const std::string& value)
{
    if (value.empty())
        return false;
    size_t i = 0;
    while (i < value.size())
    {
        unsigned char c = static_cast<unsigned char>(value[i]);
        if (c == ' ' || c == '\t' || c < 32 || c == 127)
            return false;
        ++i;
    }
    if (value[0] == '[')
    {
        size_t close = value.find(']');
        if (close == std::string::npos)
            return false;
        if (!is_valid_ip_literal(value.substr(1, close - 1)))
            return false;
        if (close + 1 == value.size())
            return true;
        if (value[close + 1] != ':')
            return false;
        return is_valid_decimal_port(value.substr(close + 2));
    }
    size_t colon = value.find(':');
    if (colon != std::string::npos && value.find(':', colon + 1) != std::string::npos)
        return false;
    std::string host = value.substr(0, colon);
    if (!is_valid_reg_name(host))
        return false;
    if (colon == std::string::npos)
        return true;
    return is_valid_decimal_port(value.substr(colon + 1));
}

// Parses headers.
int RequestParser::parse_headers(std::istringstream& iss, Request& req)
{
    std::string line;
    while (std::getline(iss, line))
    {
        if (!line.empty() && line[line.size() - 1] == '\r')
            line.erase(line.size() - 1);
        if (line.empty())
            break;
        size_t colon_pos = line.find(':');
        if (colon_pos == std::string::npos)
            return REQUEST_ERROR;
        std::string key = line.substr(0, colon_pos);
        if (!is_valid_token(key))
            return REQUEST_ERROR;
        std::string raw_value = line.substr(colon_pos + 1);
        if (has_invalid_header_value_char(raw_value))
            return REQUEST_ERROR;
        std::string lower_key = Request::toLowerAscii(key);
        if (req._headers.find(lower_key) != req._headers.end())
            return REQUEST_ERROR;
        std::string value = trim_ows(raw_value);
        if (lower_key == "host" && !is_valid_host_value(value))
            return REQUEST_ERROR;
        req._headers[lower_key] = value;
    }
    if (req._headers.find("host") == req._headers.end())
        return REQUEST_ERROR;
    return REQUEST_OK;
}
