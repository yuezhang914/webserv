#ifndef CGI_HANDLER_HPP
#define CGI_HANDLER_HPP

#include "Request.hpp"
#include <string>

class CgiHandler {
private:
    const Request &_request;
    std::string    _script_path;

public:
    CgiHandler(const Request &request, const std::string &script_path);
    ~CgiHandler();

    std::string execute();
    std::string buildHttpResponse(const std::string &cgi_output) const;
};

#endif