#include "CgiResponse.hpp"
#include <sstream>

/*
函数：CgiResponse::serialize
用途：把cgi的原生输出转变为符合HTTP规范的输出
参数：Cgi的原生输出
实现逻辑：如果原生输出为空， 则HTTP response 返回502； 如果原生输出已经带有header, 则无需另外加header, 否则需要添加http header
返回： 返回HTTP 格式的response
*/
std::string CgiResponse::serialize(const std::string &cgi_raw_output)
{
    std::string http_response;

    if (cgi_raw_output.empty())
    {
        http_response = "HTTP/1.1 502 Bad Gateway\r\n"
                        "Server: Webserv/1.0\r\n"
                        "Content-Length: 0\r\n"
                        "Connection: close\r\n\r\n";
        return http_response;
    }
    bool has_cgi_header = (cgi_raw_output.find("\r\n\r\n") != std::string::npos ||
                           cgi_raw_output.find("\n\n") != std::string::npos);
    if (has_cgi_header)
    {
        size_t header_end = cgi_raw_output.find("\r\n\r\n");
        size_t delimiter_len = 4;
        if (header_end == std::string::npos)
        {
            header_end = cgi_raw_output.find("\n\n");
            delimiter_len = 2;
        }

        // 剥离出脚本自带的头部和真正的 Body
        std::string script_headers = cgi_raw_output.substr(0, header_end);
        std::string body = cgi_raw_output.substr(header_end + delimiter_len);

        // 用 C++98 稳健计算 Body 长度
        std::stringstream ss;
        ss << body.size();

        // 强行把 Content-Length 拼装进响应报文中，并用 Connection: close 或 keep-alive 封口
        http_response = "HTTP/1.1 200 OK\r\n"
                        "Server: Webserv/1.0\r\n" +
                        script_headers + "\r\n"
                                         "Content-Length: " +
                        ss.str() + "\r\n"
                                   "Connection: keep-alive\r\n"
                                   "\r\n" +
                        body;
    }
    else
    {
        std::stringstream ss;
        ss << cgi_raw_output.size();
        http_response = "HTTP/1.1 200 OK\r\n"
                        "Server: Webserv/1.0\r\n"
                        "Content-Type: text/html\r\n"
                        "Content-Length: " +
                        ss.str() + "\r\n"
                                   "\r\n" +
                        cgi_raw_output;
    }
    return http_response;
}