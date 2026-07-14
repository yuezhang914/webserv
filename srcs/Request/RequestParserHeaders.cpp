/*
文件：srcs/Request/RequestParserHeaders.cpp
用途：保存 OWS、Host authority 与 headers 严格解析的原有实现。
拆分说明：函数实现从原 RequestParser.cpp 原样移动，不增加 wrapper 或中间 parser。
*/
#include "RequestParser.hpp"

/*
函数：trim_ows
用途：去掉 header value 或 transfer-coding 两端的 OWS 空白，也就是空格和 tab。
参数来源：parse_headers 处理冒号后的 value；parse_transfer_encoding 处理逗号切出来的编码片段。
实现逻辑：
    1. 找第一个不是空格/tab 的位置。
    2. 找最后一个不是空格/tab 的位置。
    3. 如果全是空白，返回空字符串；否则 substr 返回中间部分。
*/
std::string RequestParser::trim_ows(const std::string& value) {
	size_t first = value.find_first_not_of(" \t");
	if (first == std::string::npos)
		return "";
	size_t last = value.find_last_not_of(" \t");
	return value.substr(first, last - first + 1);
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
bool RequestParser::is_valid_decimal_port(const std::string& port) {
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
说明：这里只校验 Host authority 的语法边界，不在 RequestParser 中做配置选择。
*/
bool RequestParser::is_valid_reg_name(const std::string& host) {
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
				|| hex_value(host[i + 1]) < 0
				|| hex_value(host[i + 2]) < 0)
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
bool RequestParser::is_valid_ip_literal(const std::string& literal) {
	if (literal.empty() || literal.find(':') == std::string::npos)
		return false;
	size_t i = 0;
	while (i < literal.size()) {
		if (hex_value(literal[i]) < 0 && literal[i] != ':' && literal[i] != '.')
			return false;
		++i;
	}
	return true;
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
bool RequestParser::has_invalid_header_value_char(const std::string& value) {
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
bool RequestParser::is_valid_host_value(const std::string& value) {
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
用途：解析请求头，把每个 Header 行严格校验后存入 req._headers。
参数来源：RequestParser::parseBuffer 把 header 部分放进 istringstream，第一行 request line 已读掉，剩余行交给本函数。
返回值：所有 header 合法时返回 REQUEST_OK；任意 header 格式、key、value 或关键 header 重复时返回 REQUEST_ERROR。
实现逻辑：
    1. RequestParser::parseBuffer 已经验证整个 header 区域只使用 CRLF；这里逐行 getline，并去掉行尾 '\\r'。
    2. 每行必须包含冒号；冒号前是 key，冒号后是原始 value。
    3. key 必须是合法 HTTP token，不能为空，不能包含空格、tab、控制字符或冒号。
    4. 原始 value 不能含除 HTAB 外的控制字符。
    5. value 去掉两端空格和 tab，key 转成小写。
    6. Host、Content-Length、Transfer-Encoding 都不允许重复，大小写不同也视为重复。
    7. Host value 必须非空，并且不能含空格、tab或控制字符。
    8. 通过检查后才写入 req._headers。
    9. 所有行处理结束后，确认 HTTP/1.1 必需的 Host 已经存在。
例子："Content-Length: 3" 会保存为 headers["content-length"] = "3"。
*/
int RequestParser::parse_headers(std::istringstream& iss, Request& req) {
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
		std::string lower_key = Request::toLowerAscii(key);
		if ((lower_key == "content-length" || lower_key == "host"
				|| lower_key == "transfer-encoding")
			&& req._headers.find(lower_key) != req._headers.end())
			return REQUEST_ERROR;
		std::string value = trim_ows(raw_value);
		if (lower_key == "host" && !is_valid_host_value(value))
			return REQUEST_ERROR;
		req._headers[lower_key] = value;
	}
	if (req._headers.find("host") == req._headers.end())
		return REQUEST_ERROR;
	return REQUEST_OK;
}

