/*
文件：srcs/Request/RequestParser.cpp
HTTP request parser 实现。这个文件只负责从 ServerManager 已经读入的 raw buffer 中解析请求行、headers、Content-Length/chunked body，不负责网络 recv，也不负责 response 生成。
本版强化点：严格区分 400 与 413；校验 request line、header key、重复 Host/Content-Length、Transfer-Encoding、Host 必填、header 总大小；chunked 解析支持 trailer 消费，并在读到巨大 chunk size 时提前判断 body limit。
*/
#include "RequestParser.hpp"
#include "../Config/ConfigRouteUtils.hpp"

static const size_t MAX_HEADER_SIZE = 8192;

/*
函数：is_token_char
用途：判断 HTTP token 中的一个字符是否合法。
参数来源：parse_request_line 用它检查 method；parse_headers 用它检查 header key。
实现逻辑：
    1. 字母和数字直接允许。
    2. HTTP token 常用符号 ! # $ % & ' * + - . ^ _ ` | ~ 允许。
    3. 空格、tab、冒号、控制字符、括号、斜杠等不允许。
为什么需要：method 和 header key 不能是空字符串，也不能包含空格或控制字符；否则后续 map 查找和路由判断会被非法输入污染。
*/
static bool is_token_char(char c) {
	unsigned char uc = static_cast<unsigned char>(c);
	if ((uc >= 'A' && uc <= 'Z') || (uc >= 'a' && uc <= 'z') || (uc >= '0' && uc <= '9'))
		return true;
	return c == '!' || c == '#' || c == '$' || c == '%' || c == '&'
		|| c == '\'' || c == '*' || c == '+' || c == '-' || c == '.'
		|| c == '^' || c == '_' || c == '`' || c == '|' || c == '~';
}

/*
函数：is_valid_token
用途：检查一整个 method 或 header key 是否是合法 HTTP token。
参数来源：parse_request_line 传入 method；parse_headers 传入冒号前的 key。
实现逻辑：
    1. 空字符串非法。
    2. 逐字符调用 is_token_char。
    3. 任意字符非法则返回 false。
意义：拒绝空 header 名、带空格的 header 名、带冒号或控制字符的 header 名；避免 `: value`、` Host: x`、`Bad Header: x` 这种请求被当成正常请求。
*/
static bool is_valid_token(const std::string& token) {
	if (token.empty())
		return false;
	for (size_t i = 0; i < token.size(); ++i) {
		if (!is_token_char(token[i]))
			return false;
	}
	return true;
}

/*
函数：trim_ows
用途：去掉 header value 或 transfer-coding 两端的 OWS 空白，也就是空格和 tab。
参数来源：parse_headers 处理冒号后的 value；parse_transfer_encoding 处理逗号切出来的编码片段。
实现逻辑：
    1. 找第一个不是空格/tab 的位置。
    2. 找最后一个不是空格/tab 的位置。
    3. 如果全是空白，返回空字符串；否则 substr 返回中间部分。
*/
static std::string trim_ows(const std::string& value) {
	size_t first = value.find_first_not_of(" \t");
	if (first == std::string::npos)
		return "";
	size_t last = value.find_last_not_of(" \t");
	return value.substr(first, last - first + 1);
}

/*
函数：parse_request_line
用途：解析 HTTP 请求第一行，例如 GET /ping.html HTTP/1.1。
参数来源：parseRequestBuffer 从 header 部分第一行读出 request_line。
实现逻辑：
    1. 使用 istringstream 把请求行拆成 method、uri、version 三段。
    2. 如果三段缺任意一段，或者存在第四段 token，说明请求行格式错误。
    3. 检查 method 是合法 token，拒绝空 method 或带非法字符的 method。
    4. 检查 version 必须是 HTTP/1.1；本项目只实现 HTTP/1.1 请求解析。
    5. 把 method/uri/version 写进 req。
    6. 调用 sanitizeRequestUri 检查 URI 安全。
修改说明：旧版只找两个空格，会把 `GET / HTTP/1.1 extra` 的 version 保存成 `HTTP/1.1 extra`；新版要求请求行必须正好三段。
*/
static int parse_request_line(const std::string& request_line, Request& req) {
	std::istringstream line_stream(request_line);
	std::string extra;
	if (!(line_stream >> req.method >> req.uri >> req.version))
		return REQUEST_ERROR;
	if (line_stream >> extra)
		return REQUEST_ERROR;
	if (!is_valid_token(req.method))
		return REQUEST_ERROR;
	if (req.version != "HTTP/1.1")
		return REQUEST_ERROR;
	return sanitizeRequestUri(req);
}

