#include "CgiResponse.hpp"
#include <sstream>

std::string CgiResponse::serialize(const std::string &cgi_raw_output) {
    std::string http_response;

    if (cgi_raw_output.empty()) {
        http_response = "HTTP/1.1 502 Bad Gateway\r\n"
                        "Server: Webserv/1.0\r\n"
                        "Content-Length: 0\r\n"
                        "Connection: close\r\n\r\n";
        return http_response;
    }

    bool has_cgi_header = (cgi_raw_output.find("\r\n\r\n") != std::string::npos ||
                           cgi_raw_output.find("\n\n") != std::string::npos);

    if (has_cgi_header) {
        http_response = "HTTP/1.1 200 OK\r\n"
                        "Server: Webserv/1.0\r\n";
        http_response += cgi_raw_output;
    } else {
        std::stringstream ss;
        ss << cgi_raw_output.size();

        http_response = "HTTP/1.1 200 OK\r\n"
                        "Server: Webserv/1.0\r\n"
                        "Content-Type: text/html\r\n"
                        "Content-Length: " + ss.str() + "\r\n"
                        "\r\n"
                        + cgi_raw_output;
    }

    return http_response;
}