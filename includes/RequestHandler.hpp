#ifndef REQUEST_HANDLER_HPP
#define REQUEST_HANDLER_HPP

#include <set>
#include <string>

#include "EffectiveRoute.hpp"
#include "Request.hpp"
#include "Response.hpp"

/* 把 Request.getMethod() 映射为 Response 模块内部动作；未知合法 token 返回 ACTION_UNSUPPORTED。 */
RequestAction requestActionFromMethod(const std::string &method);
/* 判断当前 EffectiveRoute 是否允许该已实现动作。 */
bool isMethodAllowed(RequestAction action,
                     const std::set<std::string> &allowMethods);

Response handleGet(const Request &request, EffectiveRoute &route);
Response handlePost(const Request &request, const EffectiveRoute &route);
Response handleDelete(const Request &request, const EffectiveRoute &route);

#endif