/*
函数：parse_headers
用途：解析请求头，把每个 Header 行存入 req.headers。
参数来源：parseRequestBuffer 把 header 部分放进 istringstream，第一行 request line 已读掉，剩余行交给本函数。
实现逻辑：
    1. 逐行 getline 读取 header，并去掉行尾 \r。
    2. 每行必须包含冒号；冒号前是 key，冒号后是 value。
    3. key 必须是合法 HTTP token，不能为空，不能包含空格、tab、控制字符或冒号。
    4. value 去掉两端空格和 tab 后保存。
    5. key 转成小写后写入 req.headers。
    6. 重复 Host 直接返回 REQUEST_ERROR。
    7. 重复 Content-Length 直接返回 REQUEST_ERROR，避免多个长度造成请求走私风险。
例子："Content-Length: 3" 会保存为 headers["content-length"] = "3"。
修改说明：旧版只检查有没有冒号，会接受空 key、带空格 key，并且重复 Content-Length 会被后一个覆盖；新版在解析阶段直接拒绝。
*/
static int parse_headers(std::istringstream& iss, Request& req) {
	std::string line;
	while (std::getline(iss, line)) {
		if (!line.empty() && line[line.size() - 1] == '\r')
			line.erase(line.size() - 1);
		if (line.empty())
			break;
		size_t colon_pos = line.find(':');
		if (colon_pos == std::string::npos)
			return REQUEST_ERROR;
		std::string key = line.substr(0, colon_pos);
		if (!is_valid_token(key))
			return REQUEST_ERROR;
		std::string lower_key = to_lower(key);
		if ((lower_key == "content-length" || lower_key == "host")
			&& req.headers.find(lower_key) != req.headers.end())
			return REQUEST_ERROR;
		req.headers[lower_key] = trim_ows(line.substr(colon_pos + 1));
	}
	return REQUEST_OK;
}

/*
函数：has_required_host
用途：检查 HTTP/1.1 请求是否带有非空 Host header。
参数来源：parseRequestBuffer 在 parse_headers 成功之后调用。
实现逻辑：
    1. 本项目解析阶段要求 version 为 HTTP/1.1。
    2. HTTP/1.1 请求必须存在 Host。
    3. Host 值去掉空白后不能为空。
意义：server_name / virtual host 选择依赖 Host；缺少 Host 的 HTTP/1.1 请求应当作为 bad request 处理。
*/
static bool has_required_host(const Request& req) {
	std::map<std::string, std::string>::const_iterator it = req.headers.find("host");
	if (it == req.headers.end())
		return false;
	return !trim_ows(it->second).empty();
}

/*
函数：parse_content_length
用途：严格解析 Content-Length，并区分格式错误和 body 超限。
参数来源：parseRequestBuffer 在发现 content-length header 后调用；body_limit 来自 getEffectiveBodyLimit(req.config, req.uri)。
输出：成功时把长度写入 content_length；格式错误返回 REQUEST_ERROR；数字合法但超过 body_limit 或整数范围时返回 ERROR_MAX_BODY_LENGTH。
实现逻辑：
    1. Content-Length 必须非空，而且每个字符都必须是数字。
    2. 不接受 +5、-1、空格、abc、12x 等格式。
    3. 手动累加数字，避免 strtol 溢出后难以区分错误类型。
    4. 一旦发现值超过 body_limit，立即返回 ERROR_MAX_BODY_LENGTH。
修改说明：旧版把格式错误和超过限制都返回 ERROR_MAX_BODY_LENGTH，导致 `Content-Length: abc` 会被当成 413；新版格式错返回 400，超过限制才返回 413。
*/
static int parse_content_length(const std::string& value, unsigned long body_limit, size_t& content_length) {
	if (value.empty())
		return REQUEST_ERROR;
	unsigned long result = 0;
	unsigned long max_ulong = static_cast<unsigned long>(-1);
	for (size_t i = 0; i < value.size(); ++i) {
		if (value[i] < '0' || value[i] > '9')
			return REQUEST_ERROR;
		unsigned long digit = static_cast<unsigned long>(value[i] - '0');
		if (result > (max_ulong - digit) / 10)
			return ERROR_MAX_BODY_LENGTH;
		result = result * 10 + digit;
		if (result > body_limit)
			return ERROR_MAX_BODY_LENGTH;
	}
	content_length = static_cast<size_t>(result);
	return REQUEST_OK;
}

