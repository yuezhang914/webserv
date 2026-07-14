
/*
文件：srcs/Request/RequestParser.cpp
用途：只实现 RequestParser 唯一公开入口 parseBuffer()，负责串联 request-line、headers、body framing 与 consumed。
拆分说明：本次只移动原有函数定义，不拆函数、不增加转发层，也不改变公开接口或解析逻辑。
*/
#include "RequestParser.hpp"
#include "ConfigRouteUtils.hpp"

static const size_t MAX_HEADER_SIZE = 8192;

/*
函数：RequestParser::parseBuffer
用途：实现 Request 模块唯一公开入口的完整解析流程。
参数：
    - buffer：ServerManager 已经为当前 client 累积的原始 HTTP 字节。
    - req：输出 Request；函数开始时通过 resetForParsing() 清空旧值。
    - server：调用方已经确定的 ServerConfig，Request 只借用该指针。
    - consumed：成功时输出当前第一个完整 request 占用的字节数。
返回值：REQUEST_OK、REQUEST_INCOMPLETE、REQUEST_ERROR、REQUEST_VERSION_NOT_SUPPORTED 或 REQUEST_BODY_TOO_LARGE。
实现顺序：
    1. 重置 Request 和 consumed。
    2. 找到并严格检查 header 区域。
    3. 解析 request-line、URI、headers 和必需 Host。
    4. 判断 Content-Length 或 chunked framing，并拒绝二者同时出现。
    5. 使用传入 server 与 normalized path 计算 effective body limit。
    6. 解析普通 body 或 chunked body，并准确设置 consumed。
说明：本函数直接完成完整解析，不选择 ServerConfig，也不经过其他解析入口转发。
*/
int RequestParser::parseBuffer(const std::string& buffer, Request& req,
        const ServerConfig* server, size_t& consumed) {
    consumed = 0;
    req.resetForParsing(server);

    size_t header_end = buffer.find("\r\n\r\n");
    if (header_end == std::string::npos) {
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
    if (!request_line.empty()
        && request_line[request_line.size() - 1] == '\r')
        request_line.erase(request_line.size() - 1);

    int line_status = parse_request_line(request_line, req);
    if (line_status != REQUEST_OK)
        return line_status;
    if (parse_headers(iss, req) != REQUEST_OK)
        return REQUEST_ERROR;

    bool has_te = false;
    bool is_chunked = false;
    if (is_chunked_transfer_encoding(req, has_te, is_chunked)
        != REQUEST_OK)
        return REQUEST_ERROR;
    if (has_te && req._headers.count("content-length"))
        return REQUEST_ERROR;

    size_t content_length = 0;
    unsigned long body_limit = getEffectiveBodyLimit(
        req._config, req._path);
    if (!is_chunked && req._headers.count("content-length")) {
        int len_status = parse_content_length(
            req._headers["content-length"], body_limit, content_length);
        if (len_status != REQUEST_OK)
            return len_status;
    }
    if (is_chunked)
        return parse_chunked_buffer(
            buffer, body_start, body_limit, req, consumed);

    size_t available = buffer.size() - body_start;
    if (content_length > available)
        return REQUEST_INCOMPLETE;
    req._body = buffer.substr(body_start, content_length);
    consumed = body_start + content_length;
    return REQUEST_OK;
}