/*
文件：includes/RequestHandler.hpp
用途：声明普通 GET、POST、DELETE 处理入口，以及 method 到内部 RequestAction 的映射接口。
拆分说明：静态文件、目录、上传和删除分别由 srcs/Response 下对应实现文件完成。
*/
#ifndef REQUEST_HANDLER_HPP
#define REQUEST_HANDLER_HPP

/*
包含：<set>
用途：读取 EffectiveRoute 最终允许的方法集合。
*/
#include <set>

/*
包含：<string>
用途：接收 Request method 和文件路径文本。
*/
#include <string>

/*
包含：EffectiveRoute.hpp
用途：使用 RequestAction 和已经合并完成的 EffectiveRoute。
*/
#include "EffectiveRoute.hpp"

/*
包含：Request.hpp
用途：通过只读 getter 获取 method、headers、body、path 和配置来源。
*/
#include "Request.hpp"

/*
包含：Response.hpp
用途：返回已经完成或准备交给 CGI 的 Response 对象。
*/
#include "Response.hpp"

/*
函数：requestActionFromMethod
用途：把解析后的 HTTP method 映射为 Response 内部动作。
参数：method 来自 Request::getMethod()。
返回：GET、POST、DELETE 返回对应动作，其他合法 token 返回 ACTION_UNSUPPORTED。
*/
RequestAction requestActionFromMethod(const std::string &method);

/*
函数：isMethodAllowed
用途：判断当前合并路由是否允许指定已实现动作。
参数：action 来自 requestActionFromMethod()；allowMethods 来自 EffectiveRoute。
返回：集合中存在对应方法名时返回 true，否则返回 false。
*/
bool isMethodAllowed(RequestAction action,
                     const std::set<std::string> &allowMethods);

/*
函数：handleGet
用途：处理普通文件、目录 index 和 autoindex GET。
参数：request 提供连接策略；route 提供目标路径、目录标记和配置。
返回：最终 GET Response。
*/
Response handleGet(const Request &request, EffectiveRoute &route);

/*
函数：handlePost
用途：把 Request body 写入配置 upload_path，并处理类型、长度、目录和重名文件规则。
参数：request 提供 body/headers；route 提供上传目录和错误页。
返回：最终 POST Response。
*/
Response handlePost(const Request &request,
                    const EffectiveRoute &route);

/*
函数：handleDelete
用途：删除 normalized path 对应的普通文件，并拒绝目录和特殊文件。
参数：request 提供连接策略；route 提供真实 targetPath 和错误页。
返回：成功为 204，失败为对应错误 Response。
*/
Response handleDelete(const Request &request,
                      const EffectiveRoute &route);

#endif
