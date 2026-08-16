#ifndef SESSION_RESPONSE_INTERNAL_HPP
#define SESSION_RESPONSE_INTERNAL_HPP
#include <string>
class Request;

// Gets session login user name.
int extractSessionLoginUserName(const Request &request, std::string &userName);
#endif
