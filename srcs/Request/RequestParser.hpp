#ifndef REQUEST_PARSER_HPP
#define REQUEST_PARSER_HPP

#include "Request.hpp"

/*
函数：parseRequestBuffer
作用：ServerManager/ClientIO 架构使用的唯一 Request 解析入口。它只解析已经读入的字符串 buffer，不自己 recv。
参数：buffer 是 _client_buffers[clientFd]；req 是输出 Request；server 是当前 client 对应的 ServerConfig；consumed 返回成功解析后应从 buffer 删除的字节数。
返回：REQUEST_OK 表示解析出完整请求；REQUEST_INCOMPLETE 表示继续等数据；REQUEST_ERROR 表示请求格式非法；ERROR_MAX_BODY_LENGTH 表示 body 数字合法但超过当前 max_body_size。
格式边界：实现中会检查 request line、严格 CRLF、Host authority、header key/value、重复关键 header、Content-Length、Transfer-Encoding、chunk-size/trailer 上限、chunked framing、URI 安全和 header 总大小。
*/
int parseRequestBuffer(const std::string& buffer, Request& req, const ServerConfig* server, size_t& consumed);

#endif