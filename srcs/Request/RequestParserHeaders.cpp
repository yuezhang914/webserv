/*
文件：srcs/Request/RequestParserHeaders.cpp
用途：实现 OWS、Host authority 与 headers 的严格解析。
修改说明：保持公开接口不变；补强 IPv6 literal 校验，并拒绝所有会被单值 map 覆盖的重复 header。
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
函数：is_valid_ipv4_address
用途：校验 IPv6 尾部可选的嵌入式 IPv4，例如 192.0.2.1。
参数来源：count_ipv6_side_groups() 发现最后一个 group 含点号时调用。
返回值：恰好四段十进制数、每段 0 到 255 且无多余前导零时返回 true。
实现逻辑：
    1. 每段必须非空且只含十进制数字。
    2. 多位数字不能以 0 开头，避免同一地址出现歧义写法。
    3. 手动累加并在超过 255 时立即拒绝。
*/
bool RequestParser::is_valid_ipv4_address(const std::string& address) {
	size_t start = 0;
	int part_count = 0;
	while (start <= address.size()) {
		size_t dot = address.find('.', start);
		size_t end = dot == std::string::npos ? address.size() : dot;
		if (end == start || end - start > 3)
			return false;
		if (end - start > 1 && address[start] == '0')
			return false;
		unsigned long value = 0;
		size_t i = start;
		while (i < end) {
			if (address[i] < '0' || address[i] > '9')
				return false;
			value = value * 10 + static_cast<unsigned long>(address[i] - '0');
			if (value > 255)
				return false;
			++i;
		}
		++part_count;
		if (dot == std::string::npos)
			break;
		start = dot + 1;
	}
	return part_count == 4;
}

/*
函数：count_ipv6_side_groups
用途：校验 IPv6 中位于可选 "::" 一侧、只含单冒号分隔的文本，并计算其占用的 16-bit group 数。
参数来源：is_valid_ip_literal() 把 "::" 左右两侧分别传入；allow_ipv4 只对整个地址最后一侧开放。
返回值：每个 h16 为 1 到 4 个十六进制字符，且可选尾部 IPv4 合法时返回 true。
输出：group_count 返回 h16 数量；嵌入式 IPv4 按两个 16-bit group 计算。
实现逻辑：
    1. 空侧合法并计为 0，表示 "::" 位于地址开头或结尾。
    2. side 内不允许空 group，因此前导、尾随或连续单冒号均拒绝。
    3. 含点号的 token 只能是整个 side 的最后一个 token，并且 allow_ipv4 必须为 true。
*/
bool RequestParser::count_ipv6_side_groups(const std::string& side,
		bool allow_ipv4, size_t& group_count) {
	group_count = 0;
	if (side.empty())
		return true;
	size_t start = 0;
	while (start <= side.size()) {
		size_t colon = side.find(':', start);
		size_t end = colon == std::string::npos ? side.size() : colon;
		if (end == start)
			return false;
		std::string group = side.substr(start, end - start);
		if (group.find('.') != std::string::npos) {
			if (!allow_ipv4 || colon != std::string::npos
				|| !is_valid_ipv4_address(group))
				return false;
			group_count += 2;
		}
		else {
			if (group.size() > 4)
				return false;
			size_t i = 0;
			while (i < group.size()) {
				if (hex_value(group[i]) < 0)
					return false;
				++i;
			}
			++group_count;
		}
		if (colon == std::string::npos)
			break;
		start = colon + 1;
	}
	return true;
}

/*
函数：is_valid_ip_literal
用途：完整校验 Host 中方括号包围的 IPv6 地址，例如 [::1] 或 [::ffff:192.0.2.1]。
参数来源：is_valid_host_value() 取得 '[' 与 ']' 中间的内容后传入。
返回值：地址满足 IPv6 group 数量、十六进制格式、单次 "::" 压缩和可选嵌入式 IPv4 规则时返回 true。
实现逻辑：
    1. 空字符串直接拒绝，"::" 最多出现一次；":::" 也因重叠的第二个 "::" 被拒绝。
    2. 没有 "::" 时，显式 group 数必须恰好为 8。
    3. 有 "::" 时分别校验左右两侧，显式 group 总数必须小于 8，保证压缩至少替代一个 group。
    4. IPv4 只能出现在整个地址末尾，并按两个 group 计算。
限制：不接受 IPvFuture 或 zone identifier；实现只使用 C++98 字符串操作，不引入 42 subject 之外的网络解析函数。
为什么修改：原实现只检查字符，像 "::::" 也会被误判为合法 IP literal。
*/
bool RequestParser::is_valid_ip_literal(const std::string& literal) {
	if (literal.empty())
		return false;
	size_t compressed = literal.find("::");
	if (compressed != std::string::npos
		&& literal.find("::", compressed + 1) != std::string::npos)
		return false;
	size_t left_groups = 0;
	size_t right_groups = 0;
	if (compressed == std::string::npos) {
		if (!count_ipv6_side_groups(literal, true, left_groups))
			return false;
		return left_groups == 8;
	}
	std::string left = literal.substr(0, compressed);
	std::string right = literal.substr(compressed + 2);
	if (!count_ipv6_side_groups(left, false, left_groups))
		return false;
	if (!count_ipv6_side_groups(right, true, right_groups))
		return false;
	return left_groups + right_groups < 8;
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
返回值：所有 header 合法时返回 REQUEST_OK；任意 header 格式、key、value 或重复 header 时返回 REQUEST_ERROR。
实现逻辑：
    1. RequestParser::parseBuffer 已经验证整个 header 区域只使用 CRLF；这里逐行 getline，并去掉行尾 '\\r'。
    2. 每行必须包含冒号；冒号前是 key，冒号后是原始 value。
    3. key 必须是合法 HTTP token，不能为空，不能包含空格、tab、控制字符或冒号。
    4. 原始 value 不能含除 HTAB 外的控制字符。
    5. value 去掉两端空格和 tab，key 转成小写。
    6. 任意 header name 都不允许重复，大小写不同也视为重复；避免 map 静默覆盖旧值。
    7. Host value 必须非空，并且不能含空格、tab或控制字符。
    8. 通过检查后才写入 req._headers。
    9. 所有行处理结束后，确认 HTTP/1.1 必需的 Host 已经存在。
说明：本项目使用 map<string, string> 保存单值 header，无法无损保留所有重复字段；统一拒绝比错误合并或覆盖更安全。
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
		if (req._headers.find(lower_key) != req._headers.end())
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

