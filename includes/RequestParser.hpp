#ifndef REQUEST_PARSER_HPP
#define REQUEST_PARSER_HPP

#include "Request.hpp"
#include <sstream>

/*
类：RequestParser
用途：把调用方已经读入内存的 raw HTTP buffer 严格解析成 Request。
设计：无对象状态，所有函数均为 static；外部只有一个公开入口 parseBuffer()。
边界：不执行 recv/send、不生成 Response、不选择 ServerConfig，也不管理 keep-alive。
*/
class RequestParser
{
private:
    /* 禁止创建没有状态的 RequestParser 对象。 */
    RequestParser();

    static bool is_token_char(char c);
    static bool is_valid_token(const std::string &token);
    static std::string trim_ows(const std::string &value);
    static bool has_invalid_line_endings(const std::string &text,
        size_t start, size_t end, bool allow_trailing_cr);
    static int hex_value(char c);
    static bool has_bad_uri_char(const std::string &uri);
    static bool has_invalid_percent_encoding(const std::string &uri);
    static int decode_path(const std::string &encoded, std::string &decoded);
    static int normalize_path(const std::string &decoded,
        std::string &normalized);
    static int split_and_normalize_uri(const std::string &uri,
        std::string &path, std::string &query);
    static bool is_valid_http_version_syntax(const std::string &version);
    static bool is_valid_decimal_port(const std::string &port);
    static bool is_valid_reg_name(const std::string &host);
    static bool is_valid_ip_literal(const std::string &literal);
    static int parse_request_line(const std::string &request_line,
        Request &req);
    static bool has_invalid_header_value_char(const std::string &value);
    static bool is_valid_host_value(const std::string &value);
    static int parse_headers(std::istringstream &iss, Request &req);
    static int parse_content_length(const std::string &value,
        unsigned long body_limit, size_t &content_length);
    static int is_chunked_transfer_encoding(const Request &req,
        bool &has_te, bool &is_chunked);
    static int read_chunk_size_for_buffer(const std::string &line,
        size_t current_body_size, unsigned long body_limit, size_t &size);
    static bool is_forbidden_trailer_name(const std::string &lower_key);
    static int validate_trailer_line(const std::string &line);
    static int find_chunked_trailer_end(const std::string &buffer,
        size_t pos, size_t &consumed);
    static int parse_chunked_buffer(const std::string &buffer,
        size_t body_start, unsigned long body_limit,
        Request &req, size_t &consumed);

public:
    /*
    函数：RequestParser::parseBuffer
    用途：解析 buffer 前部的第一个 HTTP/1.1 request。
    参数：
        - buffer：当前已累计的原始字节，本函数不修改它。
        - req：接收解析结果；每次调用开始会清空旧数据。
        - server：调用方确定的 ServerConfig，Request 只借用该指针。
        - consumed：成功时返回第一个完整 request 占用的字节数。
    返回：RequestStatus 中定义的五种状态。
    */
    static int parseBuffer(const std::string &buffer, Request &req,
        const ServerConfig *server, size_t &consumed);
};

#endif