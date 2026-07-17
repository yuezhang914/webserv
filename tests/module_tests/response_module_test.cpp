/*
文件：tests/module_tests/response_module_test.cpp
用途：用真实拆分版 RequestParser 和测试配置类型验证 Response class、路由、静态文件、上传与删除。
覆盖重点：旧接口清理后的封装一致性、无 body 状态、header 大小写、autoindex 安全、上传路径和二进制数据。
*/
#include "Response.hpp"
#include "RequestParser.hpp"
#include "ServerConfig.hpp"
#include "LocationConfig.hpp"

#include <fstream>
#include <iostream>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

static int g_total = 0;
static int g_failed = 0;
static const std::string ROOT = "tests/tmp_response_www";

static void beginGroup(const std::string &name)
{
    std::cout << "\n========== " << name << " ==========" << std::endl;
}

static void check(bool condition, const std::string &name)
{
    ++g_total;
    if (condition)
        std::cout << "[PASS] " << name << std::endl;
    else
    {
        std::cout << "[FAIL] " << name << std::endl;
        ++g_failed;
    }
}

static bool writeFile(const std::string &path, const std::string &content)
{
    std::ofstream output(path.c_str(), std::ios::binary);
    if (!output)
        return false;
    output.write(content.data(), static_cast<std::streamsize>(content.size()));
    output.close();
    return static_cast<bool>(output);
}

static std::string readFile(const std::string &path)
{
    std::ifstream input(path.c_str(), std::ios::binary);
    if (!input)
        return "";
    std::ostringstream output;
    output << input.rdbuf();
    return output.str();
}

static bool exists(const std::string &path)
{
    struct stat pathInfo;
    return stat(path.c_str(), &pathInfo) == 0;
}

static std::string lowerAscii(const std::string &value)
{
    std::string result = value;
    size_t i = 0;
    while (i < result.size())
    {
        if (result[i] >= 'A' && result[i] <= 'Z')
            result[i] = static_cast<char>(result[i] - 'A' + 'a');
        ++i;
    }
    return result;
}

/* 通过真实 RequestParser 构造只读 Request，测试不绕过 Request class 封装。 */
static bool parseRequest(const std::string &method, const std::string &target,
                         const std::string &extraHeaders,
                         const std::string &wireBody,
                         const ServerConfig &server, Request &request)
{
    std::string raw = method + " " + target + " HTTP/1.1\r\n";
    raw += "Host: localhost\r\n";
    raw += extraHeaders;
    raw += "\r\n";
    raw += wireBody;
    size_t consumed = 0;
    int status = RequestParser::parseBuffer(raw, request, &server, consumed);
    return status == REQUEST_OK && consumed == raw.size();
}

static bool headerEquals(const Response &response, const std::string &name,
                         const std::string &expected)
{
    std::string value;
    return response.getHeader(name, value) && value == expected;
}

static bool headerMissing(const Response &response, const std::string &name)
{
    std::string value;
    return !response.getHeader(name, value);
}

static size_t countHeaderName(const Response &response,
                              const std::string &name)
{
    size_t count = 0;
    std::string expected = lowerAscii(name);
    Response::HeaderMap::const_iterator it = response.getHeaders().begin();
    while (it != response.getHeaders().end())
    {
        if (lowerAscii(it->first) == expected)
            ++count;
        ++it;
    }
    return count;
}

static ServerConfig makeServer()
{
    ServerConfig server;
    server.root = ROOT;
    server.has_root = true;
    server.index.push_back("missing-index.html");
    server.index.push_back("index.html");
    server.upload_path = "/upload/";
    server.allow_methods.insert("GET");
    server.allow_methods.insert("POST");
    server.allow_methods.insert("DELETE");

    LocationConfig readOnly;
    readOnly.path = "/readonly/";
    readOnly.allow_methods.insert("GET");
    server.locations.push_back(readOnly);

    LocationConfig redirect;
    redirect.path = "/old/";
    redirect.redirect_status = 301;
    redirect.redirect_url = "/new/";
    server.locations.push_back(redirect);

    LocationConfig notModified;
    notModified.path = "/not-modified/";
    notModified.redirect_status = 304;
    notModified.redirect_url = "/cached/";
    server.locations.push_back(notModified);

    LocationConfig listing;
    listing.path = "/list/";
    listing.has_autoindex = true;
    listing.autoindex = true;
    server.locations.push_back(listing);

    LocationConfig textIndex;
    textIndex.path = "/text-index/";
    textIndex.index.push_back("readme.txt");
    server.locations.push_back(textIndex);

    LocationConfig joinedUpload;
    joinedUpload.path = "/joined/";
    joinedUpload.upload_path = "joined_upload";
    server.locations.push_back(joinedUpload);

    LocationConfig missingUpload;
    missingUpload.path = "/missing-upload/";
    missingUpload.upload_path = "does-not-exist";
    server.locations.push_back(missingUpload);

    LocationConfig alias;
    alias.path = "/alias/";
    alias.has_alias = true;
    alias.alias = ROOT + "/alias_target/";
    alias.allow_methods.insert("GET");
    server.locations.push_back(alias);

    LocationConfig cgi;
    cgi.path = "/cgi/";
    cgi.allow_methods.insert("GET");
    cgi.cgi_extensions[".py"] = "/usr/bin/python3";
    server.locations.push_back(cgi);
    return server;
}

