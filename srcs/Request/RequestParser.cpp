

/*
文件：srcs/Request/RequestParser.cpp
HTTP request parser 实现。这个文件只负责从 ServerManager 已经读入的 raw buffer 中解析请求行、headers、Content-Length/chunked body，不负责网络 recv，也不负责 response 生成。
*/
#include "Request.hpp"
#include "ConfigRouteUtils.hpp"

/*
函数：parse_request_line
用途：解析 HTTP 请求第一行，例如 GET /ping.html HTTP/1.1。
参数来源：parseRequestBuffer 从 header 部分第一行读出 request_line。
实现逻辑：
    1. 找第一个空格，空格前是 method。
    2. 找第二个空格，两个空格之间是 uri。
    3. 第二个空格后面是 version。
    4. 如果缺少空格，说明请求行格式错，返回 REQUEST_ERROR。
    5. 把 method/uri/version 写进 req。
    6. 调用 sanitizeRequestUri 检查 URI 安全。
*/
static int parse_request_line(const std::string& request_line, Request& req) {
	size_t method_end = request_line.find(' ');
	if (method_end == std::string::npos)
		return REQUEST_ERROR;
	req.method = request_line.substr(0, method_end);
	size_t uri_end = request_line.find(' ', method_end + 1);
	if (uri_end == std::string::npos)
		return REQUEST_ERROR;
	req.uri = request_line.substr(method_end + 1, uri_end - method_end - 1);
	req.version = request_line.substr(uri_end + 1);
	return sanitizeRequestUri(req);
}

/*
函数：parse_headers
用途：解析请求头，把每个 Header 行存入 req.headers。
参数来源：parseRequestBuffer 把 header 部分放进 istringstream，第一行 request line 已读掉，剩余行交给本函数。
实现逻辑：
    1. 逐行 getline 读取 header。
    2. 如果遇到空行或只有 
 的行，表示 header 结束。
    3. 去掉行尾的 
。
    4. 查找冒号 :，没有冒号就是非法 header。
    5. 冒号前是 key，冒号后是 value。
    6. 去掉 value 前后的空格和 tab。
    7. key 转成小写后存入 req.headers。
例子："Content-Length: 3" 会保存为 headers["content-length"] = "3"。
*/
static int parse_headers(std::istringstream& iss, Request& req) {
	std::string line;
	while (std::getline(iss, line)) {
		if (line == "\r" || line.empty())
			break;
		if (!line.empty() && line[line.size() - 1] == '\r')
			line.erase(line.size() - 1);
		size_t colon_pos = line.find(':');
		if (colon_pos == std::string::npos)
			return REQUEST_ERROR;
		std::string key = line.substr(0, colon_pos);
		std::string value = line.substr(colon_pos + 1);
		size_t first = value.find_first_not_of(" \t");
		if (first == std::string::npos)
			value.clear();
		else {
			value.erase(0, first);
			value.erase(value.find_last_not_of(" \t") + 1);
		}
		req.headers[to_lower(key)] = value;
	}
	return REQUEST_OK;
}

/*
函数：read_chunk_size_for_buffer
用途：parseRequestBuffer 专用的 chunk size 解析工具。
参数来源：chunked body 中的一行十六进制长度，例如 "A" 或 "A;ext=value"。
输出：成功时把十六进制长度写入 size；格式错误返回 REQUEST_ERROR。
实现逻辑：忽略分号后的 chunk extension，用 strtoul 按 16 进制转换，并检查整段数字是否合法。
修改说明：RequestBody.cpp 原本有一个 static read_chunk_size，只在该文件内部可见；为了让新的非破坏式 parseRequestBuffer 也能解析 chunked，这里补一个局部工具函数。
*/
static int read_chunk_size_for_buffer(const std::string& line, size_t& size) {
	size_t semicolon = line.find(';');
	std::string number = line.substr(0, semicolon);
	if (number.empty())
		return REQUEST_ERROR;
	char* end = NULL;
	errno = 0;
	unsigned long value = std::strtoul(number.c_str(), &end, 16);
	if (errno != 0 || end == number.c_str() || *end != '\0')
		return REQUEST_ERROR;
	size = static_cast<size_t>(value);
	return REQUEST_OK;
}

