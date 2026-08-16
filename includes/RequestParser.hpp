#ifndef REQUEST_PARSER_HPP
#define REQUEST_PARSER_HPP
#include "Request.hpp"
#include <sstream>
class RequestParser
{
private:
    // Creates a new RequestParser object.
    RequestParser();
    // Checks whether the character is valid in an HTTP token.
    static bool is_token_char(char c);
    // Checks whether valid token.
    static bool is_valid_token(const std::string &token);
    // Removes extra spaces from ows.
    static std::string trim_ows(const std::string &value);
    // Checks whether invalid line endings.
    static bool has_invalid_line_endings(const std::string &text, size_t start, size_t end, bool allow_trailing_cr);
    // Changes one hexadecimal character into its number value.
    static int hex_value(char c);
    // Checks whether bad uri char.
    static bool has_bad_uri_char(const std::string &uri);
    // Checks whether invalid percent encoding.
    static bool has_invalid_percent_encoding(const std::string &uri);
    // Decodes path.
    static int decode_path(const std::string &encoded, std::string &decoded);
    // Makes a standard form of path.
    static int normalize_path(const std::string &decoded, std::string &normalized);
    // Splits and normalize uri.
    static int split_and_normalize_uri(const std::string &uri, std::string &path, std::string &query);
    // Checks whether valid http version syntax.
    static bool is_valid_http_version_syntax(const std::string &version);
    // Checks whether valid decimal port.
    static bool is_valid_decimal_port(const std::string &port);
    // Checks whether valid reg name.
    static bool is_valid_reg_name(const std::string &host);
    // Checks whether the text is a valid IPv4 address.
    static bool is_valid_ipv4_address(const std::string &address);
    // Counts IPv6 groups on one side of the address.
    static bool count_ipv6_side_groups(const std::string &side, bool allow_ipv4, size_t &group_count);
    // Checks whether valid ip literal.
    static bool is_valid_ip_literal(const std::string &literal);
    // Parses request line.
    static int parse_request_line(const std::string &request_line, Request &req);
    // Checks whether invalid header value char.
    static bool has_invalid_header_value_char(const std::string &value);
    // Checks whether valid host value.
    static bool is_valid_host_value(const std::string &value);
    // Parses headers.
    static int parse_headers(std::istringstream &iss, Request &req);
    // Parses content length.
    static int parse_content_length(const std::string &value, unsigned long body_limit, size_t &content_length);
    // Checks whether the request uses chunked transfer encoding.
    static int is_chunked_transfer_encoding(const Request &req, bool &has_te, bool &is_chunked);
    // Skips optional spaces in one chunk-size line.
    static void skip_chunk_ows(const std::string &line, size_t &pos);
    // Reads chunk extension token.
    static bool read_chunk_extension_token(const std::string &line, size_t &pos);
    // Reads chunk extension quoted string.
    static bool read_chunk_extension_quoted_string(const std::string &line, size_t &pos);
    // Checks whether valid chunk extensions.
    static bool is_valid_chunk_extensions(const std::string &line, size_t extension_start);
    // Reads chunk size for buffer.
    static int read_chunk_size_for_buffer(const std::string &line, size_t current_body_size, unsigned long body_limit, size_t &size);
    // Checks whether a trailer header name is not allowed.
    static bool is_forbidden_trailer_name(const std::string &lower_key);
    // Checks trailer line.
    static int validate_trailer_line(const std::string &line);
    // Finds chunked trailer end.
    static int find_chunked_trailer_end(const std::string &buffer, size_t pos, size_t &consumed);
    // Decodes complete chunked body.
    static int decode_complete_chunked_body(const std::string &buffer, size_t body_start, size_t decoded_size, Request &req);
    // Parses chunked buffer.
    static int parse_chunked_buffer(const std::string &buffer, size_t body_start, unsigned long body_limit, Request &req, size_t &consumed);
public:
    // Scans more chunked body data without blocking.
    static int advanceChunkedScan(const std::string &buffer, size_t &scan_pos, unsigned long body_limit, size_t &decoded_size, size_t &consumed);
    // Parses buffer.
    static int parseBuffer(const std::string &buffer, Request &req, const ServerConfig *server, size_t &consumed);
};
#endif
