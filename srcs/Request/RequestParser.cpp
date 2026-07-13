/*
文件：srcs/Request/RequestParser.cpp
HTTP request parser 实现。这个文件只负责从 ServerManager 已经读入的 raw buffer 中解析请求行、headers、Content-Length/chunked body，不负责网络 recv，也不负责 response 生成。
本版强化点：严格区分 400 与 413；request-line 只接受两个 SP；header/trailer 必须使用 CRLF；严格校验 Host authority、关键 header 重复、Content-Length、Transfer-Encoding、header/chunk-size/trailer 大小；所有长度运算先防溢出，再读取或拼接 body。
*/
#include "RequestParser.hpp"
#include "ConfigRouteUtils.hpp"

static const size_t MAX_HEADER_SIZE = 8192;
static const size_t MAX_CHUNK_SIZE_LINE = 1024;
static const size_t MAX_TRAILER_SIZE = 8192;

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
函数：has_invalid_line_endings
用途：检查指定字节区间内的 HTTP 行结束是否严格使用 CRLF。
参数来源：
    - text：原始 request buffer。
    - start/end：需要检查的半开区间 [start, end)。
    - allow_trailing_cr：数据尚未收完整时，是否允许区间最后一个字节暂时是 '\r'。
返回值：出现裸 '\n'、后面不是 '\n' 的 '\r'，或完整区间以孤立 '\r' 结束时返回 true。
实现逻辑：
    1. '\n' 前面必须紧挨 '\r'。
    2. '\r' 后面必须紧挨 '\n'。
    3. 只有增量读取且 '\r' 恰好位于当前 buffer 末尾时，才暂时视为未完成而不是格式错误。
为什么需要：拒绝 LF-only、裸 CR 和混合换行，避免不同 HTTP parser 对消息边界产生不同解释。
*/
static bool has_invalid_line_endings(const std::string& text, size_t start,
		size_t end, bool allow_trailing_cr) {
	size_t i = start;
	while (i < end) {
		if (text[i] == '\n') {
			if (i == start || text[i - 1] != '\r')
				return true;
		}
		else if (text[i] == '\r') {
			if (i + 1 >= end)
				return !allow_trailing_cr;
			if (text[i + 1] != '\n')
				return true;
			++i;
		}
		++i;
	}
	return false;
}

/*
函数：is_hex_digit
用途：判断字符是否为十六进制数字。
参数来源：Host 的 percent-encoding、IP literal 等辅助校验。
返回值：0-9、a-f、A-F 返回 true。
*/
static bool is_hex_digit(char c) {
	return (c >= '0' && c <= '9')
		|| (c >= 'a' && c <= 'f')
		|| (c >= 'A' && c <= 'F');
}

/*
函数：is_valid_decimal_port
用途：严格检查 Host header 冒号后的端口。
参数来源：is_valid_host_value() 从 localhost:8080 或 [::1]:8080 中切出的 port。
返回值：端口由纯数字组成并且范围为 1 到 65535 时返回 true。
实现逻辑：
    1. 空端口非法。
    2. 逐字符检查只能是数字，拒绝 +80、-1、80x。
    3. 手动累加并在超过 65535 时立即失败，避免转换溢出。
*/
static bool is_valid_decimal_port(const std::string& port) {
	if (port.empty())
		return false;
	unsigned long value = 0;
	size_t i = 0;
	while (i < port.size()) {
		if (port[i] < '0' || port[i] > '9')
			return false;
		value = value * 10 + static_cast<unsigned long>(port[i] - '0');
		if (value > 65535)
			return false;
		++i;
	}
	return value > 0;
}

