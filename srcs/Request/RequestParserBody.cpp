/*
文件：srcs/Request/RequestParserBody.cpp
用途：保存 Content-Length 与 Transfer-Encoding framing 判断的原有实现。
拆分说明：函数实现从原 RequestParser.cpp 原样移动，不改变返回状态或长度检查。
*/
#include "RequestParser.hpp"

/*
函数：parse_content_length
用途：严格解析 Content-Length，并区分格式错误和 body 超限。
参数来源：RequestParser::parseBuffer 在发现 content-length header 后调用；body_limit 来自 getEffectiveBodyLimit(req._config, req._path)。
输出：成功时把长度写入 content_length；格式错误返回 REQUEST_ERROR；数字格式合法但超过 body_limit 或整数范围时返回 REQUEST_BODY_TOO_LARGE。
实现逻辑：
    1. 先完整检查字符串：必须非空，并且每个字符都必须是十进制数字。
    2. 第一遍格式检查结束前不比较 body_limit，保证 12x、1.0、+5 等始终属于 400 格式错误，而不是被误判成 413。
    3. 第二遍手动累加数字，先检查 unsigned long 溢出。
    4. 数字合法但计算结果超过 body_limit 时返回 REQUEST_BODY_TOO_LARGE。
*/
int RequestParser::parse_content_length(const std::string& value, unsigned long body_limit, size_t& content_length) {
    if (value.empty())
        return REQUEST_ERROR;

    // 1. 修剪首尾空格/Tab (OWS)
    size_t start = 0;
    while (start < value.size() && (value[start] == ' ' || value[start] == '\t'))
        ++start;

    size_t end = value.size();
    while (end > start && (value[end - 1] == ' ' || value[end - 1] == '\t'))
        --end;

    // 如果全是空格，非法
    if (start >= end)
        return REQUEST_ERROR;

    // 2. 检查内部字符是否全为数字（不允许符号如 '+' '-' 或中间空格）
    for (size_t i = start; i < end; ++i) {
        if (value[i] < '0' || value[i] > '9')
            return REQUEST_ERROR;
    }

    // 3. 防范过长数字串（超过 20 位 `unsigned long` 必溢出）
    if (end - start > 20) {
        // 如果前面都是 '0' 可以过滤，或者直接判定为 Body Too Large / Error
        // 简单处理：非零过长字符串直接报 BODY_TOO_LARGE
        size_t non_zero = start;
        while (non_zero < end && value[non_zero] == '0')
            ++non_zero;
        if (end - non_zero > 20)
            return REQUEST_BODY_TOO_LARGE;
    }

    // 4. 解析数字与溢出判断
    unsigned long result = 0;
    unsigned long max_ulong = static_cast<unsigned long>(-1);

    for (size_t i = start; i < end; ++i) {
        unsigned long digit = static_cast<unsigned long>(value[i] - '0');
        
        // 检查乘法/加法溢出
        if (result > (max_ulong - digit) / 10)
            return REQUEST_BODY_TOO_LARGE;

        result = result * 10 + digit;

        // 检查配置的 body_limit
        if (result > body_limit)
            return REQUEST_BODY_TOO_LARGE;
    }

    const size_t max_size = static_cast<size_t>(-1);
    if (result > static_cast<unsigned long>(max_size))
        return REQUEST_BODY_TOO_LARGE;

    content_length = static_cast<size_t>(result);
    return REQUEST_OK;
}

/*
函数：is_chunked_transfer_encoding
用途：判断 Transfer-Encoding 是否为本项目支持的 chunked，并拒绝其他传输编码。
参数来源：RequestParser::parseBuffer 在 headers 中发现 transfer-encoding 后调用。
输出：如果没有 transfer-encoding，has_te=false、is_chunked=false；如果值正好是 chunked，二者都为 true；如果是 gzip、compress、gzip, chunked 等本项目不支持的形式，返回 REQUEST_ERROR。
实现逻辑：
    1. 查找 headers["transfer-encoding"]。
    2. 若不存在，说明没有 TE，返回 REQUEST_OK。
    3. 若存在，转小写并去掉两端空白。
    4. 只有严格等于 "chunked" 才允许。
意义：防止非 chunked body 被当成下一个 request 的开头；也防止同时出现复杂 transfer coding 时 parser 无法正确还原 body。
*/
int RequestParser::is_chunked_transfer_encoding(const Request& req, bool& has_te, bool& is_chunked) {
	has_te = false;
	is_chunked = false;
	std::map<std::string, std::string>::const_iterator it = req._headers.find("transfer-encoding");
	if (it == req._headers.end())
		return REQUEST_OK;
	has_te = true;
	std::string value = Request::toLowerAscii(trim_ows(it->second));
	if (value != "chunked")
		return REQUEST_ERROR;
	is_chunked = true;
	return REQUEST_OK;
}

