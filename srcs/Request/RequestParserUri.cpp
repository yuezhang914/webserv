/*
文件：srcs/Request/RequestParserUri.cpp
用途：保存 URI 字符检查、percent-decoding、path normalization 与 query 分离的原有实现。
拆分说明：函数实现从原 RequestParser.cpp 原样移动，不拆分函数、不改变解析规则。
*/
#include "RequestParser.hpp"
#include <vector>

/*
函数：has_bad_uri_char
用途：检查 request-target 中是否含有控制字符、DEL 或反斜杠。
参数来源：split_and_normalize_uri() 传入完整 request-target。
返回值：发现危险字节时返回 true，否则返回 false。
*/
bool RequestParser::has_bad_uri_char(const std::string& uri) {
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
函数：has_invalid_percent_encoding
用途：检查 URI 中每一个 % 是否都严格跟着两个十六进制字符。
参数来源：split_and_normalize_uri() 传入完整 request-target。
返回值：孤立 %、长度不足或 %ZZ 这类非法编码返回 true。
*/
bool RequestParser::has_invalid_percent_encoding(const std::string& uri) {
	size_t i = 0;
	while (i < uri.size()) {
		if (uri[i] != '%') {
			++i;
			continue;
		}
		if (i + 2 >= uri.size())
			return true;
		if (hex_value(uri[i + 1]) < 0 || hex_value(uri[i + 2]) < 0)
			return true;
		i += 3;
	}
	return false;
}

/*
函数：hex_value
用途：把一个已经确认是十六进制数字的字符转换为 0 到 15。
返回值：非法字符返回 -1；调用方据此拒绝畸形 percent-encoding。
*/
int RequestParser::hex_value(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

/*
函数：decode_path
用途：对 URI 的 path 部分执行且只执行一次 percent-decoding。
参数来源：split_and_normalize_uri() 已经从原始 request-target 中分出的 encoded path。
输出：decoded 接收解码后的字节序列。
安全检查：
    1. 每个 % 后必须有两个十六进制字符。
    2. 解码结果不能是 NUL、ASCII 控制字符、DEL 或反斜杠。
    3. 普通原始字符也要经过相同危险字符检查。
说明：编码斜杠 %2F 会被解码为 '/'，之后统一参与 segment normalization；不能绕过 dot-segment 检查。
*/
int RequestParser::decode_path(const std::string& encoded,
        std::string& decoded) {
    decoded.clear();
    size_t i = 0;
    while (i < encoded.size()) {
        unsigned char value;
        if (encoded[i] == '%') {
            if (i + 2 >= encoded.size())
                return REQUEST_ERROR;
            int high = hex_value(encoded[i + 1]);
            int low = hex_value(encoded[i + 2]);
            if (high < 0 || low < 0)
                return REQUEST_ERROR;
            value = static_cast<unsigned char>(high * 16 + low);
            i += 3;
        }
        else {
            value = static_cast<unsigned char>(encoded[i]);
            ++i;
        }
        if (value < 32 || value == 127 || value == 0 || value == '\\')
            return REQUEST_ERROR;
        decoded.push_back(static_cast<char>(value));
    }
    return REQUEST_OK;
}

/*
函数：normalize_path
用途：把已经 percent-decoding 的绝对 path 统一成不含空 segment、'.' 和 '..' 的安全路径。
实现逻辑：
    1. path 必须以 '/' 开头。
    2. 连续 '/' 和单点 segment 被忽略。
    3. '..' 弹出前一个普通 segment；没有可弹出的 segment 表示试图越过根目录，直接拒绝。
    4. 保留有意义的尾部斜杠，例如 /a/、/a/. 和 /a/b/.. 都得到目录形式。
例子：/a//b/./c -> /a/b/c；/a/b/../c -> /a/c；/../secret -> REQUEST_ERROR。
*/
int RequestParser::normalize_path(const std::string& decoded,
        std::string& normalized) {
    normalized.clear();
    if (decoded.empty() || decoded[0] != '/')
        return REQUEST_ERROR;

    bool keep_trailing_slash = decoded.size() > 1
        && (decoded[decoded.size() - 1] == '/'
            || (decoded.size() >= 2
                && decoded.compare(decoded.size() - 2, 2, "/.") == 0)
            || (decoded.size() >= 3
                && decoded.compare(decoded.size() - 3, 3, "/..") == 0));

    std::vector<std::string> segments;
    size_t start = 1;
    while (start <= decoded.size()) {
        size_t slash = decoded.find('/', start);
        size_t end = slash == std::string::npos ? decoded.size() : slash;
        std::string segment = decoded.substr(start, end - start);
        if (segment.empty() || segment == ".") {
            /* 连续斜杠和当前目录不加入结果。 */
        }
        else if (segment == "..") {
            if (segments.empty())
                return REQUEST_ERROR;
            segments.pop_back();
        }
        else
            segments.push_back(segment);
        if (slash == std::string::npos)
            break;
        start = slash + 1;
    }

    normalized = "/";
    size_t i = 0;
    while (i < segments.size()) {
        if (normalized.size() > 1)
            normalized += "/";
        normalized += segments[i];
        ++i;
    }
    if (keep_trailing_slash && normalized != "/")
        normalized += "/";
    return REQUEST_OK;
}

/*
函数：split_and_normalize_uri
用途：把原始 request-target 分成 raw query 和可安全路由的 normalized path。
参数来源：parse_request_line() 从请求行第二段取得的原始 URI。
输出：path 保存一次 percent-decoding + normalization 的结果；query 保存 '?' 后原始内容，不包含问号。
实现顺序：
    1. 拒绝空 URI、fragment、控制字符、DEL 和反斜杠。
    2. 按第一个 '?' 分开 encoded path 与 raw query。
    3. encoded path 必须以 '/' 开头，全部 percent-encoding 必须是 %XX。
    4. decode_path() 后再 normalize_path()，因此编码的点或斜杠不能绕过路径安全检查。
*/
int RequestParser::split_and_normalize_uri(const std::string& uri,
        std::string& path, std::string& query) {
    path.clear();
    query.clear();
    if (uri.empty() || uri.find('#') != std::string::npos)
        return REQUEST_ERROR;
    if (has_bad_uri_char(uri) || has_invalid_percent_encoding(uri))
        return REQUEST_ERROR;

    size_t query_pos = uri.find('?');
    std::string encoded_path = uri.substr(0, query_pos);
    if (query_pos != std::string::npos)
        query = uri.substr(query_pos + 1);
    if (encoded_path.empty() || encoded_path[0] != '/')
        return REQUEST_ERROR;

    std::string decoded;
    if (decode_path(encoded_path, decoded) != REQUEST_OK)
        return REQUEST_ERROR;
    return normalize_path(decoded, path);
}

