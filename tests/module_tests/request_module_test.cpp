#include "RequestParser.hpp"
#include "ServerConfig.hpp"
#include "LocationConfig.hpp"
#include <climits>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

static int g_total = 0;
static int g_failed = 0;

/*
函数：beginGroup
用途：在终端中打印一组测试的标题，让 request line、header、body、chunked 等测试容易区分。
参数：name 是当前测试组名称。
返回值：无。
实现逻辑：输出一个带分隔线的标题，不改变任何测试状态。
*/
static void beginGroup(const std::string& name)
{
    std::cout << "\n========== " << name << " ==========" << std::endl;
}

/*
函数：check
用途：记录并打印一条断言的通过或失败结果。
参数：condition 是待验证条件；name 是这条断言的人类可读说明。
返回值：无。
实现逻辑：每调用一次先增加总数；条件为 true 打印 PASS，否则打印 FAIL 并增加失败数。
*/
static void check(bool condition, const std::string& name)
{
    ++g_total;
    if (condition)
        std::cout << "[PASS] " << name << std::endl;
    else {
        std::cout << "[FAIL] " << name << std::endl;
        ++g_failed;
    }
}

/*
函数：parseRaw
用途：用测试准备好的 raw HTTP 字符串直接调用 Request 模块唯一入口。
参数：raw 是原始 HTTP 字节；server 是当前请求使用的配置；req 和 consumed 接收解析结果。
返回值：原样返回 parseRequestBuffer() 的状态。
实现逻辑：把 server 地址传入 parser；本函数不做网络读取，也不修改 raw。
*/
static int parseRaw(const std::string& raw, const ServerConfig& server,
                    Request& req, size_t& consumed)
{
    return parseRequestBuffer(raw, req, &server, consumed);
}

/*
函数：makeBaseServer
用途：创建所有 Request 测试共用的最小 ServerConfig。
参数：无。
返回值：返回 server body limit 为 5 字节，并带三个 location 覆盖规则的配置对象。
实现逻辑：server 默认限制设为 5；/upload/ 放宽到 10；/upload/images/ 收紧到 3；/api 收紧到 2，用于验证最长前缀和 location 路径边界。
*/
static ServerConfig makeBaseServer()
{
    ServerConfig server;
    server.root = "tests/tmp_request_www";
    server.has_root = true;
    server.max_body_size = 5;
    server.has_body_size = true;

    LocationConfig upload;
    upload.path = "/upload/";
    upload.max_body_size = 10;
    upload.has_body_size = true;
    server.locations.push_back(upload);

    LocationConfig images;
    images.path = "/upload/images/";
    images.max_body_size = 3;
    images.has_body_size = true;
    server.locations.push_back(images);

    // 不以 / 结尾的 location，用于验证 /api 不能误匹配 /api-other。
    LocationConfig api;
    api.path = "/api";
    api.max_body_size = 2;
    api.has_body_size = true;
    server.locations.push_back(api);

    return server;
}

