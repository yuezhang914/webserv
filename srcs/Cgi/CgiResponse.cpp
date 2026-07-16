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
        http_response = "HTTP/1.1 200 OK\r\n"
                        "Server: Webserv/1.0\r\n";
        http_response += cgi_raw_output;
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