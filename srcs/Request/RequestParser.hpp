#ifndef REQUEST_PARSER_HPP
#define REQUEST_PARSER_HPP

#include "Request.hpp"
#include <sstream>

/*
类：RequestParser
用途：把 ServerManager 已经读入的 raw client buffer 严格解析成 Request。
设计方式：本类没有每个对象独立的状态，因此所有解析函数都是 static；构造函数私有，禁止创建无意义的 RequestParser 实例。
封装边界：外部只调用 parseBuffer()；request-line、header、Host、Content-Length、chunked 和 trailer 辅助函数全部为 private。
*/
class RequestParser
{
private:
    /* 禁止实例化和复制；RequestParser 只是静态解析服务。 */
    RequestParser();
    RequestParser(const RequestParser &src);
    RequestParser &operator=(const RequestParser &rhs);

    static bool is_token_char(char c);
    static bool is_valid_token(const std::string &token);
    static std::string trim_ows(const std::string &value);
    static bool has_invalid_line_endings(const std::string &text,
        size_t start, size_t end, bool allow_trailing_cr);
    static bool is_hex_digit(char c);
    static bool has_bad_uri_char(const std::string &uri);
    static bool has_invalid_percent_encoding(const std::string &uri);
    static int sanitize_request_uri(const std::string &uri);
    static bool is_valid_decimal_port(const std::string &port);
    static bool is_valid_reg_name(const std::string &host);
    static bool is_valid_ip_literal(const std::string &literal);
    static int parse_request_line(const std::string &request_line,
        Request &req);
    static bool has_invalid_header_value_char(const std::string &value);
    static bool is_valid_host_value(const std::string &value);
    static int parse_headers(std::istringstream &iss, Request &req);
    static bool has_required_host(const Request &req);
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
        size_t body_start, Request &req, size_t &consumed);

public:
    /*
    函数：RequestParser::parseBuffer
    参数：
        - buffer：当前 client 已累计的原始字节，本函数不修改它。
        - req：成功时接收只读 Request 结果；每次调用前内部自动 reset。
        - server：当前 client 初步关联的 ServerConfig，作为非拥有型上下文保存。
        - consumed：成功时返回当前第一个完整 request 占用的字节数。
    返回：REQUEST_OK、REQUEST_INCOMPLETE、REQUEST_ERROR 或 ERROR_MAX_BODY_LENGTH。
    */
    static int parseBuffer(const std::string &buffer, Request &req,
        const ServerConfig *server, size_t &consumed);
};

/*
兼容函数：parseRequestBuffer
用途：保留旧 ServerManager 调用方式，内部只转发给 RequestParser::parseBuffer()。
说明：新代码推荐直接调用 RequestParser::parseBuffer()；保留此函数可让模块分阶段迁移。
*/
int parseRequestBuffer(const std::string &buffer, Request &req,
    const ServerConfig *server, size_t &consumed);

#endif