/*
函数：testStringAndUriHelpers
用途：直接测试 Request.cpp 的 to_lower() 与 sanitizeRequestUri() 公共工具。
参数：无。
返回值：无；通过 check() 累计结果。
实现逻辑：先验证大小写转换，再分别构造合法 query、fragment、反斜杠、路径穿越，以及合法和畸形 percent-encoding。
*/
static void testStringAndUriHelpers()
{
    beginGroup("Request.cpp 字符串与 URI 安全");
    check(to_lower("HoSt-X") == "host-x", "to_lower 正确转换大小写");

    Request req;
    req.uri = "/safe/file.txt?name=Tom";
    check(sanitizeRequestUri(req) == REQUEST_OK, "合法 path + query 通过 URI 检查");

    req.uri = "/file#fragment";
    check(sanitizeRequestUri(req) == REQUEST_ERROR, "URI fragment 被拒绝");

    req.uri = "/dir\\file";
    check(sanitizeRequestUri(req) == REQUEST_ERROR, "反斜杠路径被拒绝");

    req.uri = "/../secret";
    check(sanitizeRequestUri(req) == REQUEST_ERROR, "原始路径穿越被拒绝");

    req.uri = "/..?x=1";
    check(sanitizeRequestUri(req) == REQUEST_ERROR, "带 query 的 /.. 路径穿越被拒绝");

    req.uri = "/safe/%2E%2e/secret";
    check(sanitizeRequestUri(req) == REQUEST_ERROR, "大小写混合的编码点被拒绝");

    req.uri = "/safe%2Fsecret";
    check(sanitizeRequestUri(req) == REQUEST_ERROR, "编码斜杠被拒绝");

    req.uri = "/safe/file%20name.txt";
    check(sanitizeRequestUri(req) == REQUEST_OK, "合法 %20 percent-encoding 被接受");

    req.uri = "/safe/file%";
    check(sanitizeRequestUri(req) == REQUEST_ERROR, "末尾孤立百分号被拒绝");

    req.uri = "/safe/file%2";
    check(sanitizeRequestUri(req) == REQUEST_ERROR, "不足两位的 percent-encoding 被拒绝");

    req.uri = "/safe/file%ZZ";
    check(sanitizeRequestUri(req) == REQUEST_ERROR, "非十六进制 percent-encoding 被拒绝");

    req.uri = "http://example.com/file";
    check(sanitizeRequestUri(req) == REQUEST_ERROR, "非 / 开头的 absolute-form URI 被拒绝");
}

/*
函数：testValidRequestAndReset
用途：验证最基本 GET 解析、header key 小写化、value 去空白，以及 Request 对象重复使用时会被清空。
参数：server 是当前测试配置。
返回值：无。
实现逻辑：先解析一个带 body/header 的 POST，再用同一个 Request 解析 GET，确认旧 body 和旧 header 不会残留。
*/
static void testValidRequestAndReset(const ServerConfig& server)
{
    beginGroup("合法请求与 Request 重置");
    Request req;
    size_t consumed = 0;

    const std::string post =
        "POST /upload/a.txt HTTP/1.1\r\n"
        "HOST:\t localhost \t\r\n"
        "Content-Length: 3\r\n"
        "X-Test:  value  \r\n"
        "\r\n"
        "abc";
    int status = parseRaw(post, server, req, consumed);
    check(status == REQUEST_OK, "合法 POST 返回 REQUEST_OK");
    check(req.method == "POST", "method 正确保存");
    check(req.uri == "/upload/a.txt", "uri 正确保存");
    check(req.version == "HTTP/1.1", "version 正确保存");
    check(req.headers["host"] == "localhost", "Host key 小写且 value 去掉 OWS");
    check(req.headers["x-test"] == "value", "普通 header value 去掉两端空白");
    check(req.body == "abc", "Content-Length body 正确保存");
    check(consumed == post.size(), "consumed 等于完整 POST 长度");

    const std::string get =
        "GET /ping.html HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "\r\n";
    status = parseRaw(get, server, req, consumed);
    check(status == REQUEST_OK, "同一个 Request 可再次解析 GET");
    check(req.body.empty(), "再次解析前会清空旧 body");
    check(req.headers.find("x-test") == req.headers.end(), "再次解析前会清空旧 headers");
    check(req.config == &server, "req.config 指向传入 ServerConfig");
    check(req.closeConnection == true, "parser 初始化 closeConnection 为 true，等待 ServerManager 再决定策略");
}

