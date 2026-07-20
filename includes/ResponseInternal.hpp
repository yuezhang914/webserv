/*
文件：includes/ResponseInternal.hpp
用途：声明 Response 多个实现文件共同使用的最小内部字符串辅助函数。
边界：本文件不属于 Response 对外业务接口，只避免 CGI 与 Connection 实现重复 OWS 处理代码。
*/
#ifndef RESPONSE_INTERNAL_HPP
#define RESPONSE_INTERNAL_HPP

#include <string>

/* 仅供 Response 多个实现文件共享的 OWS 处理函数，不属于公共 Response.hpp 接口。 */
std::string responseTrimOws(const std::string &value);

#endif