/*
函数：is_chunked_transfer_encoding
用途：判断 Transfer-Encoding 是否为本项目支持的 chunked，并拒绝其他传输编码。
参数来源：parseRequestBuffer 在 headers 中发现 transfer-encoding 后调用。
输出：如果没有 transfer-encoding，has_te=false、is_chunked=false；如果值正好是 chunked，二者都为 true；如果是 gzip、compress、gzip, chunked 等本项目不支持的形式，返回 REQUEST_ERROR。
实现逻辑：
    1. 查找 headers["transfer-encoding"]。
    2. 若不存在，说明没有 TE，返回 REQUEST_OK。
    3. 若存在，转小写并去掉两端空白。
    4. 只有严格等于 "chunked" 才允许。
意义：防止非 chunked body 被当成下一个 request 的开头；也防止同时出现复杂 transfer coding 时 parser 无法正确还原 body。
*/
static int is_chunked_transfer_encoding(const Request& req, bool& has_te, bool& is_chunked) {
	has_te = false;
	is_chunked = false;
	std::map<std::string, std::string>::const_iterator it = req.headers.find("transfer-encoding");
	if (it == req.headers.end())
		return REQUEST_OK;
	has_te = true;
	std::string value = to_lower(trim_ows(it->second));
	if (value != "chunked")
		return REQUEST_ERROR;
	is_chunked = true;
	return REQUEST_OK;
}

/*
函数：read_chunk_size_for_buffer
用途：parseRequestBuffer 专用的 chunk size 解析工具，同时提前检查 body limit。
参数来源：chunked body 中的一行十六进制长度，例如 "A" 或 "A;ext=value"；current_body_size 是已经解码出的 body 大小；body_limit 是当前请求真实 body 限制。
输出：成功时把十六进制长度写入 size；格式错误返回 REQUEST_ERROR；当前累计 body 加上本 chunk 会超过限制时返回 ERROR_MAX_BODY_LENGTH。
实现逻辑：
    1. 忽略分号后的 chunk extension。
    2. 分号前必须是非空十六进制数字，不接受空格和非法字符。
    3. 手动按 16 进制累加，避免溢出。
    4. 每增加一位都检查 current_body_size + size 是否超过 body_limit。
修改说明：旧版只有在整个 chunk 数据收完并 append 到 body 后才检查大小；新版读到 chunk size 后就能提前拒绝巨大 chunk，避免一直等待一个注定超过限制的 body。
*/
static int read_chunk_size_for_buffer(const std::string& line, size_t current_body_size, unsigned long body_limit, size_t& size) {
	size_t semicolon = line.find(';');
	std::string number = line.substr(0, semicolon);
	if (number.empty())
		return REQUEST_ERROR;
	unsigned long result = 0;
	unsigned long max_ulong = static_cast<unsigned long>(-1);
	for (size_t i = 0; i < number.size(); ++i) {
		char c = number[i];
		unsigned long digit;
		if (c >= '0' && c <= '9')
			digit = static_cast<unsigned long>(c - '0');
		else if (c >= 'a' && c <= 'f')
			digit = static_cast<unsigned long>(c - 'a' + 10);
		else if (c >= 'A' && c <= 'F')
			digit = static_cast<unsigned long>(c - 'A' + 10);
		else
			return REQUEST_ERROR;
		if (result > (max_ulong - digit) / 16)
			return ERROR_MAX_BODY_LENGTH;
		result = result * 16 + digit;
		if (current_body_size + result > body_limit)
			return ERROR_MAX_BODY_LENGTH;
	}
	size = static_cast<size_t>(result);
	return REQUEST_OK;
}