static void prepareFiles(ServerConfig &server)
{
    beginGroup("准备测试文件");
    check(writeFile(ROOT + "/index.html", "home\n"), "创建 index.html");
    check(writeFile(ROOT + "/ping.html", "pong\n"), "创建 ping.html");
    check(writeFile(ROOT + "/hello world.txt", "space"), "创建 percent-decoding 目标文件");
    check(writeFile(ROOT + "/style.css", "body{}"), "创建 CSS 文件");
    check(writeFile(ROOT + "/image.png", std::string("\x89PNG", 4)), "创建 PNG 文件");
    check(writeFile(ROOT + "/delete_me.txt", "delete"), "创建 DELETE 文件");
    check(writeFile(ROOT + "/query_delete.txt", "delete query"), "创建 query DELETE 文件");
    check(writeFile(ROOT + "/upload/same.txt", "old"), "创建同名上传文件");
    check(writeFile(ROOT + "/list/<unsafe>.txt", "x"), "创建 autoindex HTML 转义文件");
    check(writeFile(ROOT + "/list/a?b.txt", "x"), "创建 autoindex URL 编码文件");
    check(writeFile(ROOT + "/text-index/readme.txt", "plain index"), "创建 text index 文件");
    check(writeFile(ROOT + "/alias_target/hello.txt", "alias works"), "创建 alias 文件");
    check(writeFile(ROOT + "/cgi/script.py", "SECRET_CGI_SOURCE"), "创建 CGI 占位脚本");
    check(writeFile(ROOT + "/custom404.html", "<html>custom 404</html>"), "创建自定义错误页");
    check(mkfifo((ROOT + "/blocked.fifo").c_str(), 0600) == 0, "创建 FIFO 防阻塞目标");
    server.error_pages[404] = ROOT + "/custom404.html";
}

static void testResponseClass(const ServerConfig &server)
{
    beginGroup("Response class 与 header 一致性");
    Response response;
    check(response.getVersion() == "HTTP/1.1", "默认 version 为 HTTP/1.1");
    check(response.getStatusCode() == 200
        && response.getStatusMessage() == "OK", "默认状态可直接序列化为 200 OK");
    check(response.getBody().empty(), "默认 body 为空");
    check(!response.shouldCloseConnection(), "默认保持连接");
    check(headerEquals(response, "content-length", "0"), "默认 Content-Length 为 0");
    check(headerEquals(response, "CONNECTION", "keep-alive"), "header 读取大小写不敏感");

    response.setHeader("content-type", "text/plain");
    response.setHeader("CONTENT-TYPE", "application/json");
    check(headerEquals(response, "Content-Type", "application/json"), "普通 header 大小写统一并可覆盖");
    check(countHeaderName(response, "content-type") == 1, "同名不同大小写只保存一份 header");

    response.setBody("abc");
    response.setHeader("content-length", "999");
    response.removeHeader("CONTENT-LENGTH");
    check(headerEquals(response, "Content-Length", "3"), "外部不能破坏 Content-Length");
    check(countHeaderName(response, "Content-Length") == 1, "Content-Length 不会出现大小写重复");

    response.setHeader("connection", "close");
    check(!response.shouldCloseConnection()
        && headerEquals(response, "Connection", "keep-alive"), "外部不能绕过连接策略修改 Connection");
    response.setCloseConnection(true);
    check(response.shouldCloseConnection()
        && headerEquals(response, "connection", "close"), "setCloseConnection 同步内部状态和 header");

    response.setHeader("Bad Header", "x");
    response.setHeader("X-Test", "ok\r\nInjected: yes");
    check(headerMissing(response, "Bad Header")
        && headerMissing(response, "X-Test"), "非法 header 名和值不会进入响应");

    Response bad;
    bad.createResponse(400, "bad", server.error_pages);
    check(bad.getStatusCode() == 400 && bad.shouldCloseConnection(), "400 自动强制关闭连接");
    check(headerEquals(bad, "Connection", "close"), "400 Connection header 同步");
    check(headerEquals(bad, "Content-Type", "text/html"), "默认错误页使用 HTML");

    Response custom;
    custom.createResponse(404, "old", server.error_pages);
    check(custom.getBody() == "<html>custom 404</html>", "自定义错误页覆盖旧 body");
    check(headerEquals(custom, "Content-Length", "23"), "自定义错误页长度正确");

    Response noContent;
    noContent.setHeader("Content-Type", "text/plain");
    noContent.setBody("must disappear");
    noContent.setStatus(204);
    noContent.setBody("still forbidden");
    check(noContent.getBody().empty(), "204 在状态切换前后都禁止 body");
    check(headerMissing(noContent, "Content-Length"), "204 不发送 Content-Length");
    check(headerMissing(noContent, "Content-Type"), "204 清除 body 相关 Content-Type");

    Response notModified;
    notModified.setBody("must disappear");
    notModified.setStatus(304);
    check(notModified.getBody().empty()
        && headerMissing(notModified, "Content-Length"), "304 不携带 body 或 Content-Length");

    Response binary;
    binary.appendBody("a\0b", 3);
    std::string raw = binary.responseToString();
    size_t split = raw.find("\r\n\r\n");
    check(split != std::string::npos, "序列化包含 header/body 空行");
    check(split != std::string::npos && raw.size() - split - 4 == 3,
          "序列化保留 NUL 后的二进制 body");
}