/*
函数：testRequestLineRules
用途：验证 request line 必须包含合法 method、URI 和 HTTP/1.1，且不能有多余 token。
参数：server 是当前测试配置。
返回值：无。
实现逻辑：依次发送缺段、多段、错误版本、非法 method 字符，以及严格模式下不应接受的 tab/多空格分隔。
*/
static void testRequestLineRules(const ServerConfig& server)
{
    beginGroup("request line 严格格式");
    Request req;
    size_t consumed = 0;

    check(parseRaw("GET / HTTP/1.1 extra\r\nHost: localhost\r\n\r\n", server, req, consumed) == REQUEST_ERROR,
          "request line 第四段被拒绝");
    check(parseRaw("GET / HTTP/1.0\r\nHost: localhost\r\n\r\n", server, req, consumed) == REQUEST_ERROR,
          "HTTP/1.0 被拒绝");
    check(parseRaw("GE(T / HTTP/1.1\r\nHost: localhost\r\n\r\n", server, req, consumed) == REQUEST_ERROR,
          "method 中非法 token 字符被拒绝");
    check(parseRaw("GET\r\nHost: localhost\r\n\r\n", server, req, consumed) == REQUEST_ERROR,
          "缺少 URI/version 的请求行被拒绝");
    check(parseRaw("GET\t/\tHTTP/1.1\r\nHost: localhost\r\n\r\n", server, req, consumed) == REQUEST_ERROR,
          "严格模式：tab 不能代替 request-line 的 SP");
    check(parseRaw("GET  / HTTP/1.1\r\nHost: localhost\r\n\r\n", server, req, consumed) == REQUEST_ERROR,
          "严格模式：request-line 多余分隔空格被拒绝");
    check(parseRaw(" GET / HTTP/1.1\r\nHost: localhost\r\n\r\n", server, req, consumed) == REQUEST_ERROR,
          "request-line 开头空格被拒绝");
    check(parseRaw("GET / HTTP/1.1 \r\nHost: localhost\r\n\r\n", server, req, consumed) == REQUEST_ERROR,
          "request-line 行尾空格被拒绝");
}

/*
函数：testLineEndingRules
用途：验证 request-line、header 和增量读取边界必须严格使用 CRLF。
参数：server 是当前测试配置。
返回值：无。
实现逻辑：
    1. LF-only 请求和混合 CRLF/LF 请求必须返回 REQUEST_ERROR。
    2. header 中裸 CR 必须返回 REQUEST_ERROR。
    3. 当前 buffer 最后只有一个 '\r' 时可能只是 CRLF 尚未收全，应返回 REQUEST_INCOMPLETE。
*/
static void testLineEndingRules(const ServerConfig& server)
{
    beginGroup("CRLF 行结束");
    Request req;
    size_t consumed = 0;

    check(parseRaw("GET / HTTP/1.1\nHost: localhost\n\n",
                   server, req, consumed) == REQUEST_ERROR,
          "LF-only request 被拒绝");
    check(parseRaw("GET / HTTP/1.1\r\nHost: localhost\n\r\n",
                   server, req, consumed) == REQUEST_ERROR,
          "混合 CRLF 与裸 LF 被拒绝");
    check(parseRaw("GET / HTTP/1.1\rHost: localhost\r\n\r\n",
                   server, req, consumed) == REQUEST_ERROR,
          "request-line 后裸 CR 被拒绝");
    check(parseRaw("GET / HTTP/1.1\r\nHost: localhost\r",
                   server, req, consumed) == REQUEST_INCOMPLETE,
          "buffer 末尾单独 CR 被视为尚未收完整");
}

