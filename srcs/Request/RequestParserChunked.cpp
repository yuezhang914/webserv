/*
文件：srcs/Request/RequestParserChunked.cpp
用途：保存 chunk-size、trailer 与完整 chunked body 解析的原有实现。
拆分说明：函数实现从原 RequestParser.cpp 原样移动，不拆分函数、不改变消息边界处理。
*/
#include "RequestParser.hpp"

static const size_t MAX_CHUNK_SIZE_LINE = 1024;
static const size_t MAX_TRAILER_SIZE = 8192;

/*
函数：read_chunk_size_for_buffer
用途：解析一行 chunk-size，并在返回 size 前完成格式、整数范围和累计 body 上限检查。
参数来源：
    - line：chunked body 的 size 行，不含结尾 CRLF，例如 "A" 或 "A;name=value"。
    - current_body_size：已经解码并保存的 body 字节数。
    - body_limit：当前 server/location 的 effective max_body_size。
    - size：输出本次 chunk 的字节数。
返回值：成功返回 REQUEST_OK；格式非法返回 REQUEST_ERROR；整数或累计 body 超限返回 REQUEST_BODY_TOO_LARGE。
实现逻辑：
    1. 分号前必须是非空十六进制数。
    2. 若存在 extension，分号后不能为空且只能含可见 ASCII，拒绝空格和控制字符。
    3. 手动按十六进制累加，先防 unsigned long 溢出。
    4. result 必须能装入 size_t，并为后续 chunk data + CRLF 预留 2 字节。
    5. 使用 result > body_limit - current 的减法形式检查累计上限，避免 current + result 自身先溢出。
*/
int RequestParser::read_chunk_size_for_buffer(const std::string& line,
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
			return REQUEST_BODY_TOO_LARGE;
		result = result * 16 + digit;
		++i;
	}
	const size_t max_size = static_cast<size_t>(-1);
	if (result > static_cast<unsigned long>(max_size)
		|| result > static_cast<unsigned long>(max_size - 2))
		return REQUEST_BODY_TOO_LARGE;
	if (current_body_size > static_cast<size_t>(body_limit))
		return REQUEST_BODY_TOO_LARGE;
	unsigned long current = static_cast<unsigned long>(current_body_size);
	if (current > body_limit || result > body_limit - current)
		return REQUEST_BODY_TOO_LARGE;
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
bool RequestParser::is_forbidden_trailer_name(const std::string& lower_key) {
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
int RequestParser::validate_trailer_line(const std::string& line) {
	size_t colon_pos = line.find(':');
	if (colon_pos == std::string::npos)
		return REQUEST_ERROR;
	std::string key = line.substr(0, colon_pos);
	if (!is_valid_token(key))
		return REQUEST_ERROR;
	std::string raw_value = line.substr(colon_pos + 1);
	if (has_invalid_header_value_char(raw_value))
		return REQUEST_ERROR;
	if (is_forbidden_trailer_name(Request::toLowerAscii(key)))
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
int RequestParser::find_chunked_trailer_end(const std::string& buffer,
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
    - body_limit：parseBuffer 已按 server 和 normalized path 计算出的有效上限。
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
int RequestParser::parse_chunked_buffer(const std::string& buffer,
		size_t body_start, unsigned long body_limit,
        Request& req, size_t& consumed) {
	size_t pos = body_start;
	std::string body;
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
			req._body = body;
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