/*
函数：find_chunked_trailer_end
用途：在 0-size chunk 之后找到 chunked body 的真正结束位置，并正确消费 trailer。
参数来源：parse_chunked_buffer 在读到 chunk_size == 0 后调用；pos 指向 0-size chunk 行后面的内容。
输出：如果 trailer 还没收完整，返回 REQUEST_INCOMPLETE；如果 trailer 格式非法，返回 REQUEST_ERROR；成功时把 consumed 设置为整个 chunked request 已消费到的位置。
实现逻辑：
    1. 如果 pos 位置马上是 "\r\n"，说明没有 trailer，chunked body 结束，consumed = pos + 2。
    2. 否则查找 "\r\n\r\n"，表示 trailer header block 结束。
    3. 找不到说明数据还没收完整，返回 REQUEST_INCOMPLETE。
    4. 找到后逐行检查 trailer key 是否是合法 token；本项目忽略 trailer 内容，但必须完整消费。
修改说明：旧版只找下一个 "\r\n"，遇到 trailer 会只消费第一行，留下多余 CRLF 干扰下一个请求；新版会完整吃掉 trailer block。
*/
static int find_chunked_trailer_end(const std::string& buffer, size_t pos, size_t& consumed) {
	if (buffer.size() < pos + 2)
		return REQUEST_INCOMPLETE;
	if (buffer.substr(pos, 2) == "\r\n") {
		consumed = pos + 2;
		return REQUEST_OK;
	}
	size_t trailer_end = buffer.find("\r\n\r\n", pos);
	if (trailer_end == std::string::npos)
		return REQUEST_INCOMPLETE;
	std::istringstream trailer_stream(buffer.substr(pos, trailer_end - pos));
	std::string line;
	while (std::getline(trailer_stream, line)) {
		if (!line.empty() && line[line.size() - 1] == '\r')
			line.erase(line.size() - 1);
		size_t colon_pos = line.find(':');
		if (colon_pos == std::string::npos)
			return REQUEST_ERROR;
		if (!is_valid_token(line.substr(0, colon_pos)))
			return REQUEST_ERROR;
	}
	consumed = trailer_end + 4;
	return REQUEST_OK;
}

/*
函数：parse_chunked_buffer
用途：在不修改原始 buffer 的前提下解析 Transfer-Encoding: chunked 的 body。
参数来源：parseRequestBuffer 判断 header 中 transfer-encoding 是 chunked 后调用；buffer 是 ServerManager 累积出的完整原始数据。
输出：成功时把还原后的 body 写入 req.body，并通过 consumed 告诉调用方应删除多少字节；数据没收完整返回 REQUEST_INCOMPLETE；格式错返回 REQUEST_ERROR；超过 body limit 返回 ERROR_MAX_BODY_LENGTH。
实现逻辑：
    1. pos 从 body_start 开始，body 临时保存还原后的内容。
    2. 逐个读取 chunk size 行，使用 read_chunk_size_for_buffer 按十六进制转成长度。
    3. 读到 chunk size 后立刻检查 body.size() + chunk_size 是否超过 getEffectiveBodyLimit。
    4. 如果当前 chunk 数据还没收完整，返回 REQUEST_INCOMPLETE。
    5. 把每个 chunk data 追加进 body。
    6. 遇到 0 长度 chunk 后，调用 find_chunked_trailer_end 完整消费 trailer，再设置 req.body 和 consumed。
修改说明：新版不再把 trailer 留给下一次解析，也不再等待超大 chunk 全部到齐才报 413。
*/
static int parse_chunked_buffer(const std::string& buffer, size_t body_start, Request& req, size_t& consumed) {
	size_t pos = body_start;
	std::string body;
	unsigned long body_limit = getEffectiveBodyLimit(req.config, req.uri);
	while (true) {
		size_t line_end = buffer.find("\r\n", pos);
		if (line_end == std::string::npos)
			return REQUEST_INCOMPLETE;
		size_t chunk_size = 0;
		int size_status = read_chunk_size_for_buffer(buffer.substr(pos, line_end - pos), body.size(), body_limit, chunk_size);
		if (size_status != REQUEST_OK)
			return size_status;
		pos = line_end + 2;
		if (chunk_size == 0) {
			int trailer_status = find_chunked_trailer_end(buffer, pos, consumed);
			if (trailer_status != REQUEST_OK)
				return trailer_status;
			req.body = body;
			return REQUEST_OK;
		}
		if (buffer.size() < pos + chunk_size + 2)
			return REQUEST_INCOMPLETE;
		if (buffer.substr(pos + chunk_size, 2) != "\r\n")
			return REQUEST_ERROR;
		body.append(buffer, pos, chunk_size);
		pos += chunk_size + 2;
	}
}