/*
函数：testHeaderRules
用途：验证 Host 必填、header key 合法性、关键 header 重复检查和 value 控制字符防护。
参数：server 是当前测试配置。
返回值：无。
实现逻辑：构造缺失/重复关键 header、非法 key/value，并检查 hostname、IPv4、方括号 IP literal、端口范围和危险 authority 字符。
*/
static void testHeaderRules(const ServerConfig& server)
{
    beginGroup("header 严格格式");
    Request req;
    size_t consumed = 0;

    check(parseRaw("GET / HTTP/1.1\r\n\r\n", server, req, consumed) == REQUEST_ERROR,
          "HTTP/1.1 缺少 Host 被拒绝");
    check(parseRaw("GET / HTTP/1.1\r\nHost:   \r\n\r\n", server, req, consumed) == REQUEST_ERROR,
          "空 Host 被拒绝");
    check(parseRaw("GET / HTTP/1.1\r\nHost localhost\r\n\r\n", server, req, consumed) == REQUEST_ERROR,
          "没有冒号的 header 被拒绝");
    check(parseRaw("GET / HTTP/1.1\r\n: value\r\nHost: localhost\r\n\r\n", server, req, consumed) == REQUEST_ERROR,
          "空 header key 被拒绝");
    check(parseRaw("GET / HTTP/1.1\r\nBad Header: x\r\nHost: localhost\r\n\r\n", server, req, consumed) == REQUEST_ERROR,
          "带空格的 header key 被拒绝");
    check(parseRaw("GET / HTTP/1.1\r\nHost: a\r\nhOsT: b\r\n\r\n", server, req, consumed) == REQUEST_ERROR,
          "大小写不同的重复 Host 被拒绝");
    check(parseRaw("POST /upload/a HTTP/1.1\r\nHost: a\r\nContent-Length: 0\r\ncontent-length: 0\r\n\r\n", server, req, consumed) == REQUEST_ERROR,
          "大小写不同的重复 Content-Length 被拒绝");
    check(parseRaw("POST /upload/a HTTP/1.1\r\nHost: a\r\nTransfer-Encoding: chunked\r\ntransfer-encoding: chunked\r\n\r\n0\r\n\r\n", server, req, consumed) == REQUEST_ERROR,
          "严格模式：重复 Transfer-Encoding 被拒绝");
    check(parseRaw("GET / HTTP/1.1\r\nHost: bad host\r\n\r\n", server, req, consumed) == REQUEST_ERROR,
          "严格模式：含空格的非法 Host value 被拒绝");
    check(parseRaw("GET / HTTP/1.1\r\nHost: localhost:8080\r\n\r\n", server, req, consumed) == REQUEST_OK,
          "Host 支持 hostname:port");
    check(parseRaw("GET / HTTP/1.1\r\nHost: 127.0.0.1:8080\r\n\r\n", server, req, consumed) == REQUEST_OK,
          "Host 支持 IPv4:port");
    check(parseRaw("GET / HTTP/1.1\r\nHost: [::1]:8080\r\n\r\n", server, req, consumed) == REQUEST_OK,
          "Host 支持方括号 IP literal 与端口");
    check(parseRaw("GET / HTTP/1.1\r\nHost: localhost:abc\r\n\r\n", server, req, consumed) == REQUEST_ERROR,
          "Host 非数字端口被拒绝");
    check(parseRaw("GET / HTTP/1.1\r\nHost: localhost:0\r\n\r\n", server, req, consumed) == REQUEST_ERROR,
          "Host 端口 0 被拒绝");
    check(parseRaw("GET / HTTP/1.1\r\nHost: localhost:65536\r\n\r\n", server, req, consumed) == REQUEST_ERROR,
          "Host 超范围端口被拒绝");
    check(parseRaw("GET / HTTP/1.1\r\nHost: :8080\r\n\r\n", server, req, consumed) == REQUEST_ERROR,
          "Host 主机部分为空被拒绝");
    check(parseRaw("GET / HTTP/1.1\r\nHost: ::1\r\n\r\n", server, req, consumed) == REQUEST_ERROR,
          "Host 中未加方括号的 IPv6 形式被拒绝");
    check(parseRaw("GET / HTTP/1.1\r\nHost: example.com/path\r\n\r\n", server, req, consumed) == REQUEST_ERROR,
          "Host 中路径字符被拒绝");
    check(parseRaw("GET / HTTP/1.1\r\nHost: user@example.com\r\n\r\n", server, req, consumed) == REQUEST_ERROR,
          "Host 中 userinfo 被拒绝");
    check(parseRaw("GET / HTTP/1.1\r\nHost: [::1\r\n\r\n", server, req, consumed) == REQUEST_ERROR,
          "Host 未闭合方括号被拒绝");

    std::string control = "GET / HTTP/1.1\r\nHost: localhost\r\nX-Test: a";
    control.push_back(static_cast<char>(1));
    control += "b\r\n\r\n";
    check(parseRaw(control, server, req, consumed) == REQUEST_ERROR,
          "严格模式：header value 中控制字符被拒绝");
}

