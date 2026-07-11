#ifndef REQUEST_HANDLER_HPP
#define REQUEST_HANDLER_HPP

#include "Response.hpp"

Response handleGet(const Request& request, EffectiveRoute& eff);
Response handlePost(const Request& request, const EffectiveRoute& eff);
Response handleDelete(const Request& request, const EffectiveRoute& eff);
bool isMethodAllowed(int method, std::set<std::string> allow_methods);

#endif