/*
函数：is_valid_reg_name
用途：检查非方括号形式的 Host 主机部分。
参数来源：is_valid_host_value() 从 Host value 中去掉可选端口后传入。
返回值：host 非空、含至少一个字母或数字，并且只包含 URI reg-name 的安全字符时返回 true。
实现逻辑：
    1. 允许字母、数字、- . _ ~ 和标准 sub-delims。
    2. % 必须后跟两个十六进制字符。
    3. 拒绝 /、?、#、@、反斜杠、冒号和控制字符。
说明：这里只校验 Host authority 的语法边界；实际选择哪个 server_name 仍由 ServerManager 完成。
*/
static bool is_valid_reg_name(const std::string& host) {
	if (host.empty())
		return false;
	bool has_alnum = false;
	size_t i = 0;
	while (i < host.size()) {
		unsigned char c = static_cast<unsigned char>(host[i]);
		if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
			|| (c >= '0' && c <= '9')) {
			has_alnum = true;
			++i;
			continue;
		}
		if (host[i] == '-' || host[i] == '.' || host[i] == '_'
			|| host[i] == '~' || host[i] == '!' || host[i] == '$'
			|| host[i] == '&' || host[i] == '\'' || host[i] == '('
			|| host[i] == ')' || host[i] == '*' || host[i] == '+'
			|| host[i] == ',' || host[i] == ';' || host[i] == '=') {
			++i;
			continue;
		}
		if (host[i] == '%') {
			if (i + 2 >= host.size()
				|| !is_hex_digit(host[i + 1])
				|| !is_hex_digit(host[i + 2]))
				return false;
			i += 3;
			continue;
		}
		return false;
	}
	return has_alnum;
}

/*
函数：is_valid_ip_literal
用途：检查 Host 中方括号包围的 IP literal 基本格式，例如 [::1]。
参数来源：is_valid_host_value() 取得 '[' 与 ']' 中间的内容后传入。
返回值：内容非空、至少含一个冒号，并且只含十六进制字符、冒号或点号时返回 true。
说明：本项目不完整解析所有 IPv6/IPvFuture 细节，但会拒绝未闭合括号、空 literal 和明显非法字符。
*/
static bool is_valid_ip_literal(const std::string& literal) {
	if (literal.empty() || literal.find(':') == std::string::npos)
		return false;
	size_t i = 0;
	while (i < literal.size()) {
		if (!is_hex_digit(literal[i]) && literal[i] != ':' && literal[i] != '.')
			return false;
		++i;
	}
	return true;
}

/*
函数：parse_request_line
用途：严格解析 HTTP 请求第一行，例如 GET /ping.html HTTP/1.1。
参数来源：parseRequestBuffer 从 header 部分第一行读出 request_line；req 是等待填充的 Request 对象。
返回值：请求行格式、method、version 和 URI 都合法时返回 REQUEST_OK；否则返回 REQUEST_ERROR。
实现逻辑：
    1. HTTP request-line 的三个字段只能由两个普通空格 SP 分隔，tab 不能代替 SP。
    2. 查找第一个和第二个空格，并确认 method、URI、version 三段都不为空。
    3. 第二个空格后不能再出现其他空格，拒绝多余字段、连续空格和行尾空格。
    4. method 必须是合法 HTTP token。
    5. version 必须完整等于 HTTP/1.1。
    6. 把三段写进 req，再调用 sanitizeRequestUri 检查 URI 安全。
为什么严格检查：istringstream 会自动跳过 tab 和多个空格，可能把不符合 request-line 格式的文本误当成合法请求。
*/
static int parse_request_line(
    const std::string& request_line,
    Request& req)
{
    if (request_line.empty())
        return REQUEST_ERROR;

    // 请求行只能用普通空格分隔，不能出现 Tab。
    if (request_line.find('\t') != std::string::npos)
        return REQUEST_ERROR;

    // 找 method 后面的第一个普通空格。
    size_t first_space = request_line.find(' ');
    if (first_space == std::string::npos || first_space == 0)
        return REQUEST_ERROR;

    // 找 URI 后面的第二个普通空格。
    size_t second_space = request_line.find(' ', first_space + 1);
    if (second_space == std::string::npos
        || second_space == first_space + 1)
        return REQUEST_ERROR;

    // 第二个空格后必须有 HTTP version。
    if (second_space + 1 >= request_line.size())
        return REQUEST_ERROR;

    // 请求行只能有两个普通空格。
    if (request_line.find(' ', second_space + 1)
        != std::string::npos)
        return REQUEST_ERROR;

    req.method = request_line.substr(0, first_space);
    req.uri = request_line.substr(
        first_space + 1,
        second_space - first_space - 1);
    req.version = request_line.substr(second_space + 1);

    if (!is_valid_token(req.method))
        return REQUEST_ERROR;

    if (req.version != "HTTP/1.1")
        return REQUEST_ERROR;

    return sanitizeRequestUri(req);
}