/*
函数：testHeaderSizeLimit
用途：验证 header 未结束或已经结束时都不能无限增长超过 MAX_HEADER_SIZE。
参数：server 是当前测试配置。
返回值：无。
实现逻辑：生成超过 8192 字节的 header；一份不带结束标记，一份带结束标记，两者都应返回 REQUEST_ERROR。
*/
static void testHeaderSizeLimit(const ServerConfig& server)
{
    beginGroup("header 大小限制");
    Request req;
    size_t consumed = 0;
    std::string large = "GET / HTTP/1.1\r\nHost: localhost\r\nX-Fill: ";
    large += std::string(8300, 'a');

    check(parseRaw(large, server, req, consumed) == REQUEST_ERROR,
          "未结束且超过上限的 header 被拒绝");
    large += "\r\n\r\n";
    check(parseRaw(large, server, req, consumed) == REQUEST_ERROR,
          "已结束但超过上限的 header 被拒绝");
}

/*
函数：testContentLengthRules
用途：严格区分 Content-Length 格式错误、请求未收完整和 body 超限。
参数：server 是当前测试配置。
返回值：无。
实现逻辑：测试空值、正负号、字母、小数、零、前导零、未收够 body、恰好限制和超过限制。
*/
static void testContentLengthRules(const ServerConfig& server)
{
    beginGroup("Content-Length");
    Request req;
    size_t consumed = 0;

    const char* invalid_values[] = {"", "+1", "-1", "1.0", "0x10", "12x"};
    for (size_t i = 0; i < sizeof(invalid_values) / sizeof(invalid_values[0]); ++i) {
        std::string raw = "POST /upload/a HTTP/1.1\r\nHost: localhost\r\nContent-Length: ";
        raw += invalid_values[i];
        raw += "\r\n\r\n";
        check(parseRaw(raw, server, req, consumed) == REQUEST_ERROR,
              std::string("非法 Content-Length 被拒绝：'") + invalid_values[i] + "'");
    }

    check(parseRaw("POST /upload/a HTTP/1.1\r\nHost: localhost\r\nContent-Length: 0\r\n\r\n", server, req, consumed) == REQUEST_OK,
          "Content-Length: 0 合法");
    check(parseRaw("POST /upload/a HTTP/1.1\r\nHost: localhost\r\nContent-Length: 003\r\n\r\nabc", server, req, consumed) == REQUEST_OK,
          "纯数字前导零合法");
    check(parseRaw("POST /upload/a HTTP/1.1\r\nHost: localhost\r\nContent-Length: 5\r\n\r\nhe", server, req, consumed) == REQUEST_INCOMPLETE,
          "body 少于 Content-Length 返回 REQUEST_INCOMPLETE");
    check(parseRaw("POST /a HTTP/1.1\r\nHost: localhost\r\nContent-Length: 5\r\n\r\n12345", server, req, consumed) == REQUEST_OK,
          "body 恰好等于 server limit 合法");
    check(parseRaw("POST /a HTTP/1.1\r\nHost: localhost\r\nContent-Length: 6\r\n\r\n", server, req, consumed) == ERROR_MAX_BODY_LENGTH,
          "Content-Length 超过 server limit 提前返回 413 状态");

    ServerConfig huge = server;
    huge.max_body_size = static_cast<unsigned long>(-1);
    std::ostringstream max_length;
    max_length << static_cast<size_t>(-1);
    std::string huge_raw =
        "POST /a HTTP/1.1\r\nHost: localhost\r\nContent-Length: ";
    huge_raw += max_length.str();
    huge_raw += "\r\n\r\n";
    check(parseRaw(huge_raw, huge, req, consumed) == REQUEST_INCOMPLETE,
          "极大 Content-Length 不会让 body_start + length 溢出并误判完整");
}

