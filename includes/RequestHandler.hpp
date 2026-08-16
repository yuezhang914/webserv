#ifndef REQUEST_HANDLER_HPP
#define REQUEST_HANDLER_HPP
#include "EffectiveRoute.hpp"
#include "Request.hpp"
#include "Response.hpp"

// Changes an HTTP method into an internal request action.
RequestAction requestActionFromMethod(const std::string &method);

// Checks whether the current route allows this HTTP method.
bool isMethodAllowed(RequestAction action, const std::set<std::string> &allowMethods);

// Handles get.
Response handleGet(const Request &request, EffectiveRoute &route);

// Handles post.
Response handlePost(const Request &request, const EffectiveRoute &route);

// Handles delete.
Response handleDelete(const Request &request, const EffectiveRoute &route);
#endif
