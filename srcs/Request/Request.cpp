/*
文件：srcs/Request/Request.cpp
HTTP request 基础工具实现。 Request 的通用工具、URI 安全检查和调试输出；真正的请求读取与解析流程已拆到 RequestParser.cpp。
*/
#include "Request.hpp"
#include <cctype>

/*
函数：to_lower
用途：把字符串中的英文字母统一转换成小写。
参数来源：主要接收 HTTP header 名、Transfer-Encoding 值、URI 安全检查等字符串。
返回值：返回转换后的小写字符串，不修改原始参数。
实现逻辑：
    1. 复制输入字符串到 res，避免修改原字符串。
    2. 使用 while 逐个读取字符。
    3. 先把字符转换成 unsigned char，再交给 std::tolower，避免有符号 char 直接传入字符分类函数产生未定义行为。
    4. 把转换结果写回副本并返回。
为什么需要：HTTP header 名称大小写不敏感，把 key 统一成小写后，后面可以稳定查 headers["content-length"]。
*/
std::string to_lower(const std::string& str) {
	std::string res = str;
	size_t i = 0;
	while (i < res.size()) {
		unsigned char c = static_cast<unsigned char>(res[i]);
		res[i] = static_cast<char>(std::tolower(c));
		++i;
	}
	return res;
}

/*
函数：has_bad_uri_char
用途：检查 URI 中是否含有危险或非法字符。
实现逻辑：
    1. 遍历 uri 的每个字符。
    2. 如果字符 ASCII 小于 32，说明是控制字符，拒绝。
    3. 如果字符是 127，也拒绝。
    4. 如果字符是反斜杠 \，拒绝，避免 Windows 风格路径干扰路径安全。
    5. 全部字符安全则返回 false。
*/
static bool has_bad_uri_char(const std::string& uri) {
	size_t i = 0;
	while (i < uri.size()) {
		unsigned char c = static_cast<unsigned char>(uri[i]);
		if (c < 32 || c == 127 || c == '\\')
			return true;
		++i;
	}
	return false;
}


/*
函数：is_hex_digit
用途：判断一个字符是否是 URI percent-encoding 中允许的十六进制字符。
参数来源：has_invalid_percent_encoding() 检查 % 后面的两个字符时传入。
返回值：字符属于 0-9、a-f 或 A-F 时返回 true，否则返回 false。
*/
static bool is_hex_digit(char c) {
	return (c >= '0' && c <= '9')
		|| (c >= 'a' && c <= 'f')
		|| (c >= 'A' && c <= 'F');
}

/*
函数：has_invalid_percent_encoding
用途：检查 URI 中每一个 % 是否都严格跟着两个十六进制字符。
参数来源：sanitizeRequestUri() 传入完整 request-target。
返回值：发现孤立 %、长度不足或 %ZZ 这类非法编码时返回 true。
实现逻辑：
    1. 从左到右扫描 URI。
    2. 普通字符直接跳过。
    3. 遇到 % 时，确认后面至少还有两个字符。
    4. 两个字符必须都是十六进制数字；合法后一次跳过整组 %XX。
为什么需要：避免不同模块对畸形 percent-encoding 产生不同解释，也避免后续 URI 解码越界。
*/
static bool has_invalid_percent_encoding(const std::string& uri) {
	size_t i = 0;
	while (i < uri.size()) {
		if (uri[i] != '%') {
			++i;
			continue;
		}
		if (i + 2 >= uri.size())
			return true;
		if (!is_hex_digit(uri[i + 1]) || !is_hex_digit(uri[i + 2]))
			return true;
		i += 3;
	}
	return false;
}

/*
函数：sanitizeRequestUri
用途：检查 Request.uri 是否安全，防止非法路径、控制字符、fragment 和路径穿越。
参数来源：parseRequestBuffer 内部的 parse_request_line 已经把请求行中的 URI 写进 req.uri；URI 可能带 query，例如 /search?q=a。
实现逻辑：
    1. URI 不能为空，不能包含 HTTP fragment 标记 #，因为 fragment 不应该出现在 HTTP request target 中。
    2. 先按 ? 分离 path 和 query；路径安全检查只针对 path，避免 query 干扰 /.. 结尾判断。
    3. path 必须以 / 开头。
    4. 对完整 URI 调用 has_bad_uri_char，拒绝控制字符和反斜杠。
    5. 检查所有 % 是否严格使用 %XX 十六进制格式，拒绝孤立 % 和 %ZZ。
    6. 对 path 的小写版本检查 %00、%2e、%2f，防止编码后的空字符、点和斜杠绕过路径检查。
    7. 对 path 检查 /../、path == /..、结尾 /..、开头 ../ 等路径穿越形式。
    8. 全部通过返回 REQUEST_OK，否则返回 REQUEST_ERROR。
后续影响：如果这里失败，parseRequestBuffer 会返回 REQUEST_ERROR，ServerManager 会生成错误响应或关闭连接。
*/
int sanitizeRequestUri(Request& req) {
	if (req.uri.empty())
		return REQUEST_ERROR;
	if (req.uri.find('#') != std::string::npos)
		return REQUEST_ERROR;
	if (has_bad_uri_char(req.uri))
		return REQUEST_ERROR;
	if (has_invalid_percent_encoding(req.uri))
		return REQUEST_ERROR;

	size_t query_pos = req.uri.find('?');
	std::string path = req.uri.substr(0, query_pos);
	if (path.empty() || path[0] != '/')
		return REQUEST_ERROR;

	std::string lower_path = to_lower(path);
	if (lower_path.find("%00") != std::string::npos || lower_path.find("%2e") != std::string::npos || lower_path.find("%2f") != std::string::npos)
		return REQUEST_ERROR;
	if (path == "/.." || path.find("/../") != std::string::npos)
		return REQUEST_ERROR;
	if (path.size() >= 3 && path.compare(path.size() - 3, 3, "/..") == 0)
		return REQUEST_ERROR;
	if (path.find("../") == 0)
		return REQUEST_ERROR;
	return REQUEST_OK;
}