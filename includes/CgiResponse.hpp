#ifndef CGI_RESPONSE_HPP
#define CGI_RESPONSE_HPP

#include <string>

class CgiResponse {
public:
    static std::string serialize(const std::string &cgi_raw_output);
};

#endif