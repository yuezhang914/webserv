#ifndef RESPONSE_INTERNAL_HPP
#define RESPONSE_INTERNAL_HPP
#include <string>

// Removes optional spaces from both ends of a header value.
std::string responseTrimOws(const std::string &value);
#endif
