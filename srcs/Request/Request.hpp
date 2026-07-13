#ifndef REQUEST_HPP
#define REQUEST_HPP

#include "Webserv.hpp"


/*
枚举：RequestStatus
作用：让 ServerManager 明确知道当前 buffer 的解析状态。
*/
enum RequestStatus {
	REQUEST_OK = 0,          /* 请求完整且合法，Request 已经填好，可以进入 buildResponse 或 CGI。 */
	REQUEST_INCOMPLETE = 1,  /* 请求还没有收完整，ServerManager 继续监听 POLLIN，等待下次 readFromNet。 */
	REQUEST_ERROR = -1       /* 请求格式错误，ServerManager 应生成错误响应或关闭连接。 */
};

/*
结构体：Request
作用：保存一次 HTTP request 解析后的结果。
从哪来：ServerManager 调用 ClientIO::readFromNet() 得到 raw buffer 后，把 buffer 交给 parseRequestBuffer()；parseRequestBuffer() 填充本结构体。
给谁用：Response/EffectiveRoute/CGI 读取 method、uri、headers、body 和 config 来判断方法、路径、body、CGI 环境变量等。
*/
struct Request {
	/* HTTP 方法。来源：请求行第一个单词，例如 GET /ping.html HTTP/1.1 中的 GET。 */
	std::string method;
	/* 浏览器请求路径。来源：请求行第二段，例如 /ping.html 或 /test_cgi.py?name=Tom。注意：这不是磁盘真实路径。 */
	std::string uri;
	/* HTTP 版本。来源：请求行第三段，例如 HTTP/1.1。Response 会检查它是否支持。 */
	std::string version;
	/* 请求头表。来源：Host: xxx、Content-Length: xxx 等 header；key 会转成小写，方便统一查找。 */
	std::map<std::string, std::string> headers;
	/* 请求体。来源：header 后面的 body，主要用于 POST 和 CGI stdin。GET 通常为空。 */
	std::string body;
	/* 当前 client 连接对应的 server 配置。来源：ServerManager 的 _client_to_srv_map。 */
	const ServerConfig* config;
	// 给 ServerManager 预留的连接控制字段；RequestParser 只初始化它，真正的设置和使用尚未在 Request 模块中完成。
	/* 告诉 ServerManager 响应发完后是否关闭 socket。来源：Connection header 和 HTTP 版本判断。 */
	bool closeConnection;
};

/*
函数：parseRequestBuffer
输入：ServerManager/ClientIO 已经读入的 raw buffer、输出 Request、当前 client 对应的 ServerConfig、已消费字节数 consumed。
输出：REQUEST_OK、REQUEST_INCOMPLETE、REQUEST_ERROR 或 ERROR_MAX_BODY_LENGTH。
实现逻辑：本函数不调用 recv，也不修改传入 buffer；它只判断 buffer 里是否已经包含一个完整且合法的 HTTP/1.1 request。成功时填好 req，并把 consumed 设置成本次请求占用的字节数，调用方再从自己的 _client_buffers[clientFd] 中 erase(consumed)。
格式检查：会严格检查 request line 三段格式、CRLF 行结束、Host 必填及 authority 格式、header key/value、重复 Host/Content-Length/Transfer-Encoding、Content-Length 纯数字、Transfer-Encoding 只支持 chunked、header/chunk-size/trailer 大小、URI percent-encoding 与路径穿越、chunked framing 和 body size。
状态区分：格式错误返回 REQUEST_ERROR；请求未收完整返回 REQUEST_INCOMPLETE；body 数字合法但超过当前 server/location max_body_size 时返回 ERROR_MAX_BODY_LENGTH。
*/
int parseRequestBuffer(const std::string& buffer, Request& req, const ServerConfig* server, size_t& consumed);

/* 检查 URI 是否安全：必须以 / 开头；percent-encoding 必须是 %XX；不能有控制字符、反斜杠、%00、%2e、%2f、../ 等路径穿越风险。 */
int sanitizeRequestUri(Request& req);
/* 把字符串转成小写。主要用于 header key 和 Host/server_name 比较，避免大小写差异导致匹配失败。 */
std::string to_lower(const std::string& str);

#endif