/*
函数：parse_chunked_buffer
用途：在不修改原始 buffer 的前提下解析 Transfer-Encoding: chunked 的 body。
参数来源：parseRequestBuffer 判断 header 中 transfer-encoding 是 chunked 后调用；buffer 是 ServerManager 累积出的完整原始数据。
输出：成功时把还原后的 body 写入 req.body，并通过 consumed 告诉调用方应删除多少字节；数据没收完整返回 REQUEST_INCOMPLETE。
实现逻辑：
    1. pos 从 body_start 开始，body 临时保存还原后的内容。
    2. 逐个读取 chunk size 行，使用 read_chunk_size 按十六进制转成长度。
    3. 如果当前 chunk 数据还没收完整，返回 REQUEST_INCOMPLETE。
    4. 把每个 chunk data 追加进 body，并用 getEffectiveBodyLimit(req.config, req.uri) 检查是否超过本次请求真实限制。
    5. 遇到 0 长度 chunk 后，确认结尾 CRLF 已收到，设置 req.body 和 consumed。
修改说明：这是 parseRequestBuffer 使用的非破坏式 chunked 解析函数；它不修改外部 buffer，只通过 consumed 告诉 ServerManager 应该删除多少字节。
意义：ServerManager 可以统一在外层根据 consumed 清理 buffer，Request 模块不再直接管理 fd 对应的 map。
*/
static int parse_chunked_buffer(const std::string& buffer, size_t body_start, Request& req, size_t& consumed) {
	size_t pos = body_start;
	std::string body;
	while (true) {
		size_t line_end = buffer.find("\r\n", pos);
		if (line_end == std::string::npos)
			return REQUEST_INCOMPLETE;
		size_t chunk_size = 0;
		if (read_chunk_size_for_buffer(buffer.substr(pos, line_end - pos), chunk_size) != REQUEST_OK)
			return REQUEST_ERROR;
		pos = line_end + 2;
		if (chunk_size == 0) {
			size_t end = buffer.find("\r\n", pos);
			if (end == std::string::npos)
				return REQUEST_INCOMPLETE;
			req.body = body;
			consumed = end + 2;
			return REQUEST_OK;
		}
		if (buffer.size() < pos + chunk_size + 2)
			return REQUEST_INCOMPLETE;
		if (buffer.substr(pos + chunk_size, 2) != "\r\n")
			return REQUEST_ERROR;
		body.append(buffer, pos, chunk_size);
		if (body.size() > getEffectiveBodyLimit(req.config, req.uri))
			return ERROR_MAX_BODY_LENGTH;
		pos += chunk_size + 2;
	}
}

/*
函数：parseRequestBuffer
用途：把已经读入内存的 HTTP raw buffer 解析成 Request，是 ServerManager/ClientIO 架构下 Request 模块给 Server 使用的新接口。
输入来源：ServerManager::handleClientRead() 先调用 ClientIO::readFromNet()，再把读到的字节 append 到 _client_buffers[clientFd]，最后把这个字符串传给本函数。
输出去向：如果返回 REQUEST_OK，req 会被填好，consumed 表示这一个完整请求占用的字节数；ServerManager 应 erase 掉这些字节，再调用 buildResponse(req)。
实现逻辑：
    1. 本函数不调用 recv，不接触 fd，也不修改 buffer。
    2. 查找 \r\n\r\n，确认 headers 是否完整；没找到就返回 REQUEST_INCOMPLETE。
    3. 解析 request line，得到 method/uri/version。
    4. 把 server 写入 req.config，让后续 body limit、Response、CGI 都能查当前 server 配置。
    5. 解析 headers。
    6. 如果是非 chunked 且有 Content-Length，先检查长度格式和 getEffectiveBodyLimit(req.config, req.uri)。
    7. Content-Length body 完整时截取 req.body，并设置 consumed。
    8. chunked body 交给 parse_chunked_buffer；它会边解析边检查 effective body limit。
修改说明：这是删除旧 serverLoop 兼容入口后的唯一 Request 解析入口。ServerManager 负责网络读取，RequestParser 只解析字符串。
意义：Request 模块不再接触 socket fd，避免网络读取职责和 HTTP 解析职责混在一起。
*/
int parseRequestBuffer(const std::string& buffer, Request& req, const ServerConfig* server, size_t& consumed) {
	consumed = 0;
	req.method.clear();
	req.uri.clear();
	req.version.clear();
	req.headers.clear();
	req.body.clear();
	req.config = server;
	req.use_location = false;
	req.closeConnection = true;
	size_t header_end = buffer.find("\r\n\r\n");
	if (header_end == std::string::npos)
		return REQUEST_INCOMPLETE;
	size_t body_start = header_end + 4;
	std::istringstream iss(buffer.substr(0, header_end));
	std::string request_line;
	if (!std::getline(iss, request_line))
		return REQUEST_ERROR;
	if (!request_line.empty() && request_line[request_line.size() - 1] == '\r')
		request_line.erase(request_line.size() - 1);
	if (parse_request_line(request_line, req) != REQUEST_OK)
		return REQUEST_ERROR;
	if (parse_headers(iss, req) != REQUEST_OK)
		return REQUEST_ERROR;
	bool is_chunked = req.headers.count("transfer-encoding") && to_lower(req.headers["transfer-encoding"]) == "chunked";
	size_t content_length = 0;
	unsigned long body_limit = getEffectiveBodyLimit(req.config, req.uri);
	if (!is_chunked && req.headers.count("content-length")) {
		char* endptr;
		long len = strtol(req.headers["content-length"].c_str(), &endptr, 10);
		if (*endptr != '\0' || len < 0 || static_cast<unsigned long>(len) > body_limit)
			return ERROR_MAX_BODY_LENGTH;
		content_length = static_cast<size_t>(len);
	}
	if (is_chunked)
		return parse_chunked_buffer(buffer, body_start, req, consumed);
	if (buffer.length() < body_start + content_length)
		return REQUEST_INCOMPLETE;
	req.body = buffer.substr(body_start, content_length);
	consumed = body_start + content_length;
	return REQUEST_OK;
}

