#pragma once

#include "Request.hpp"

struct Request;

/*
函数：parse_body_normal
作用：解析普通 Content-Length 请求体。
输入来源：parse_request 已经找到了 header/body 分界位置，并从 Content-Length 得到 content_length。
输出去向：成功时把 req.body 填好，并删除 buffers[socket] 中已经消费过的数据。
*/
int parse_body_normal(const std::string& request, size_t body_start, size_t content_length, Request& req, std::map<int, std::string>& buffers, int socket);

/*
函数：parse_body_chunked
作用：解析 Transfer-Encoding: chunked 的请求体，把多个 chunk 合并成普通 body。
输入来源：parse_request 判断 headers["transfer-encoding"] == "chunked" 后调用。
输出去向：成功时把合并后的 body 写入 req.body，并清理 client buffer。
*/
int parse_body_chunked(const std::string& request, size_t body_start, Request& req, std::map<int, std::string>& buffers, int socket);
