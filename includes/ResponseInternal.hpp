/*
文件：includes/ResponseInternal.hpp
用途：声明 Response 多个实现文件共同使用的最小内部字符串辅助函数。
模块边界：本文件只连接 ResponseConnection.cpp 与 ResponseCgi.cpp，不属于 ServerManager 公共接口。
*/
#ifndef RESPONSE_INTERNAL_HPP
#define RESPONSE_INTERNAL_HPP

/*
包含：<string>
用途：接收并返回需要删除 HTTP optional whitespace 的文本。
*/
#include <string>

/*
函数：responseTrimOws
用途：删除字符串两端的普通空格和 tab，用于 Connection token 和 CGI header 解析。
参数：value 是调用方传入的完整字符串或字段片段。
返回：去除首尾 OWS 后的副本。
*/
std::string responseTrimOws(const std::string &value);

#endif
