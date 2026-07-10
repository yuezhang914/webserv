#ifndef REQUEST_PARSER_HPP
#define REQUEST_PARSER_HPP

#include "Request.hpp"

/*
函数：parseRequestBuffer
作用：ServerManager/ClientIO 架构使用的唯一 Request 解析入口。它只解析已经读入的字符串 buffer，不自己 recv。
参数：buffer 是 _client_buffers[clientFd]；req 是输出 Request；server 是当前 client 对应的 ServerConfig；consumed 返回成功解析后应从 buffer 删除的字节数。
*/
int parseRequestBuffer(const std::string& buffer, Request& req, const ServerConfig* server, size_t& consumed);

#endif
