/*
文件：includes/SessionResponse.hpp
用途：声明 Response 层与 Session Bonus 之间的最小接入接口，把三个虚拟演示路由转换成普通 Response。
模块边界：本文件不保存 Session 数据；长期状态仍由调用方传入的共享 SessionStore 管理。
接入规则：ResponseBuilder 的唯一 buildResponse(request, sessionStore) 入口在精确匹配演示路径时调用这些接口。
*/
#ifndef SESSION_RESPONSE_HPP
#define SESSION_RESPONSE_HPP

/*
包含：Response.hpp
用途：使用 Response 返回完整 HTTP 响应，并复用现有状态、header、body 和连接策略接口。
*/
#include "Response.hpp"

/*
包含：<string>
用途：在路径识别接口中直接使用 std::string，不依赖其他头文件的传递包含。
*/
#include <string>

class Request;
class SessionStore;

/*
函数：isSessionDemoPath
用途：判断 normalized request path 是否是本模块保留的 counter、login 或 logout 演示路径。
参数：path 由 Request::getPath() 传入，已经由 RequestParser 解码、规范化并去除 query。
返回：精确匹配三个演示路径时返回 true，其他路径返回 false。
实现逻辑：只做完整字符串比较，不接受前缀、后缀或模糊匹配，避免意外截获普通静态资源。
*/
bool isSessionDemoPath(const std::string &path);

/*
函数：buildSessionDemoResponse
用途：根据请求路径和方法调用 SessionDemo，并把结果转换成现有 Response 对象。
参数：request 来自 RequestParser；sessionStore 是 ServerManager 长期持有并传入的共享存储。
返回：成功时返回 counter/login/logout 响应；方法、正文或媒体类型不合法时返回对应 4xx；内部失败返回 500。
实现逻辑：读取 Cookie header，解析登录用户名，调用对应 SessionDemo，复制状态、Set-Cookie、Content-Type 和 body，同时继承 Request 的连接关闭策略。
*/
Response buildSessionDemoResponse(const Request &request,
                                  SessionStore &sessionStore);

#endif
