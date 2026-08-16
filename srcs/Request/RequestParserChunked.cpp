#include "RequestParser.hpp"
#include "Defines.hpp"

// Skips optional spaces in one chunk-size line.
void RequestParser::skip_chunk_ows(const std::string& line, size_t& pos)
{
    while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t'))
        ++pos;
}

// Reads chunk extension token.
bool RequestParser::read_chunk_extension_token(const std::string& line, size_t& pos)
{
    size_t start = pos;
    while (pos < line.size() && is_token_char(line[pos]))
        ++pos;
    return pos > start;
}

// Reads chunk extension quoted string.
bool RequestParser::read_chunk_extension_quoted_string(const std::string& line, size_t& pos)
{
    if (pos >= line.size() || line[pos] != '"')
        return false;
    ++pos;
    while (pos < line.size())
    {
        unsigned char c = static_cast<unsigned char>(line[pos]);
        if (c == '"')
        {
            ++pos;
            return true;
        }
        if (c == '\\')
        {
            ++pos;
            if (pos >= line.size())
                return false;
            unsigned char escaped = static_cast<unsigned char>(line[pos]);
            if (escaped != '\t' && (escaped < 32 || escaped > 126))
                return false;
            ++pos;
            continue;
        }
        if (c != '\t' && (c < 32 || c > 126))
            return false;
        ++pos;
    }
    return false;
}

// Checks whether valid chunk extensions.
bool RequestParser::is_valid_chunk_extensions(const std::string& line, size_t extension_start)
{
    size_t pos = extension_start;
    while (pos < line.size())
    {
        if (line[pos] != ';')
            return false;
        ++pos;
        skip_chunk_ows(line, pos);
        if (!read_chunk_extension_token(line, pos))
            return false;
        skip_chunk_ows(line, pos);
        if (pos < line.size() && line[pos] == '=')
        {
            ++pos;
            skip_chunk_ows(line, pos);
            if (pos >= line.size())
                return false;
            if (line[pos] == '"')
            {
                if (!read_chunk_extension_quoted_string(line, pos))
                    return false;
            }
            else if (!read_chunk_extension_token(line, pos))
                return false;
            skip_chunk_ows(line, pos);
        }
        if (pos == line.size())
            return true;
        if (line[pos] != ';')
            return false;
    }
    return false;
}

// Reads chunk size for buffer.
int RequestParser::read_chunk_size_for_buffer(const std::string& line, size_t current_body_size, unsigned long body_limit, size_t& size)
{
    size_t number_end = 0;
    while (number_end < line.size() && hex_value(line[number_end]) >= 0)
        ++number_end;
    if (number_end == 0)
        return REQUEST_ERROR;
    size_t syntax_pos = number_end;
    skip_chunk_ows(line, syntax_pos);
    if (syntax_pos < line.size())
    {
        if (line[syntax_pos] != ';' || !is_valid_chunk_extensions(line, syntax_pos))
            return REQUEST_ERROR;
    }
    else if (syntax_pos != number_end)
        return REQUEST_ERROR;
    std::string number = line.substr(0, number_end);
    unsigned long result = 0;
    const unsigned long max_ulong = static_cast<unsigned long>(-1);
    size_t i = 0;
    while (i < number.size())
    {
        unsigned long digit = static_cast<unsigned long>(hex_value(number[i]));
        if (result > (max_ulong - digit) / 16)
            return REQUEST_BODY_TOO_LARGE;
        result = result * 16 + digit;
        ++i;
    }
    const size_t max_size = static_cast<size_t>(-1);
    if (result > static_cast<unsigned long>(max_size) || result > static_cast<unsigned long>(max_size - 2))
        return REQUEST_BODY_TOO_LARGE;
    if (current_body_size > static_cast<size_t>(body_limit))
        return REQUEST_BODY_TOO_LARGE;
    unsigned long current = static_cast<unsigned long>(current_body_size);
    if (current > body_limit || result > body_limit - current)
        return REQUEST_BODY_TOO_LARGE;
    size = static_cast<size_t>(result);
    return REQUEST_OK;
}

// Checks whether a trailer header name is not allowed.
bool RequestParser::is_forbidden_trailer_name(const std::string& lower_key)
{
    return lower_key == "host" || lower_key == "content-length" || lower_key == "transfer-encoding";
}

// Checks trailer line.
int RequestParser::validate_trailer_line(const std::string& line)
{
    size_t colon_pos = line.find(':');
    if (colon_pos == std::string::npos)
        return REQUEST_ERROR;
    std::string key = line.substr(0, colon_pos);
    if (!is_valid_token(key))
        return REQUEST_ERROR;
    std::string raw_value = line.substr(colon_pos + 1);
    if (has_invalid_header_value_char(raw_value))
        return REQUEST_ERROR;
    if (is_forbidden_trailer_name(Request::toLowerAscii(key)))
        return REQUEST_ERROR;
    return REQUEST_OK;
}

