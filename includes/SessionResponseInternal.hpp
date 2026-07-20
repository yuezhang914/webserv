/*
文件：includes/SessionResponseInternal.hpp
用途：声明 Session Response 接入层内部使用的登录正文解析函数。
模块边界：本文件只连接 srcs/Response/SessionForm.cpp 与 SessionResponse.cpp，不属于 ServerManager 必须调用的公共接口。
*/
#ifndef SESSION_RESPONSE_INTERNAL_HPP
#define SESSION_RESPONSE_INTERNAL_HPP

/*
包含：<string>
用途：使用 std::string 输出从 text/plain 或 urlencoded body 中解析出的用户名。
*/
#include <string>

class Request;

/*
函数：extractSessionLoginUserName
用途：根据 Request 的 Content-Type 从 login body 提取用户名。
参数：request 由 RequestParser 完整解析；userName 是调用方提供的输出变量。
返回：成功返回 200；表单畸形返回 400；不支持的媒体类型返回 415。
实现逻辑：无 Content-Type 或 text/plain 使用完整 body；application/x-www-form-urlencoded 解码唯一 user 字段；任何失败清空输出。
*/
int extractSessionLoginUserName(const Request &request,
                                std::string &userName);

#endif