/*
函数：parseRequestBuffer
用途：把已经读入内存的 HTTP raw buffer 解析成 Request，是 ServerManager/ClientIO 架构下 Request 模块给 Server 使用的唯一解析入口。
输入来源：ServerManager::handleClientRead() 先调用 ClientIO::readFromNet()，再把读到的字节 append 到 _client_buffers[clientFd]，最后把这个字符串传给本函数。
输出去向：如果返回 REQUEST_OK，req 会被填好，consumed 表示这一个完整请求占用的字节数；ServerManager 应 erase 掉这些字节，再调用 buildResponse(req)。
实现逻辑：
    1. 本函数不调用 recv，不接触 fd，也不修改 buffer。
    2. 先查找 \r\n\r\n；没找到且 buffer 超过 MAX_HEADER_SIZE，说明 header 过大，返回 REQUEST_ERROR；没超过则返回 REQUEST_INCOMPLETE。
    3. header 已完整时继续检查 header 总长度是否超过 MAX_HEADER_SIZE。
    4. 解析 request line，要求正好 method、uri、HTTP/1.1 三段。
    5. 把 server 写入 req.config，让 body limit、Response、CGI 都能查当前 server 配置。
    6. 解析 headers，拒绝非法 key、重复 Host、重复 Content-Length。
    7. 检查 Host 必填且非空。
    8. Transfer-Encoding 只支持严格的 chunked；非 chunked 的 TE 直接 REQUEST_ERROR。
    9. Content-Length 和 Transfer-Encoding 同时出现直接 REQUEST_ERROR。
    10. Content-Length 格式错返回 REQUEST_ERROR，数字合法但超过 effective body limit 返回 ERROR_MAX_BODY_LENGTH。
    11. chunked body 交给 parse_chunked_buffer；普通 body 等待 Content-Length 指定长度完整后截取。
修改说明：这是严格复核后的防守版 parser，修复了旧版把非法 Content-Length 当 413、非 chunked TE 被误当无 body、chunked trailer 未完整消费、request line/header key 过松、缺 Host 未报错、header 无限增长等问题。
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
	if (header_end == std::string::npos) {
		if (buffer.size() > MAX_HEADER_SIZE)
			return REQUEST_ERROR;
		return REQUEST_INCOMPLETE;
	}
	if (header_end > MAX_HEADER_SIZE)
		return REQUEST_ERROR;

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
	if (!has_required_host(req))
		return REQUEST_ERROR;

	bool has_te = false;
	bool is_chunked = false;
	if (is_chunked_transfer_encoding(req, has_te, is_chunked) != REQUEST_OK)
		return REQUEST_ERROR;
	if (has_te && req.headers.count("content-length"))
		return REQUEST_ERROR;

	size_t content_length = 0;
	unsigned long body_limit = getEffectiveBodyLimit(req.config, req.uri);
	if (!is_chunked && req.headers.count("content-length")) {
		int len_status = parse_content_length(req.headers["content-length"], body_limit, content_length);
		if (len_status != REQUEST_OK)
			return len_status;
	}
	if (is_chunked)
		return parse_chunked_buffer(buffer, body_start, req, consumed);
	if (buffer.length() < body_start + content_length)
		return REQUEST_INCOMPLETE;
	req.body = buffer.substr(body_start, content_length);
	consumed = body_start + content_length;
	return REQUEST_OK;
}