// Finds chunked trailer end.
int RequestParser::find_chunked_trailer_end(const std::string& buffer, size_t pos, size_t& consumed)
{
    if (pos > buffer.size())
        return REQUEST_ERROR;
    if (buffer.size() - pos < 2)
        return REQUEST_INCOMPLETE;
    if (buffer.compare(pos, 2, "\r\n") == 0)
    {
        consumed = pos + 2;
        return REQUEST_OK;
    }
    size_t trailer_end = buffer.find("\r\n\r\n", pos);
    if (trailer_end == std::string::npos)
    {
        if (has_invalid_line_endings(buffer, pos, buffer.size(), true))
            return REQUEST_ERROR;
        if (buffer.size() - pos > MAX_TRAILER_SIZE)
            return REQUEST_ERROR;
        return REQUEST_INCOMPLETE;
    }
    size_t trailer_size = trailer_end + 4 - pos;
    if (trailer_size > MAX_TRAILER_SIZE)
        return REQUEST_ERROR;
    if (has_invalid_line_endings(buffer, pos, trailer_end + 4, false))
        return REQUEST_ERROR;
    size_t line_start = pos;
    while (line_start < trailer_end)
    {
        size_t line_end = buffer.find("\r\n", line_start);
        if (line_end == std::string::npos || line_end > trailer_end)
            return REQUEST_ERROR;
        if (validate_trailer_line(buffer.substr(line_start, line_end - line_start)) != REQUEST_OK)
            return REQUEST_ERROR;
        line_start = line_end + 2;
    }
    consumed = trailer_end + 4;
    return REQUEST_OK;
}

// Scans more chunked body data without blocking.
int RequestParser::advanceChunkedScan(const std::string& buffer, size_t& scan_pos, unsigned long body_limit, size_t& decoded_size, size_t& consumed)
{
    while (true)
    {
        if (scan_pos > buffer.size())
            return REQUEST_ERROR;
        size_t line_end = buffer.find("\r\n", scan_pos);
        if (line_end == std::string::npos)
        {
            if (has_invalid_line_endings(buffer, scan_pos, buffer.size(), true))
                return REQUEST_ERROR;
            if (buffer.size() - scan_pos > MAX_CHUNK_SIZE_LINE)
                return REQUEST_ERROR;
            return REQUEST_INCOMPLETE;
        }
        if (line_end - scan_pos > MAX_CHUNK_SIZE_LINE)
            return REQUEST_ERROR;
        size_t chunk_size = 0;
        int size_status = read_chunk_size_for_buffer(buffer.substr(scan_pos, line_end - scan_pos), decoded_size, body_limit, chunk_size);
        if (size_status != REQUEST_OK)
            return size_status;
        size_t data_start = line_end + 2;
        if (chunk_size == 0)
            return find_chunked_trailer_end(buffer, data_start, consumed);
        if (data_start > buffer.size())
            return REQUEST_ERROR;
        size_t available = buffer.size() - data_start;
        if (available < chunk_size || available - chunk_size < 2)
            return REQUEST_INCOMPLETE;
        if (buffer.compare(data_start + chunk_size, 2, "\r\n") != 0)
            return REQUEST_ERROR;
        decoded_size += chunk_size;
        scan_pos = data_start + chunk_size + 2;
    }
}

// Decodes complete chunked body.
int RequestParser::decode_complete_chunked_body(const std::string& buffer, size_t body_start, size_t decoded_size, Request& req)
{
    size_t pos = body_start;
    std::string body;
    body.reserve(decoded_size);
    while (true)
    {
        size_t line_end = buffer.find("\r\n", pos);
        if (line_end == std::string::npos)
            return REQUEST_ERROR;
        size_t chunk_size = 0;
        int status = read_chunk_size_for_buffer(buffer.substr(pos, line_end - pos), body.size(), static_cast<unsigned long>(decoded_size), chunk_size);
        if (status != REQUEST_OK)
            return status;
        pos = line_end + 2;
        if (chunk_size == 0)
            break;
        body.append(buffer, pos, chunk_size);
        pos += chunk_size + 2;
    }
    req._body.swap(body);
    return REQUEST_OK;
}

// Parses chunked buffer.
int RequestParser::parse_chunked_buffer(const std::string &buffer, size_t body_start, unsigned long body_limit, Request &req, size_t &consumed)
{
    size_t scan_pos = body_start;
    size_t decoded_size = 0;
    int status = advanceChunkedScan(buffer, scan_pos, body_limit, decoded_size, consumed);
    if (status != REQUEST_OK)
        return status;
    return decode_complete_chunked_body(buffer, body_start, decoded_size, req);
}