/*
函数：testEffectiveBodyLimits
用途：验证 server 默认限制、location 覆盖、最长前缀 location 和 query string 去除。
参数：server 带 /upload/ 与 /upload/images/ 两级限制。
返回值：无。
实现逻辑：验证 /upload/ 与 /upload/images/ 的最长匹配、query 去除，以及不以斜杠结尾的 /api 只能匹配自身或 /api/...，不能误匹配 /api-other。
*/
static void testEffectiveBodyLimits(const ServerConfig& server)
{
    beginGroup("effective body limit");
    Request req;
    size_t consumed = 0;

    check(parseRaw("POST /upload/a HTTP/1.1\r\nHost: localhost\r\nContent-Length: 6\r\n\r\n123456", server, req, consumed) == REQUEST_OK,
          "/upload/ location 放宽到 10 字节");
    check(parseRaw("POST /upload/images/a HTTP/1.1\r\nHost: localhost\r\nContent-Length: 4\r\n\r\n", server, req, consumed) == ERROR_MAX_BODY_LENGTH,
          "最长前缀 /upload/images/ 的 3 字节限制优先");
    check(parseRaw("POST /upload/a?x=1 HTTP/1.1\r\nHost: localhost\r\nContent-Length: 6\r\n\r\n123456", server, req, consumed) == REQUEST_OK,
          "location 匹配会忽略 query string");
    check(parseRaw("POST /api/item HTTP/1.1\r\nHost: localhost\r\nContent-Length: 3\r\n\r\n", server, req, consumed) == ERROR_MAX_BODY_LENGTH,
          "/api/item 正确使用 /api 的 2 字节限制");
    check(parseRaw("POST /api-other HTTP/1.1\r\nHost: localhost\r\nContent-Length: 3\r\n\r\nabc", server, req, consumed) == REQUEST_OK,
          "/api 不会误匹配 /api-other");
}

