/*
文件：includes/RequestHandler.hpp
用途：声明普通 GET、POST、DELETE 处理入口，以及 method 到内部 RequestAction 的映射接口。
拆分说明：静态文件、目录、上传和删除分别由 srcs/Response 下对应实现文件完成，公开函数签名保持不变。
*/
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