static void testGetAndRouting(const ServerConfig &server)
{
    beginGroup("GET、路由、autoindex 与 CGI 边界");
    Request request;
    check(parseRequest("GET", "/ping.html", "", "", server, request), "解析 GET 请求");
    Response response = buildResponse(request);
    check(response.getStatusCode() == 200 && response.getBody() == "pong\n", "GET 返回静态文件");
    check(headerEquals(response, "Content-Type", "text/html"), "HTML MIME type 正确");
    check(headerEquals(response, "Content-Length", "5"), "GET body 长度正确");

    check(parseRequest("GET", "/style.css", "", "", server, request), "解析 CSS 请求");
    response = buildResponse(request);
    check(headerEquals(response, "Content-Type", "text/css"), "CSS MIME type 正确");

    check(parseRequest("GET", "/image.png", "", "", server, request), "解析 PNG 请求");
    response = buildResponse(request);
    check(response.getBody().size() == 4 && headerEquals(response, "Content-Type", "image/png"),
          "PNG 二进制与 MIME type 正确");

    check(parseRequest("GET", "/hello%20world.txt?x=1", "", "", server, request),
          "解析 encoded path 与 query");
    response = buildResponse(request);
    check(response.getStatusCode() == 200 && response.getBody() == "space",
          "Response 使用 getPath 而不是原始 URI 查文件");

    check(parseRequest("GET", "/", "", "", server, request), "解析目录 GET");
    response = buildResponse(request);
    check(response.getStatusCode() == 200 && response.getBody() == "home\n",
          "目录按多 index 候选返回首页");

    check(parseRequest("GET", "/text-index/", "", "", server, request), "解析非 HTML index GET");
    response = buildResponse(request);
    check(response.getStatusCode() == 200 && response.getBody() == "plain index",
          "location index 文件读取成功");
    check(headerEquals(response, "Content-Type", "text/plain"), "index MIME 使用实际命中文件扩展名");

    check(parseRequest("GET", "/list/", "", "", server, request), "解析 autoindex GET");
    response = buildResponse(request);
    check(response.getStatusCode() == 200, "autoindex 返回 200");
    check(response.getBody().find("Index of /list/") != std::string::npos,
          "autoindex 显示 URL path");
    check(response.getBody().find(ROOT + "/list/") == std::string::npos,
          "autoindex 不暴露服务器磁盘路径");
    check(response.getBody().find("&lt;unsafe&gt;.txt") != std::string::npos,
          "autoindex 显示名经过 HTML 转义");
    check(response.getBody().find("href=\"a%3Fb.txt\"") != std::string::npos,
          "autoindex href 对问号做 path-segment 编码");
    check(response.getBody().find(">a?b.txt</a>") != std::string::npos,
          "autoindex 显示文本保持原文件名");

    check(parseRequest("GET", "/blocked.fifo", "", "", server, request), "解析 FIFO GET");
    response = buildResponse(request);
    check(response.getStatusCode() == 403, "GET 非普通文件不会进入阻塞读取");

    check(parseRequest("GET", "/alias/hello.txt", "", "", server, request), "解析 alias GET");
    response = buildResponse(request);
    check(response.getStatusCode() == 200 && response.getBody() == "alias works", "alias 路径正确");

    check(parseRequest("GET", "/old/item", "", "", server, request), "解析 redirect 请求");
    response = buildResponse(request);
    check(response.getStatusCode() == 301 && headerEquals(response, "Location", "/new/"),
          "redirect 状态和 Location 正确");
    check(!response.getBody().empty() && headerEquals(response, "Content-Type", "text/html"),
          "允许 body 的 redirect 生成说明页");

    check(parseRequest("GET", "/not-modified/item", "", "", server, request), "解析 304 配置请求");
    response = buildResponse(request);
    check(response.getStatusCode() == 304 && headerEquals(response, "Location", "/cached/"),
          "304 redirect 配置保留状态和 Location");
    check(response.getBody().empty() && headerMissing(response, "Content-Length")
        && headerMissing(response, "Content-Type"), "304 分支不生成非法响应 body");

    check(parseRequest("POST", "/readonly/file.txt", "Content-Length: 1\r\n", "x", server, request),
          "解析不允许 POST 的 location");
    response = buildResponse(request);
    check(response.getStatusCode() == 405 && headerEquals(response, "Allow", "GET"),
          "405 返回 Allow header");

    check(parseRequest("PATCH", "/ping.html", "", "", server, request), "解析未实现 method");
    response = buildResponse(request);
    check(response.getStatusCode() == 501, "合法但未实现 method 返回 501");

    check(parseRequest("GET", "/cgi/script.py?name=test", "", "", server, request), "解析 CGI 请求");
    response = buildResponse(request);
    check(response.getStatusCode() == 501, "CGI 未接入时返回 501");
    check(response.getBody().find("SECRET_CGI_SOURCE") == std::string::npos,
          "CGI 未接入时不泄露脚本源码");

    check(parseRequest("GET", "/missing.txt", "", "", server, request), "解析缺失资源请求");
    response = buildResponse(request);
    check(response.getStatusCode() == 404 && response.getBody() == "<html>custom 404</html>",
          "404 使用自定义错误页");

    check(parseRequest("GET", "/ping.html", "Connection: keep-alive, close\r\n", "", server, request),
          "解析 Connection token 列表");
    response = buildResponse(request);
    check(response.getStatusCode() == 200 && response.shouldCloseConnection(),
          "Connection 列表包含 close 时关闭连接");
}