/*
函数：testChunkedRules
用途：覆盖 chunked body 的正常、多 chunk、大小写十六进制、extension、trailer、不完整和错误格式。
参数：server 是当前测试配置。
返回值：无。
实现逻辑：逐个构造 chunked raw request，检查解码、framing、extension、size-line/trailer 上限、严格 CRLF、禁止 trailer 字段及长度运算溢出。
*/
static void testChunkedRules(const ServerConfig& server)
{
    beginGroup("Transfer-Encoding: chunked");
    Request req;
    size_t consumed = 0;
    int status;

    const std::string multi =
        "POST /upload/a HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Transfer-Encoding: ChUnKeD\r\n\r\n"
        "2\r\nhe\r\n"
        "3;name=value\r\nllo\r\n"
        "0\r\n"
        "X-Trailer: done\r\n\r\n";
    status = parseRaw(multi, server, req, consumed);
    check(status == REQUEST_OK, "多 chunk + extension + trailer 解析成功");
    check(req.body == "hello", "多个 chunk 被合并为 hello");
    check(consumed == multi.size(), "trailer 被完整计入 consumed");

    check(parseRaw("POST /upload/a HTTP/1.1\r\nHost: localhost\r\nTransfer-Encoding: chunked\r\n\r\nA\r\n0123456789\r\n0\r\n\r\n", server, req, consumed) == REQUEST_OK,
          "大写十六进制 A 且恰好 location limit 合法");
    check(parseRaw("POST /upload/a HTTP/1.1\r\nHost: localhost\r\nTransfer-Encoding: chunked\r\n\r\nB\r\n", server, req, consumed) == ERROR_MAX_BODY_LENGTH,
          "超限 chunk 只收到 size 行就立即返回 413 状态");
    check(parseRaw("POST /upload/a HTTP/1.1\r\nHost: localhost\r\nTransfer-Encoding: gzip\r\n\r\n", server, req, consumed) == REQUEST_ERROR,
          "不支持的 Transfer-Encoding 被拒绝");
    check(parseRaw("POST /upload/a HTTP/1.1\r\nHost: localhost\r\nTransfer-Encoding: gzip, chunked\r\n\r\n0\r\n\r\n", server, req, consumed) == REQUEST_ERROR,
          "复杂 transfer coding 被拒绝");
    check(parseRaw("POST /upload/a HTTP/1.1\r\nHost: localhost\r\nTransfer-Encoding: chunked\r\nContent-Length: 0\r\n\r\n0\r\n\r\n", server, req, consumed) == REQUEST_ERROR,
          "Transfer-Encoding 与 Content-Length 同时出现被拒绝");
    check(parseRaw("POST /upload/a HTTP/1.1\r\nHost: localhost\r\nTransfer-Encoding: chunked\r\n\r\nZ\r\n", server, req, consumed) == REQUEST_ERROR,
          "非法十六进制 chunk size 被拒绝");
    check(parseRaw("POST /upload/a HTTP/1.1\r\nHost: localhost\r\nTransfer-Encoding: chunked\r\n\r\n2\r\na", server, req, consumed) == REQUEST_INCOMPLETE,
          "chunk data 未收完整返回 REQUEST_INCOMPLETE");
    check(parseRaw("POST /upload/a HTTP/1.1\r\nHost: localhost\r\nTransfer-Encoding: chunked\r\n\r\n2\r\nabXX", server, req, consumed) == REQUEST_ERROR,
          "chunk data 后缺少 CRLF 被拒绝");
    check(parseRaw("POST /upload/a HTTP/1.1\r\nHost: localhost\r\nTransfer-Encoding: chunked\r\n\r\n0\r\nX-Trailer: a\r\n", server, req, consumed) == REQUEST_INCOMPLETE,
          "trailer 未结束返回 REQUEST_INCOMPLETE");
    check(parseRaw("POST /upload/a HTTP/1.1\r\nHost: localhost\r\nTransfer-Encoding: chunked\r\n\r\n0\r\nBad Trailer: x\r\n\r\n", server, req, consumed) == REQUEST_ERROR,
          "非法 trailer key 被拒绝");

    std::string long_chunk_line =
        "POST /upload/a HTTP/1.1\r\nHost: localhost\r\n"
        "Transfer-Encoding: chunked\r\n\r\n";
    long_chunk_line += std::string(1100, 'A');
    check(parseRaw(long_chunk_line, server, req, consumed) == REQUEST_ERROR,
          "未结束且超过上限的 chunk-size 行被拒绝");
    long_chunk_line += "\r\n";
    check(parseRaw(long_chunk_line, server, req, consumed) == REQUEST_ERROR,
          "已经结束但超过上限的 chunk-size 行被拒绝");

    check(parseRaw("POST /upload/a HTTP/1.1\r\nHost: localhost\r\nTransfer-Encoding: chunked\r\n\r\n1;bad ext\r\na\r\n0\r\n\r\n", server, req, consumed) == REQUEST_ERROR,
          "chunk extension 中空格被拒绝");

    ServerConfig huge = server;
    huge.max_body_size = static_cast<unsigned long>(-1);
    std::ostringstream max_chunk;
    max_chunk << std::hex << static_cast<size_t>(-1);
    std::string overflow_chunk =
        "POST /a HTTP/1.1\r\nHost: localhost\r\n"
        "Transfer-Encoding: chunked\r\n\r\n"
        "1\r\na\r\n";
    overflow_chunk += max_chunk.str();
    overflow_chunk += "\r\n";
    check(parseRaw(overflow_chunk, huge, req, consumed) == ERROR_MAX_BODY_LENGTH,
          "累计 chunk 大小使用防溢出减法检查");

    std::string long_trailer =
        "POST /upload/a HTTP/1.1\r\nHost: localhost\r\n"
        "Transfer-Encoding: chunked\r\n\r\n0\r\nX-Fill: ";
    long_trailer += std::string(8300, 'a');
    check(parseRaw(long_trailer, server, req, consumed) == REQUEST_ERROR,
          "未结束且超过上限的 trailer 被拒绝");
    long_trailer += "\r\n\r\n";
    check(parseRaw(long_trailer, server, req, consumed) == REQUEST_ERROR,
          "已结束但超过上限的 trailer 被拒绝");

    check(parseRaw("POST /upload/a HTTP/1.1\r\nHost: localhost\r\nTransfer-Encoding: chunked\r\n\r\n0\r\nX-Test: a\n\n", server, req, consumed) == REQUEST_ERROR,
          "trailer 中裸 LF 被拒绝");
    check(parseRaw("POST /upload/a HTTP/1.1\r\nHost: localhost\r\nTransfer-Encoding: chunked\r\n\r\n0\r\nContent-Length: 1\r\n\r\n", server, req, consumed) == REQUEST_ERROR,
          "trailer 不能重新定义 Content-Length");

    std::string trailer_control =
        "POST /upload/a HTTP/1.1\r\nHost: localhost\r\n"
        "Transfer-Encoding: chunked\r\n\r\n0\r\nX-Test: a";
    trailer_control.push_back(static_cast<char>(1));
    trailer_control += "b\r\n\r\n";
    check(parseRaw(trailer_control, server, req, consumed) == REQUEST_ERROR,
          "trailer value 中控制字符被拒绝");
}

