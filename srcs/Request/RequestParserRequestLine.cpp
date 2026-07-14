/*
文件：srcs/Request/RequestParserRequestLine.cpp
用途：保存 HTTP token、严格 CRLF、HTTP version 与 request-line 的原有实现。
拆分说明：函数实现从原 RequestParser.cpp 原样移动，不增加新的调用层级。
*/
#include "RequestParser.hpp"

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
bool RequestParser::is_token_char(char c) {
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
bool RequestParser::is_valid_token(const std::string& token) {
	if (token.empty())
		return false;
	for (size_t i = 0; i < token.size(); ++i) {
		if (!is_token_char(token[i]))
			return false;
	}
	return true;
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
bool RequestParser::has_invalid_line_endings(const std::string& text, size_t start,
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
函数：is_valid_http_version_syntax
用途：区分“HTTP version 字符串本身损坏”和“语法正确但版本不受支持”。
当前接受的语法形状严格为 HTTP/D.D，其中 D 是十进制数字。
例子：HTTP/1.0、HTTP/2.0 语法正确；HTTX/1.1、HTTP/1.x、HTTP/11 属于格式错误。
*/
bool RequestParser::is_valid_http_version_syntax(
        const std::string& version) {
    return version.size() == 8
        && version.compare(0, 5, "HTTP/") == 0
        && version[5] >= '0' && version[5] <= '9'
        && version[6] == '.'
        && version[7] >= '0' && version[7] <= '9';
}


/*
函数：parse_request_line
用途：严格解析 HTTP request-line，并填充 Request 的 method、原始 URI、normalized path、query 和 version。
返回值：
    - REQUEST_OK：格式和 URI 合法，version 为 HTTP/1.1。
    - REQUEST_VERSION_NOT_SUPPORTED：version 形状合法但不是 HTTP/1.1。
    - REQUEST_ERROR：空字段、Tab、多余空格、method token、version 语法或 URI 非法。
实现逻辑：
    1. request-line 只能由两个普通 SP 分隔，不能使用 Tab 或多余空格。
    2. method 必须是合法 token。
    3. version 先检查 HTTP/D.D 语法，再区分不支持版本。
    4. 原始 request-target 保存在 _uri；_path 和 _query 由 split_and_normalize_uri() 生成。
*/
int RequestParser::parse_request_line(
        const std::string& request_line, Request& req) {
    if (request_line.empty()
        || request_line.find('\t') != std::string::npos)
        return REQUEST_ERROR;

    size_t first_space = request_line.find(' ');
    if (first_space == std::string::npos || first_space == 0)
        return REQUEST_ERROR;
    size_t second_space = request_line.find(' ', first_space + 1);
    if (second_space == std::string::npos
        || second_space == first_space + 1
        || second_space + 1 >= request_line.size())
        return REQUEST_ERROR;
    if (request_line.find(' ', second_space + 1) != std::string::npos)
        return REQUEST_ERROR;

    req._method = request_line.substr(0, first_space);
    req._uri = request_line.substr(first_space + 1,
        second_space - first_space - 1);
    req._version = request_line.substr(second_space + 1);

    if (!is_valid_token(req._method))
        return REQUEST_ERROR;
    if (!is_valid_http_version_syntax(req._version))
        return REQUEST_ERROR;
    if (req._version != "HTTP/1.1")
        return REQUEST_VERSION_NOT_SUPPORTED;
    return split_and_normalize_uri(req._uri, req._path, req._query);
}

