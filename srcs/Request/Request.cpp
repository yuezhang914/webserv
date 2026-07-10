/*
文件：srcs/Request/Request.cpp
HTTP request 基础工具实现。 Request 的通用工具、URI 安全检查和调试输出；真正的请求读取与解析流程已拆到 RequestParser.cpp。
*/
#include "Request.hpp"

/*
函数：to_lower
用途：把字符串转成小写。
参数来源：header 名称、Host 值、URI 安全检查等。
实现逻辑：
    1. 复制输入字符串到 res，避免修改原字符串。
    2. 使用 std::transform 和 ::tolower 把每个字符变成小写。
    3. 返回小写后的副本。
为什么需要：HTTP header 名称大小写不敏感，把 key 统一成小写后，后面可以稳定查 headers["content-length"]。
*/
std::string to_lower(const std::string& str) {
	std::string res = str;
	std::transform(res.begin(), res.end(), res.begin(), ::tolower);
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
函数：sanitizeRequestUri
用途：检查 Request.uri 是否安全，防止非法路径、控制字符、fragment 和路径穿越。
参数来源：parseRequestBuffer 内部的 parse_request_line 已经把请求行中的 URI 写进 req.uri；URI 可能带 query，例如 /search?q=a。
实现逻辑：
    1. URI 不能为空，不能包含 HTTP fragment 标记 #，因为 fragment 不应该出现在 HTTP request target 中。
    2. 先按 ? 分离 path 和 query；路径安全检查只针对 path，避免 query 干扰 /.. 结尾判断。
    3. path 必须以 / 开头。
    4. 对完整 URI 调用 has_bad_uri_char，拒绝控制字符和反斜杠。
    5. 对 path 的小写版本检查 %00、%2e、%2f，防止编码后的空字符、点和斜杠绕过路径检查。
    6. 对 path 检查 /../、path == /..、结尾 /..、开头 ../ 等路径穿越形式。
    7. 全部通过返回 REQUEST_OK，否则返回 REQUEST_ERROR。
后续影响：如果这里失败，parseRequestBuffer 会返回 REQUEST_ERROR，ServerManager 会生成错误响应或关闭连接。
*/
int sanitizeRequestUri(Request& req) {
	if (req.uri.empty())
		return REQUEST_ERROR;
	if (req.uri.find('#') != std::string::npos)
		return REQUEST_ERROR;
	if (has_bad_uri_char(req.uri))
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