/*
函数：testBinaryAndPipeline
用途：验证 parser 按字节保存 NUL body，并且 consumed 只覆盖当前第一个 request。
参数：server 是当前测试配置。
返回值：无。
实现逻辑：构造含 NUL 的 3 字节 body；再把两个 GET 拼在同一 buffer 中，检查第一次只消费第一个。
*/
static void testBinaryAndPipeline(const ServerConfig& server)
{
    beginGroup("二进制 body 与 pipeline");
    Request req;
    size_t consumed = 0;

    std::string binary =
        "POST /upload/bin HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Content-Length: 3\r\n\r\n";
    binary.append("a\0b", 3);
    int status = parseRaw(binary, server, req, consumed);
    check(status == REQUEST_OK, "含 NUL 的二进制 body 解析成功");
    check(req.body.size() == 3 && req.body[0] == 'a' && req.body[1] == '\0' && req.body[2] == 'b',
          "二进制 body 不会在 NUL 处截断");

    const std::string first = "GET /one HTTP/1.1\r\nHost: localhost\r\n\r\n";
    const std::string second = "GET /two HTTP/1.1\r\nHost: localhost\r\n\r\n";
    status = parseRaw(first + second, server, req, consumed);
    check(status == REQUEST_OK, "一个 buffer 含两个 request 时先解析第一个");
    check(req.uri == "/one", "pipeline 第一次得到第一个 URI");
    check(consumed == first.size(), "pipeline consumed 不吞掉第二个 request");
}

/*
函数：main
用途：按由基础到复杂的顺序运行全部 Request 模块测试并返回适合脚本判断的退出码。
参数：无。
返回值：全部通过返回 0；至少一项失败返回 1。
实现逻辑：创建共用配置，依次调用每个测试组，最后输出总断言数、通过数和失败数。
*/
int main()
{
    ServerConfig server = makeBaseServer();

    testStringAndUriHelpers();
    testValidRequestAndReset(server);
    testRequestLineRules(server);
    testLineEndingRules(server);
    testHeaderRules(server);
    testHeaderSizeLimit(server);
    testContentLengthRules(server);
    testEffectiveBodyLimits(server);
    testChunkedRules(server);
    testBinaryAndPipeline(server);

    std::cout << "\n========== Request 测试汇总 ==========" << std::endl;
    std::cout << "总断言：" << g_total << std::endl;
    std::cout << "通过：" << (g_total - g_failed) << std::endl;
    std::cout << "失败：" << g_failed << std::endl;
    if (g_failed != 0)
        std::cout << "说明：失败项可能是 parser 尚未覆盖的严格格式边界，而不一定是测试脚本错误。" << std::endl;
    return g_failed == 0 ? 0 : 1;
}
