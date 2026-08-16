#include "RequestParser.hpp"
#include "ConfigRouteUtils.hpp"

// Parses buffer.
int RequestParser::parseBuffer(const std::string &buffer, Request &req, const ServerConfig *server, size_t &consumed)
{
    consumed = 0;
    req.resetForParsing(server);
    if (server == NULL)
        return REQUEST_ERROR;
    size_t header_end = buffer.find("\r\n\r\n");
    if (header_end == std::string::npos)
    {
        if (has_invalid_line_endings(buffer, 0, buffer.size(), true))
            return REQUEST_ERROR;
        if (buffer.size() > MAX_HEADER_SIZE)
            return REQUEST_ERROR;
        return REQUEST_INCOMPLETE;
    }
    size_t header_size = header_end + 4;
    if (header_size > MAX_HEADER_SIZE)
        return REQUEST_ERROR;
    if (has_invalid_line_endings(buffer, 0, header_size, false))
        return REQUEST_ERROR;
    size_t body_start = header_size;
    std::istringstream iss(buffer.substr(0, header_end));
    std::string request_line;
    if (!std::getline(iss, request_line))
        return REQUEST_ERROR;
    if (!request_line.empty() && request_line[request_line.size() - 1] == '\r')
        request_line.erase(request_line.size() - 1);
    int line_status = parse_request_line(request_line, req);
    if (line_status != REQUEST_OK)
        return line_status;
    if (parse_headers(iss, req) != REQUEST_OK)
        return REQUEST_ERROR;
    bool has_te = false;
    bool is_chunked = false;
    if (is_chunked_transfer_encoding(req, has_te, is_chunked) != REQUEST_OK)
        return REQUEST_ERROR;
    if (has_te && req._headers.count("content-length"))
        return REQUEST_ERROR;
    size_t content_length = 0;
    unsigned long body_limit = getEffectiveBodyLimit(req._config, req._path);
    if (!is_chunked && req._headers.count("content-length"))
    {
        int len_status = parse_content_length(req._headers["content-length"], body_limit, content_length);
        if (len_status != REQUEST_OK)
            return len_status;
    }
    if (is_chunked)
        return parse_chunked_buffer(buffer, body_start, body_limit, req, consumed);
    size_t available = buffer.size() - body_start;
    if (content_length > available)
        return REQUEST_INCOMPLETE;
    req._body = buffer.substr(body_start, content_length);
    consumed = body_start + content_length;
    return REQUEST_OK;
}
