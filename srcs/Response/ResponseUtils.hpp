#ifndef RESPONSEUTILS_HPP
#define RESPONSEUTILS_HPP

#include "Response.hpp"

#define BUFFER_SIZE 4096
/* 根据 Request.uri 在 server.locations 中找最长前缀匹配；use_location 告诉调用者是否真的匹配到 location。 */
LocationConfig* getMatchingLocation(const Request& req, const ServerConfig* server, Response& res, bool &use_location);
/* 根据 URI 后缀和 location.cgi_extensions 判断请求是否需要执行 CGI。 */
bool isCGIRequest(const LocationConfig *loc, const std::string& uri);

#endif
