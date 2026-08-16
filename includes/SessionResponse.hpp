#ifndef SESSION_RESPONSE_HPP
#define SESSION_RESPONSE_HPP
#include "Response.hpp"
#include <string>
class Request;
class SessionStore;

// Checks whether the request uses a session demo path.
bool isSessionDemoPath(const std::string &path);

// Builds session demo response.
Response buildSessionDemoResponse(const Request &request, SessionStore &sessionStore);
#endif