/*
函数：has_invalid_header_value_char
用途：检查 header value 中是否含有非法控制字符。
参数来源：parse_headers 从冒号后取得、尚未 trim 和保存的原始 value。
返回值：发现非法控制字符时返回 true，否则返回 false。
实现逻辑：
    1. 逐个读取 value 中的字节。
    2. 允许普通可见字符、空格和 HTAB。
    3. 拒绝除 HTAB 外的 ASCII 控制字符以及 DEL（127）。
为什么需要：控制字符可能破坏 header 边界或让后续模块对同一个 header 产生不同理解。
*/
static bool has_invalid_header_value_char(const std::string& value) {
	size_t i = 0;
	while (i < value.size()) {
		unsigned char c = static_cast<unsigned char>(value[i]);
		if ((c < 32 && c != '\t') || c == 127)
			return true;
		++i;
	}
	return false;
}

/*
函数：is_valid_host_value
用途：严格检查 HTTP/1.1 Host header 是否是本项目可安全处理的 authority。
参数来源：parse_headers() 对 Host value 做 trim_ows() 后传入。
返回值：合法的 reg-name/IPv4/方括号 IP literal，以及可选的 1-65535 端口返回 true。
实现逻辑：
    1. Host 不能为空，也不能含空格、tab、控制字符或 DEL。
    2. 以 '[' 开头时，要求存在匹配的 ']'，括号内通过 is_valid_ip_literal()。
    3. ']' 后只能为空，或是 ':' 加合法端口。
    4. 非方括号形式最多只能有一个冒号；冒号前用 is_valid_reg_name() 校验，后面校验端口。
    5. 因此会拒绝 bad host、user@example、example/path、::1、host:abc、host:0 等歧义形式。
*/
static bool is_valid_host_value(const std::string& value) {
	if (value.empty())
		return false;
	size_t i = 0;
	while (i < value.size()) {
		unsigned char c = static_cast<unsigned char>(value[i]);
		if (c == ' ' || c == '\t' || c < 32 || c == 127)
			return false;
		++i;
	}
	if (value[0] == '[') {
		size_t close = value.find(']');
		if (close == std::string::npos)
			return false;
		if (!is_valid_ip_literal(value.substr(1, close - 1)))
			return false;
		if (close + 1 == value.size())
			return true;
		if (value[close + 1] != ':')
			return false;
		return is_valid_decimal_port(value.substr(close + 2));
	}
	size_t colon = value.find(':');
	if (colon != std::string::npos
		&& value.find(':', colon + 1) != std::string::npos)
		return false;
	std::string host = value.substr(0, colon);
	if (!is_valid_reg_name(host))
		return false;
	if (colon == std::string::npos)
		return true;
	return is_valid_decimal_port(value.substr(colon + 1));
}