static void testPost(const ServerConfig &server)
{
    beginGroup("POST 上传");
    Request request;
    Response response;

    check(parseRequest("POST", "/upload/new.txt",
        "Content-Length: 3\r\nContent-Type: text/plain\r\n", "abc", server, request),
        "解析 Content-Length POST");
    response = buildResponse(request);
    check(response.getStatusCode() == 200, "普通 POST 返回 200");
    check(readFile(ROOT + "/upload/new.txt") == "abc", "普通 POST 写入 body");

    check(parseRequest("POST", "/upload/chunk.txt",
        "Transfer-Encoding: chunked\r\nContent-Type: text/plain\r\n",
        "2\r\nhe\r\n3\r\nllo\r\n0\r\n\r\n", server, request),
        "解析 chunked POST");
    response = buildResponse(request);
    check(response.getStatusCode() == 200, "chunked POST 返回 200");
    check(readFile(ROOT + "/upload/chunk.txt") == "hello", "chunked body 已解码后写盘");

    check(parseRequest("POST", "/missing-parent/name.txt",
        "Content-Length: 1\r\nContent-Type: text/plain\r\n", "x", server, request),
        "解析 URI 父目录不存在的 POST");
    response = buildResponse(request);
    check(response.getStatusCode() == 200, "POST 不错误依赖 URI 父目录");
    check(readFile(ROOT + "/upload/name.txt") == "x", "POST 使用配置 upload_path 保存文件");

    check(parseRequest("POST", "/joined/child/joined.txt",
        "Content-Length: 4\r\nContent-Type: text/plain\r\n", "join", server, request),
        "解析无边界斜杠 upload_path POST");
    response = buildResponse(request);
    check(response.getStatusCode() == 200
        && readFile(ROOT + "/joined_upload/joined.txt") == "join",
        "root 与 upload_path 通过 joinPaths 正确拼接");

    check(parseRequest("POST", "/upload/a..b.txt",
        "Content-Length: 2\r\nContent-Type: text/plain\r\n", "ok", server, request),
        "解析文件名内部连续点 POST");
    response = buildResponse(request);
    check(response.getStatusCode() == 200
        && readFile(ROOT + "/upload/a..b.txt") == "ok",
        "普通文件名内部的连续点不会被误判为路径穿越");

    check(parseRequest("POST", "/upload/multipart.bin",
        "Content-Length: 3\r\nContent-Type: multipart/form-data; boundary=x\r\n",
        "abc", server, request), "解析 multipart POST");
    response = buildResponse(request);
    check(response.getStatusCode() == 415, "未实现 multipart 解析时明确返回 415");
    check(!exists(ROOT + "/upload/multipart.bin"), "multipart 原始 framing 不会被误写成文件");

    check(parseRequest("POST", "/missing-upload/file.txt",
        "Content-Length: 1\r\nContent-Type: text/plain\r\n", "x", server, request),
        "解析上传目录不存在 POST");
    response = buildResponse(request);
    check(response.getStatusCode() == 409, "真正 upload_path 不存在时返回 409");

    check(parseRequest("POST", "/upload/no-length.txt", "", "", server, request),
          "解析无 framing POST");
    response = buildResponse(request);
    check(response.getStatusCode() == 411, "无 Content-Length/TE 返回 411");

    check(parseRequest("POST", "/upload/bad-type.txt",
        "Content-Length: 1\r\nContent-Type: image/tiff\r\n", "x", server, request),
        "解析不支持 Content-Type POST");
    response = buildResponse(request);
    check(response.getStatusCode() == 415, "不支持 Content-Type 返回 415");

    check(parseRequest("POST", "/upload/same.txt", "Content-Length: 3\r\n", "new", server, request),
          "解析同名上传");
    response = buildResponse(request);
    check(response.getStatusCode() == 200, "同名上传成功");
    check(readFile(ROOT + "/upload/same.txt") == "old", "同名上传不覆盖旧文件");
    check(readFile(ROOT + "/upload/same_1.txt") == "new", "同名上传生成 _1 文件");

    check(parseRequest("POST", "/upload/", "Content-Length: 0\r\n", "", server, request),
          "解析无文件名 POST");
    response = buildResponse(request);
    check(response.getStatusCode() == 400, "POST URI 无文件名返回 400");
}

