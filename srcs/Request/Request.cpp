/*
文件：srcs/Request/Request.cpp
HTTP request 基础工具实现。这里保留 Request 的通用工具、URI 安全检查和调试输出；真正的请求读取与解析流程已拆到 RequestParser.cpp。
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
用途：检查 Request.uri 是否安全，防止非法路径或路径穿越。
参数来源：parseRequestBuffer 内部的 parse_request_line 已经把请求行中的 URI 写进 req.uri。
实现逻辑：
    1. URI 不能为空，并且必须以 / 开头。
    2. 调用 has_bad_uri_char，拒绝控制字符和反斜杠。
    3. 转成小写后检查 %00、%2e、%2f，防止编码后的空字符、点、斜杠绕过检查。
    4. 检查 /../、结尾 /..、开头 ../，防止访问 root 外面的文件。
    5. 全部通过返回 REQUEST_OK，否则返回 REQUEST_ERROR。
后续影响：如果这里失败，parseRequestBuffer 会返回 REQUEST_ERROR，ServerManager 会生成错误响应或关闭连接。
*/
int sanitizeRequestUri(Request& req) {
	if (req.uri.empty() || req.uri[0] != '/')
		return REQUEST_ERROR;
	if (has_bad_uri_char(req.uri))
		return REQUEST_ERROR;
	std::string lower = to_lower(req.uri);
	if (lower.find("%00") != std::string::npos || lower.find("%2e") != std::string::npos || lower.find("%2f") != std::string::npos)
		return REQUEST_ERROR;
	if (req.uri.find("/../") != std::string::npos || req.uri.find("/..") == req.uri.size() - 3 || req.uri.find("../") == 0)
		return REQUEST_ERROR;
	return REQUEST_OK;
}