/*
函数：parse_headers
用途：解析请求头，把每个 Header 行严格校验后存入 req.headers。
参数来源：parseRequestBuffer 把 header 部分放进 istringstream，第一行 request line 已读掉，剩余行交给本函数。
返回值：所有 header 合法时返回 REQUEST_OK；任意 header 格式、key、value 或关键 header 重复时返回 REQUEST_ERROR。
实现逻辑：
    1. parseRequestBuffer 已经验证整个 header 区域只使用 CRLF；这里逐行 getline，并去掉行尾 '\\r'。
    2. 每行必须包含冒号；冒号前是 key，冒号后是原始 value。
    3. key 必须是合法 HTTP token，不能为空，不能包含空格、tab、控制字符或冒号。
    4. 原始 value 不能含除 HTAB 外的控制字符。
    5. value 去掉两端空格和 tab，key 转成小写。
    6. Host、Content-Length、Transfer-Encoding 都不允许重复，大小写不同也视为重复。
    7. Host value 必须非空，并且不能含空格、tab或控制字符。
    8. 通过检查后才写入 req.headers。
例子："Content-Length: 3" 会保存为 headers["content-length"] = "3"。
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
		std::string raw_value = line.substr(colon_pos + 1);
		if (has_invalid_header_value_char(raw_value))
			return REQUEST_ERROR;
		std::string lower_key = to_lower(key);
		if ((lower_key == "content-length" || lower_key == "host"
				|| lower_key == "transfer-encoding")
			&& req.headers.find(lower_key) != req.headers.end())
			return REQUEST_ERROR;
		std::string value = trim_ows(raw_value);
		if (lower_key == "host" && !is_valid_host_value(value))
			return REQUEST_ERROR;
		req.headers[lower_key] = value;
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
输出：成功时把长度写入 content_length；格式错误返回 REQUEST_ERROR；数字格式合法但超过 body_limit 或整数范围时返回 ERROR_MAX_BODY_LENGTH。
实现逻辑：
    1. 先完整检查字符串：必须非空，并且每个字符都必须是十进制数字。
    2. 第一遍格式检查结束前不比较 body_limit，保证 12x、1.0、+5 等始终属于 400 格式错误，而不是被误判成 413。
    3. 第二遍手动累加数字，先检查 unsigned long 溢出。
    4. 数字合法但计算结果超过 body_limit 时返回 ERROR_MAX_BODY_LENGTH。
*/
static int parse_content_length(const std::string& value, unsigned long body_limit, size_t& content_length) {
	if (value.empty())
		return REQUEST_ERROR;
	size_t i = 0;
	while (i < value.size()) {
		if (value[i] < '0' || value[i] > '9')
			return REQUEST_ERROR;
		++i;
	}
	unsigned long result = 0;
	unsigned long max_ulong = static_cast<unsigned long>(-1);
	i = 0;
	while (i < value.size()) {
		unsigned long digit = static_cast<unsigned long>(value[i] - '0');
		if (result > (max_ulong - digit) / 10)
			return ERROR_MAX_BODY_LENGTH;
		result = result * 10 + digit;
		if (result > body_limit)
			return ERROR_MAX_BODY_LENGTH;
		++i;
	}
	const size_t max_size = static_cast<size_t>(-1);
	if (result > static_cast<unsigned long>(max_size))
		return ERROR_MAX_BODY_LENGTH;
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
用途：解析一行 chunk-size，并在返回 size 前完成格式、整数范围和累计 body 上限检查。
参数来源：
    - line：chunked body 的 size 行，不含结尾 CRLF，例如 "A" 或 "A;name=value"。
    - current_body_size：已经解码并保存的 body 字节数。
    - body_limit：当前 server/location 的 effective max_body_size。
    - size：输出本次 chunk 的字节数。
返回值：成功返回 REQUEST_OK；格式非法返回 REQUEST_ERROR；整数或累计 body 超限返回 ERROR_MAX_BODY_LENGTH。
实现逻辑：
    1. 分号前必须是非空十六进制数。
    2. 若存在 extension，分号后不能为空且只能含可见 ASCII，拒绝空格和控制字符。
    3. 手动按十六进制累加，先防 unsigned long 溢出。
    4. result 必须能装入 size_t，并为后续 chunk data + CRLF 预留 2 字节。
    5. 使用 result > body_limit - current 的减法形式检查累计上限，避免 current + result 自身先溢出。
*/
static int read_chunk_size_for_buffer(const std::string& line,
		size_t current_body_size, unsigned long body_limit, size_t& size) {
	size_t semicolon = line.find(';');
	std::string number = line.substr(0, semicolon);
	if (number.empty())
		return REQUEST_ERROR;
	if (semicolon != std::string::npos) {
		std::string extension = line.substr(semicolon + 1);
		if (extension.empty())
			return REQUEST_ERROR;
		size_t ext_index = 0;
		while (ext_index < extension.size()) {
			unsigned char c = static_cast<unsigned char>(extension[ext_index]);
			if (c < 33 || c > 126)
				return REQUEST_ERROR;
			++ext_index;
		}
	}
	unsigned long result = 0;
	const unsigned long max_ulong = static_cast<unsigned long>(-1);
	size_t i = 0;
	while (i < number.size()) {
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
		++i;
	}
	const size_t max_size = static_cast<size_t>(-1);
	if (result > static_cast<unsigned long>(max_size)
		|| result > static_cast<unsigned long>(max_size - 2))
		return ERROR_MAX_BODY_LENGTH;
	if (current_body_size > static_cast<size_t>(body_limit))
		return ERROR_MAX_BODY_LENGTH;
	unsigned long current = static_cast<unsigned long>(current_body_size);
	if (current > body_limit || result > body_limit - current)
		return ERROR_MAX_BODY_LENGTH;
	size = static_cast<size_t>(result);
	return REQUEST_OK;
}

/*
函数：is_forbidden_trailer_name
用途：拒绝不能在 chunked trailer 中重新定义的消息边界或路由字段。
参数来源：find_chunked_trailer_end() 把 trailer key 转成小写后传入。
返回值：Host、Content-Length 或 Transfer-Encoding 返回 true。
为什么需要：这些字段若出现在 trailer，前面的 parser 已经依据旧值完成路由或 framing，继续接受会制造歧义。
*/
static bool is_forbidden_trailer_name(const std::string& lower_key) {
	return lower_key == "host"
		|| lower_key == "content-length"
		|| lower_key == "transfer-encoding";
}

/*
函数：validate_trailer_line
用途：严格检查一条 chunked trailer header。
参数来源：find_chunked_trailer_end() 按 CRLF 切出的单行文本。
返回值：key/value 合法且不属于禁止字段时返回 REQUEST_OK，否则返回 REQUEST_ERROR。
实现逻辑：
    1. 必须含冒号。
    2. 冒号前的 key 必须是合法 HTTP token。
    3. value 不能含除 HTAB 外的控制字符。
    4. Host、Content-Length、Transfer-Encoding 不允许在 trailer 中出现。
*/
static int validate_trailer_line(const std::string& line) {
	size_t colon_pos = line.find(':');
	if (colon_pos == std::string::npos)
		return REQUEST_ERROR;
	std::string key = line.substr(0, colon_pos);
	if (!is_valid_token(key))
		return REQUEST_ERROR;
	std::string raw_value = line.substr(colon_pos + 1);
	if (has_invalid_header_value_char(raw_value))
		return REQUEST_ERROR;
	if (is_forbidden_trailer_name(to_lower(key)))
		return REQUEST_ERROR;
	return REQUEST_OK;
}

/*
函数：find_chunked_trailer_end
用途：在 0-size chunk 后找到 chunked message 的真正结束位置，并严格消费可选 trailer。
参数来源：
    - buffer：完整 client buffer。
    - pos：紧跟在 "0\r\n" 后面的下标。
    - consumed：成功时输出当前完整 request 的结束位置。
返回值：完整合法返回 REQUEST_OK；尚未收全返回 REQUEST_INCOMPLETE；格式或大小非法返回 REQUEST_ERROR。
实现逻辑：
    1. pos 后立即是 CRLF，表示没有 trailer，直接结束。
    2. trailer 最多 MAX_TRAILER_SIZE 字节，超过后不再无限等待。
    3. trailer 区域必须严格使用 CRLF，拒绝裸 LF、裸 CR 和混合换行。
    4. 找到 CRLFCRLF 后，逐行调用 validate_trailer_line()。
    5. 成功时 consumed 包含整个 trailer 终止空行。
*/
static int find_chunked_trailer_end(const std::string& buffer,
		size_t pos, size_t& consumed) {
	if (pos > buffer.size())
		return REQUEST_ERROR;
	if (buffer.size() - pos < 2)
		return REQUEST_INCOMPLETE;
	if (buffer.compare(pos, 2, "\r\n") == 0) {
		consumed = pos + 2;
		return REQUEST_OK;
	}
	size_t trailer_end = buffer.find("\r\n\r\n", pos);
	if (trailer_end == std::string::npos) {
		if (has_invalid_line_endings(buffer, pos, buffer.size(), true))
			return REQUEST_ERROR;
		if (buffer.size() - pos > MAX_TRAILER_SIZE)
			return REQUEST_ERROR;
		return REQUEST_INCOMPLETE;
	}
	size_t trailer_size = trailer_end + 4 - pos;
	if (trailer_size > MAX_TRAILER_SIZE)
		return REQUEST_ERROR;
	if (has_invalid_line_endings(buffer, pos, trailer_end + 4, false))
		return REQUEST_ERROR;
	size_t line_start = pos;
	while (line_start < trailer_end) {
		size_t line_end = buffer.find("\r\n", line_start);
		if (line_end == std::string::npos || line_end > trailer_end)
			return REQUEST_ERROR;
		if (validate_trailer_line(
				buffer.substr(line_start, line_end - line_start))
			!= REQUEST_OK)
			return REQUEST_ERROR;
		line_start = line_end + 2;
	}
	consumed = trailer_end + 4;
	return REQUEST_OK;
}

/*
函数：parse_chunked_buffer
用途：在不修改原始 buffer 的前提下，严格解析并还原 Transfer-Encoding: chunked body。
参数来源：
    - buffer：ServerManager 为当前 client 累积的原始字节。
    - body_start：header 结束空行后的第一个 body 字节。
    - req：输出解析后的 body。
    - consumed：成功时输出整个 request 占用的字节数。
返回值：成功、未完成、格式错误或 body 超限状态。
实现逻辑：
    1. 每个 chunk-size 行必须在 MAX_CHUNK_SIZE_LINE 内结束，否则拒绝。
    2. size 行通过 read_chunk_size_for_buffer() 做十六进制、extension 和溢出检查。
    3. chunk data 长度判断使用减法形式，避免 pos + chunk_size + 2 溢出。
    4. 每段 data 后必须紧跟 CRLF。
    5. 0-size chunk 后由 find_chunked_trailer_end() 严格解析 trailer。
*/
static int parse_chunked_buffer(const std::string& buffer,
		size_t body_start, Request& req, size_t& consumed) {
	size_t pos = body_start;
	std::string body;
	unsigned long body_limit = getEffectiveBodyLimit(req.config, req.uri);
	while (true) {
		if (pos > buffer.size())
			return REQUEST_ERROR;
		size_t line_end = buffer.find("\r\n", pos);
		if (line_end == std::string::npos) {
			if (has_invalid_line_endings(buffer, pos, buffer.size(), true))
				return REQUEST_ERROR;
			if (buffer.size() - pos > MAX_CHUNK_SIZE_LINE)
				return REQUEST_ERROR;
			return REQUEST_INCOMPLETE;
		}
		if (line_end - pos > MAX_CHUNK_SIZE_LINE)
			return REQUEST_ERROR;
		size_t chunk_size = 0;
		int size_status = read_chunk_size_for_buffer(
			buffer.substr(pos, line_end - pos),
			body.size(), body_limit, chunk_size);
		if (size_status != REQUEST_OK)
			return size_status;
		pos = line_end + 2;
		if (chunk_size == 0) {
			int trailer_status = find_chunked_trailer_end(
				buffer, pos, consumed);
			if (trailer_status != REQUEST_OK)
				return trailer_status;
			req.body = body;
			return REQUEST_OK;
		}
		if (pos > buffer.size())
			return REQUEST_ERROR;
		size_t available = buffer.size() - pos;
		if (available < chunk_size)
			return REQUEST_INCOMPLETE;
		if (available - chunk_size < 2)
			return REQUEST_INCOMPLETE;
		if (buffer.compare(pos + chunk_size, 2, "\r\n") != 0)
			return REQUEST_ERROR;
		body.append(buffer, pos, chunk_size);
		pos += chunk_size + 2;
	}
}

/*
函数：parseRequestBuffer
用途：把 ServerManager 已经读取并累积的 raw buffer 解析成一个完整 Request。
输入来源：
    - buffer：当前 client 的接收缓冲区。
    - req：输出 Request；每次调用开始都会清空旧状态。
    - server：当前连接初步关联的 ServerConfig。
    - consumed：成功时输出本次 request 占用的字节数，供调用方 erase。
返回值：
    - REQUEST_OK：一个请求完整且合法。
    - REQUEST_INCOMPLETE：当前字节还不足，继续等待 POLLIN。
    - REQUEST_ERROR：请求行、header、Host、framing、chunked 或 URI 格式非法。
    - ERROR_MAX_BODY_LENGTH：合法长度超过 effective max_body_size 或无法安全表示。
实现逻辑：
    1. 重置 req 和 consumed，本函数不 recv，也不修改 buffer。
    2. 在 header 尚未结束时，提前拒绝裸 LF/裸 CR 和超过 MAX_HEADER_SIZE 的输入。
    3. 找到 CRLFCRLF 后，再验证完整 header 区域严格使用 CRLF，并把终止空行计入大小上限。
    4. 解析 request-line、headers 和必填 Host。
    5. Transfer-Encoding 只允许单个 chunked；与 Content-Length 同时出现时拒绝。
    6. 普通 body 严格解析 Content-Length，并用 content_length > available 的减法形式等待数据，避免加法溢出。
    7. chunked body 交给 parse_chunked_buffer()。
*/
int parseRequestBuffer(const std::string& buffer, Request& req,
		const ServerConfig* server, size_t& consumed) {
	consumed = 0;
	req.method.clear();
	req.uri.clear();
	req.version.clear();
	req.headers.clear();
	req.body.clear();
	req.config = server;
	req.closeConnection = true;

	size_t header_end = buffer.find("\r\n\r\n");
	if (header_end == std::string::npos) {
		if (has_invalid_line_endings(buffer, 0, buffer.size(), true))
			return REQUEST_ERROR;
		if (buffer.size() > MAX_HEADER_SIZE)
			return REQUEST_ERROR;
		return REQUEST_INCOMPLETE;
	}
	size_t header_size = header_end + 4;
	if (header_size > MAX_HEADER_SIZE)
		return REQUEST_ERROR;
	if (has_invalid_line_endings(buffer, 0, header_size, false))
		return REQUEST_ERROR;

	size_t body_start = header_size;
	std::istringstream iss(buffer.substr(0, header_end));
	std::string request_line;
	if (!std::getline(iss, request_line))
		return REQUEST_ERROR;
	if (!request_line.empty()
		&& request_line[request_line.size() - 1] == '\r')
		request_line.erase(request_line.size() - 1);
	if (parse_request_line(request_line, req) != REQUEST_OK)
		return REQUEST_ERROR;
	if (parse_headers(iss, req) != REQUEST_OK)
		return REQUEST_ERROR;
	if (!has_required_host(req))
		return REQUEST_ERROR;

	bool has_te = false;
	bool is_chunked = false;
	if (is_chunked_transfer_encoding(req, has_te, is_chunked)
		!= REQUEST_OK)
		return REQUEST_ERROR;
	if (has_te && req.headers.count("content-length"))
		return REQUEST_ERROR;

	size_t content_length = 0;
	unsigned long body_limit = getEffectiveBodyLimit(req.config, req.uri);
	if (!is_chunked && req.headers.count("content-length")) {
		int len_status = parse_content_length(
			req.headers["content-length"], body_limit, content_length);
		if (len_status != REQUEST_OK)
			return len_status;
	}
	if (is_chunked)
		return parse_chunked_buffer(buffer, body_start, req, consumed);

	size_t available = buffer.size() - body_start;
	if (content_length > available)
		return REQUEST_INCOMPLETE;
	req.body = buffer.substr(body_start, content_length);
	consumed = body_start + content_length;
	return REQUEST_OK;
}