static void testDelete(const ServerConfig &server)
{
    beginGroup("DELETE");
    Request request;
    Response response;

    check(parseRequest("DELETE", "/delete_me.txt", "", "", server, request), "解析 DELETE");
    response = buildResponse(request);
    check(response.getStatusCode() == 204 && !exists(ROOT + "/delete_me.txt"),
          "DELETE 文件成功并返回 204");
    check(response.getBody().empty() && headerMissing(response, "Content-Length"),
          "204 DELETE 响应无 body 且不发送 Content-Length");

    check(parseRequest("DELETE", "/query_delete.txt?force=1", "", "", server, request),
          "解析带 query DELETE");
    response = buildResponse(request);
    check(response.getStatusCode() == 204 && !exists(ROOT + "/query_delete.txt"),
          "DELETE 使用 normalized path 忽略 query");

    check(parseRequest("DELETE", "/missing-delete.txt", "", "", server, request),
          "解析缺失 DELETE");
    response = buildResponse(request);
    check(response.getStatusCode() == 404, "DELETE 缺失文件返回 404");

    check(parseRequest("DELETE", "/delete_dir", "", "", server, request),
          "解析目录 DELETE");
    response = buildResponse(request);
    check(response.getStatusCode() == 403, "DELETE 目录返回 403");
}

static void testMissingConfig()
{
    beginGroup("配置防御");
    ServerConfig noRoot;
    Request request;
    check(parseRequest("GET", "/ping.html", "", "", noRoot, request), "解析无 root 配置请求");
    Response response = buildResponse(request);
    check(response.getStatusCode() == 500, "没有有效 root 返回 500 而不是崩溃");
}

int main()
{
    ServerConfig server = makeServer();
    prepareFiles(server);
    testResponseClass(server);
    testGetAndRouting(server);
    testPost(server);
    testDelete(server);
    testMissingConfig();

    std::cout << "\n========== Response class 测试汇总 ==========" << std::endl;
    std::cout << "总断言：" << g_total << std::endl;
    std::cout << "通过：" << (g_total - g_failed) << std::endl;
    std::cout << "失败：" << g_failed << std::endl;
    return g_failed == 0 ? 0 : 1;
}
