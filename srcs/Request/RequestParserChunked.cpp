/*
文件：srcs/Request/RequestParserChunked.cpp
用途：实现 chunk-size、chunk extension、trailer 与完整 chunked body 的严格解析。
修改说明：保留原消息边界和 body-limit 逻辑，并补充 extension 的 token/quoted-string 语法校验。
*/
#include "RequestParser.hpp"

static const size_t MAX_CHUNK_SIZE_LINE = 1024;
static const size_t MAX_TRAILER_SIZE = 8192;

/*
函数：skip_chunk_ows
用途：跳过 chunk extension 语法中允许忽略的空格和水平制表符。
参数来源：is_valid_chunk_extensions() 在分号、名称、等号和值之间调用。
参数修改：pos 从当前检查位置移动到第一个非 OWS 字符，或移动到行尾。
*/
void RequestParser::skip_chunk_ows(const std::string& line, size_t& pos) {
	while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t'))
		++pos;
}

/*
函数：read_chunk_extension_token
用途：从当前位置读取一个非空 HTTP token，供 chunk-ext-name 或未加引号的 chunk-ext-val 使用。
参数来源：is_valid_chunk_extensions() 已把 pos 放在 token 的第一个候选字符。
返回值：至少读取一个合法 token 字符时返回 true；空 token 或首字符非法时返回 false。
参数修改：成功或部分扫描后，pos 指向 token 后的第一个字符。
*/
bool RequestParser::read_chunk_extension_token(const std::string& line,
		size_t& pos) {
	size_t start = pos;
	while (pos < line.size() && is_token_char(line[pos]))
		++pos;
	return pos > start;
}

/*
函数：read_chunk_extension_quoted_string
用途：读取一个完整的双引号 chunk extension value，并验证其中的普通字符和反斜杠转义。
参数来源：is_valid_chunk_extensions() 确认当前位置是双引号后调用。
返回值：找到合法闭合引号时返回 true；未闭合、尾部孤立反斜杠或控制字符返回 false。
实现逻辑：
    1. 首字符必须是双引号。
    2. 普通内容只允许 HTAB、空格和可见 ASCII；双引号只能作为结束符。
    3. 反斜杠必须转义一个 HTAB、空格或可见 ASCII 字符。
    4. 成功时 pos 指向闭合引号后的第一个字符。
*/
bool RequestParser::read_chunk_extension_quoted_string(
		const std::string& line, size_t& pos) {
	if (pos >= line.size() || line[pos] != '"')
		return false;
	++pos;
	while (pos < line.size()) {
		unsigned char c = static_cast<unsigned char>(line[pos]);
		if (c == '"') {
			++pos;
			return true;
		}
		if (c == '\\') {
			++pos;
			if (pos >= line.size())
				return false;
			unsigned char escaped = static_cast<unsigned char>(line[pos]);
			if (escaped != '\t' && (escaped < 32 || escaped > 126))
				return false;
			++pos;
			continue;
		}
		if (c != '\t' && (c < 32 || c > 126))
			return false;
		++pos;
	}
	return false;
}

/*
函数：is_valid_chunk_extensions
用途：严格校验 chunk-size 后面的全部 extension，形状为重复的 ; name [= token/quoted-string]。
参数来源：read_chunk_size_for_buffer() 已跳过 chunk-size 后的可选 OWS，并把 extension_start 放在第一个分号上。
返回值：至少一组 extension 完整且整行被消费时返回 true，否则返回 false。
实现逻辑：
    1. 每组必须以分号开始，分号后跳过可选 OWS。
    2. extension name 必须是非空 HTTP token。
    3. 可选等号两侧允许 OWS；等号后必须有 token 或完整 quoted-string。
    4. 一组结束后只能到达行尾，或由下一个分号开始新的一组。
为什么修改：原实现只检查可见 ASCII，无法拒绝 ;<bad>、;name= 和未闭合引号。
*/
bool RequestParser::is_valid_chunk_extensions(const std::string& line,
		size_t extension_start) {
	size_t pos = extension_start;
	while (pos < line.size()) {
		if (line[pos] != ';')
			return false;
		++pos;
		skip_chunk_ows(line, pos);
		if (!read_chunk_extension_token(line, pos))
			return false;
		skip_chunk_ows(line, pos);
		if (pos < line.size() && line[pos] == '=') {
			++pos;
			skip_chunk_ows(line, pos);
			if (pos >= line.size())
				return false;
			if (line[pos] == '"') {
				if (!read_chunk_extension_quoted_string(line, pos))
					return false;
			}
			else if (!read_chunk_extension_token(line, pos))
				return false;
			skip_chunk_ows(line, pos);
		}
		if (pos == line.size())
			return true;
		if (line[pos] != ';')
			return false;
	}
	return false;
}

/*
函数：read_chunk_size_for_buffer
用途：解析一行 chunk-size，并在返回 size 前完成 extension、整数范围和累计 body 上限检查。
参数来源：
    - line：chunked body 的 size 行，不含结尾 CRLF，例如 "A"、"A;name=value" 或 "A;name=\"x\""。
    - current_body_size：已经解码并保存的 body 字节数。
    - body_limit：当前 server/location 的 effective max_body_size。
    - size：输出本次 chunk 的字节数。
返回值：成功返回 REQUEST_OK；格式非法返回 REQUEST_ERROR；整数或累计 body 超限返回 REQUEST_BODY_TOO_LARGE。
实现逻辑：
    1. 行首必须是非空十六进制数，0x 前缀、符号和混入其他字符均拒绝。
    2. size 后若存在内容，跳过可选 OWS 后必须以分号开始，并由 is_valid_chunk_extensions() 完整校验。
    3. 手动按十六进制累加，先防 unsigned long 溢出。
    4. result 必须能装入 size_t，并为后续 chunk data + CRLF 预留 2 字节。
    5. 使用 result > body_limit - current 的减法形式检查累计上限，避免 current + result 自身先溢出。
*/
int RequestParser::read_chunk_size_for_buffer(const std::string& line,
		size_t current_body_size, unsigned long body_limit, size_t& size) {
	size_t number_end = 0;
	while (number_end < line.size() && hex_value(line[number_end]) >= 0)
		++number_end;
	if (number_end == 0)
		return REQUEST_ERROR;
	size_t syntax_pos = number_end;
	skip_chunk_ows(line, syntax_pos);
	if (syntax_pos < line.size()) {
		if (line[syntax_pos] != ';'
			|| !is_valid_chunk_extensions(line, syntax_pos))
			return REQUEST_ERROR;
	}
	else if (syntax_pos != number_end)
		return REQUEST_ERROR;
	std::string number = line.substr(0, number_end);
	unsigned long result = 0;
	const unsigned long max_ulong = static_cast<unsigned long>(-1);
	size_t i = 0;
	while (i < number.size()) {
		unsigned long digit = static_cast<unsigned long>(hex_value(number[i]));
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

 