#include "Response.hpp"
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

/*
函数：beginGroup
用途：打印 Response 测试组标题，使对象基础、GET、POST、DELETE、路由等输出容易阅读。
参数：name 是测试组名称。
返回值：无。
实现逻辑：只输出分隔标题，不改变测试计数。
*/
static void beginGroup(const std::string& name)
{
    std::cout << "\n========== " << name << " ==========" << std::endl;
}

/*
函数：check
用途：记录并打印一条 Response 测试断言。
参数：condition 是验证条件；name 是断言说明。
返回值：无。
实现逻辑：增加总数；条件成立打印 PASS，否则打印 FAIL 并增加失败数。
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
函数：writeFile
用途：在测试网站目录中创建二进制安全的文件。
参数：path 是完整路径；content 是要写入的字节串。
返回值：打开、写入和关闭都成功返回 true，否则返回 false。
实现逻辑：以 binary 模式打开，使用 write(content.data(), content.size()) 写入，关闭后检查流状态。
*/
static bool writeFile(const std::string& path, const std::string& content)
{
    std::ofstream out(path.c_str(), std::ios::binary);
    if (!out)
        return false;
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    out.close();
    return static_cast<bool>(out);
}

/*
函数：readFile
用途：读取测试产生的文件，用来验证 GET body 或 POST 写盘副作用。
参数：path 是完整文件路径。
返回值：返回文件全部字节；打不开时返回空字符串。
实现逻辑：以 binary 模式打开，把 rdbuf() 写入 ostringstream 后返回字符串。
*/
static std::string readFile(const std::string& path)
{
    std::ifstream in(path.c_str(), std::ios::binary);
    if (!in)
        return "";
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

/*
函数：exists
用途：判断测试路径是否存在。
参数：path 是待检查路径。
返回值：stat 成功返回 true，失败返回 false。
实现逻辑：只调用 stat，不区分文件和目录。
*/
static bool exists(const std::string& path)
{
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

/*
函数：makeRequest
用途：手动构造一个可以直接交给 buildResponse() 的最小合法 Request。
参数：method/uri 是测试请求；server 是它所属的 ServerConfig。
返回值：返回 HTTP/1.1、带 Host、默认 keep-alive 的 Request。
实现逻辑：逐字段填充，不经过 RequestParser，因此可以单独测试 Response 模块。
*/
static Request makeRequest(const std::string& method, const std::string& uri,
                           const ServerConfig& server)
{
    Request request;
    request.method = method;
    request.uri = uri;
    request.version = "HTTP/1.1";
    request.headers["host"] = "localhost";
    request.body.clear();
    request.config = &server;
    request.use_location = false;
    request.closeConnection = false;
    return request;
}

/*
函数：makeServer
用途：构造 mandatory Response 测试需要的 server、location、redirect、autoindex、alias 和未实现 CGI 配置。
参数：无。
返回值：返回根目录为 tests/tmp_response_www 的 ServerConfig。
实现逻辑：设置多 index、上传目录和三种方法；添加只读、redirect、autoindex、alias location；再添加只用于验证“CGI 尚未实现时返回 501”的 .py location。
*/
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
    server.autoindex = false;
    server.has_autoindex = true;

    LocationConfig readonly;
    readonly.path = "/readonly/";
    readonly.allow_methods.insert("GET");
    server.locations.push_back(readonly);

    LocationConfig redirect;
    redirect.path = "/old/";
    redirect.redirect_status = 301;
    redirect.redirect_url = "/new/";
    server.locations.push_back(redirect);

    LocationConfig listing;
    listing.path = "/list/";
    listing.has_autoindex = true;
    listing.autoindex = true;
    server.locations.push_back(listing);

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

/*
函数：prepareFiles
用途：创建 mandatory Response 测试所需的静态文件、错误页、目录、alias 目标和 CGI 占位脚本。
参数：server 会被写入 404 自定义错误页路径。
返回值：无；每个文件创建结果通过 check() 记录。
实现逻辑：写 index、静态文件、各种 MIME 文件、待删除文件、自定义错误页、autoindex 特殊文件名；额外写入含敏感标记的 .py 文件，用来确认 CGI 尚未实现时不会泄露脚本源码。
*/
static void prepareFiles(ServerConfig& server)
{
    beginGroup("准备测试文件");
    check(writeFile(ROOT + "/index.html", "home\n"), "创建第二候选 index.html");
    check(writeFile(ROOT + "/ping.html", "pong\n"), "创建 ping.html");
    check(writeFile(ROOT + "/style.css", "body{}"), "创建 CSS 文件");
    check(writeFile(ROOT + "/app.js", "var x=1;"), "创建 JavaScript 文件");
    check(writeFile(ROOT + "/data.json", "{}"), "创建 JSON 文件");
    check(writeFile(ROOT + "/note.txt", "text"), "创建文本文件");
    check(writeFile(ROOT + "/image.png", std::string("\x89PNG", 4)), "创建 PNG 文件");
    check(writeFile(ROOT + "/photo.jpg", "jpg"), "创建 JPG 文件");
    check(writeFile(ROOT + "/photo.jpeg", "jpeg"), "创建 JPEG 文件");
    check(writeFile(ROOT + "/anim.gif", "gif"), "创建 GIF 文件");
    check(writeFile(ROOT + "/icon.svg", "<svg/>") , "创建 SVG 文件");
    check(writeFile(ROOT + "/doc.pdf", "%PDF"), "创建 PDF 文件");
    check(writeFile(ROOT + "/blob.bin", "bin"), "创建未知扩展文件");
    check(writeFile(ROOT + "/delete_me.txt", "delete me"), "创建 DELETE 目标文件");
    check(writeFile(ROOT + "/query_delete.txt", "delete by query"), "创建带 query DELETE 目标");
    check(writeFile(ROOT + "/upload/same.txt", "old"), "创建同名上传文件用于唯一命名测试");
    check(writeFile(ROOT + "/list/<unsafe>.txt", "x"), "创建 autoindex 需要转义的文件名");
    check(writeFile(ROOT + "/alias_target/hello.txt", "alias works"), "创建 alias 目标文件");
    check(writeFile(ROOT + "/cgi/script.py", "SECRET_CGI_SOURCE\n"), "创建 CGI 占位脚本");
    check(writeFile(ROOT + "/custom404.html", "<html>custom 404</html>"), "创建自定义 404 页面");
    server.error_pages[404] = ROOT + "/custom404.html";
}

/*
函数：testResponseObjectBasics
用途：直接测试 Response 构造、createResponse、错误页、Connection 和 responseToString，不经过路由。
参数：server 提供自定义错误页配置。
返回值：无。
实现逻辑：验证 200 文本、204 无 body、400 强制关闭、404 自定义 HTML，以及包含 NUL 的 body 序列化。
*/
static void testResponseObjectBasics(const ServerConfig& server)
{
    beginGroup("Response 对象基础");

    Response response;
    check(response.version == "HTTP/1.1" && response.statusCode == 0 && !response.closingConnection,
          "Response 默认构造状态正确");

    response.createResponse(200, "hello", server.error_pages);
    check(response.statusCode == 200 && response.statusMessage == "OK", "createResponse 生成 200 状态");
    check(response.headers["Content-Type"] == "text/plain", "简单文本响应 Content-Type 正确");
    check(response.headers["Content-Length"] == "5", "简单文本响应 Content-Length 正确");
    check(response.headers["Connection"] == "keep-alive", "普通 200 默认 keep-alive");

    Response noContent;
    noContent.createResponse(204, "should disappear", server.error_pages);
    check(noContent.body.empty(), "204 会清空 body");
    check(noContent.headers["Content-Length"] == "0", "204 Content-Length 为 0");
    check(noContent.headers.find("Content-Type") == noContent.headers.end(), "204 不发送 Content-Type");

    Response bad;
    bad.createResponse(400, "bad", server.error_pages);
    check(bad.closingConnection && bad.headers["Connection"] == "close", "400 强制关闭连接");
    check(bad.headers["Content-Type"] == "text/html", "默认错误页覆盖为 HTML");

    Response custom;
    custom.createResponse(404, "old text", server.error_pages);
    check(custom.body == "<html>custom 404</html>", "自定义错误页覆盖旧 bodyText");
    check(custom.headers["Content-Type"] == "text/html", "自定义错误页 Content-Type 为 text/html");
    check(custom.headers["Content-Length"] == toString<size_t>(custom.body.size()), "自定义错误页长度重新计算");

    Response binary;
    binary.statusCode = 200;
    binary.statusMessage = "OK";
    binary.body.assign("a\0b", 3);
    binary.headers["Content-Length"] = "3";
    std::string raw = binary.responseToString();
    size_t split = raw.find("\r\n\r\n");
    check(split != std::string::npos, "responseToString 包含 header/body 空行");
    check(split != std::string::npos && raw.size() - (split + 4) == 3, "responseToString 保留二进制 body 长度");
    check(split != std::string::npos && raw[split + 5] == '\0', "responseToString 不在 NUL 处截断");
}

/*
函数：testStaticGetAndMime
用途：测试普通静态 GET、query 去除以及常见扩展名 MIME 映射。
参数：server 是完整测试配置。
返回值：无。
实现逻辑：先验证 ping.html?x=1，再用路径/期望 MIME 表逐项调用 buildResponse()。
*/
static void testStaticGetAndMime(const ServerConfig& server)
{
    beginGroup("静态 GET 与 MIME");

    Request request = makeRequest("GET", "/ping.html?x=1", server);
    Response response = buildResponse(request);
    check(response.statusCode == 200 && response.body == "pong\n", "query string 不进入真实文件路径");
    check(response.headers["Content-Length"] == toString<size_t>(response.body.size()), "GET Content-Length 等于实际 body");

    const char* paths[] = {
        "/ping.html", "/style.css", "/app.js", "/data.json", "/note.txt",
        "/image.png", "/photo.jpg", "/photo.jpeg", "/anim.gif", "/icon.svg",
        "/doc.pdf", "/blob.bin"
    };
    const char* types[] = {
        "text/html", "text/css", "application/javascript", "application/json", "text/plain",
        "image/png", "image/jpeg", "image/jpeg", "image/gif", "image/svg+xml",
        "application/pdf", "application/octet-stream"
    };
    for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); ++i) {
        request = makeRequest("GET", paths[i], server);
        response = buildResponse(request);
        check(response.statusCode == 200, std::string("静态 GET 成功：") + paths[i]);
        check(response.headers["Content-Type"] == types[i], std::string("MIME 正确：") + paths[i]);
    }

    request = makeRequest("GET", "/missing.html", server);
    response = buildResponse(request);
    check(response.statusCode == 404, "不存在文件返回 404");
    check(response.body == "<html>custom 404</html>", "404 使用配置的自定义错误页");
}

/*
函数：testDirectoryIndexAndAutoindex
用途：验证多 index fallback、autoindex off/on 和目录项 HTML 转义。
参数：server 是完整测试配置。
返回值：无。
实现逻辑：根目录第一个 index 不存在但第二个存在；/noindex/ 禁止列表；/list/ 允许列表并检查特殊文件名被转义。
*/
static void testDirectoryIndexAndAutoindex(const ServerConfig& server)
{
    beginGroup("目录 index 与 autoindex");

    Request request = makeRequest("GET", "/", server);
    Response response = buildResponse(request);
    check(response.statusCode == 200 && response.body == "home\n", "多个 index 候选按顺序 fallback");

    request = makeRequest("GET", "/noindex/", server);
    response = buildResponse(request);
    check(response.statusCode == 403, "目录无 index 且 autoindex off 返回 403");

    request = makeRequest("GET", "/list/", server);
    response = buildResponse(request);
    check(response.statusCode == 200, "location autoindex on 返回 200");
    check(response.headers["Content-Type"] == "text/html", "autoindex Content-Type 为 text/html");
    check(response.body.find("&lt;unsafe&gt;.txt") != std::string::npos, "autoindex 对文件名做 HTML 转义");
}

/*
函数：testRoutingAliasRedirectAndMethods
用途：验证 alias、重定向、未知方法 501、location 方法限制 405 和 Allow header。
参数：server 是完整测试配置。
返回值：无。
实现逻辑：alias 请求读取另一目录；redirect 在目标不存在时仍返回 301；PUT 返回 501；readonly POST 返回 405。
*/
static void testRoutingAliasRedirectAndMethods(const ServerConfig& server)
{
    beginGroup("路由、alias、redirect 与方法");

    Request request = makeRequest("GET", "/alias/hello.txt", server);
    Response response = buildResponse(request);
    check(response.statusCode == 200 && response.body == "alias works", "alias 正确替换 location 前缀");

    request = makeRequest("GET", "/old/not-on-disk", server);
    response = buildResponse(request);
    check(response.statusCode == 301, "redirect 不依赖本地文件存在");
    check(response.statusMessage == "Moved Permanently", "301 状态短语正确");
    check(response.headers["Location"] == "/new/", "redirect Location 正确");
    check(response.headers["Connection"] == "keep-alive" && !response.closingConnection,
          "redirect 的 header 与 closingConnection 一致");

    request = makeRequest("PUT", "/missing.html", server);
    response = buildResponse(request);
    check(response.statusCode == 501, "未实现方法在文件检查前返回 501");

    request = makeRequest("POST", "/readonly/new.txt", server);
    request.headers["content-length"] = "1";
    request.body = "x";
    response = buildResponse(request);
    check(response.statusCode == 405, "已实现但 location 不允许的方法返回 405");
    check(response.headers["Allow"] == "GET", "405 Allow header 正确");

    request = makeRequest("GET", "/ping.html", server);
    request.version = "HTTP/1.0";
    response = buildResponse(request);
    check(response.statusCode == 505, "Response 独立防线拒绝 HTTP/1.0");
}

/*
函数：testPostUploads
用途：测试 Content-Length/chunked 上传、二进制写入、长度错误、类型错误、唯一命名和空文件名。
参数：server 是完整测试配置。
返回值：无。
实现逻辑：手动构造 Request.body 和相关 headers，调用 buildResponse() 后同时检查 Response 与磁盘文件。
*/
static void testPostUploads(const ServerConfig& server)
{
    beginGroup("POST 上传");

    Request request = makeRequest("POST", "/upload/new.txt", server);
    request.headers["content-length"] = "5";
    request.headers["content-type"] = "text/plain; charset=utf-8";
    request.body = "hello";
    Response response = buildResponse(request);
    check(response.statusCode == 200, "Content-Length POST 返回 200");
    check(readFile(ROOT + "/upload/new.txt") == "hello", "Content-Length POST 写盘内容正确");

    request = makeRequest("POST", "/upload/chunk.txt", server);
    request.headers["transfer-encoding"] = "chunked";
    request.headers["content-type"] = "application/octet-stream";
    request.body.assign("a\0b", 3);
    response = buildResponse(request);
    check(response.statusCode == 200, "已解码 chunked POST 返回 200");
    std::string stored = readFile(ROOT + "/upload/chunk.txt");
    check(stored.size() == 3 && stored[1] == '\0', "chunked 二进制 body 写盘不截断");

    request = makeRequest("POST", "/upload/no-length.txt", server);
    request.body = "x";
    response = buildResponse(request);
    check(response.statusCode == 411, "没有 Content-Length/TE 返回 411");

    request = makeRequest("POST", "/upload/bad-type.txt", server);
    request.headers["content-length"] = "1";
    request.headers["content-type"] = "image/tiff";
    request.body = "x";
    response = buildResponse(request);
    check(response.statusCode == 415, "不支持的 Content-Type 返回 415");

    request = makeRequest("POST", "/upload/mismatch.txt", server);
    request.headers["content-length"] = "5";
    request.body = "abc";
    response = buildResponse(request);
    check(response.statusCode == 400, "header 长度与 body.size 不一致返回 400");

    request = makeRequest("POST", "/upload/same.txt", server);
    request.headers["content-length"] = "3";
    request.body = "new";
    response = buildResponse(request);
    check(response.statusCode == 200, "同名上传仍成功");
    check(readFile(ROOT + "/upload/same.txt") == "old", "同名上传不覆盖旧文件");
    check(readFile(ROOT + "/upload/same_1.txt") == "new", "同名上传生成 _1 文件");

    request = makeRequest("POST", "/upload/", server);
    request.headers["content-length"] = "0";
    response = buildResponse(request);
    check(response.statusCode == 400, "POST URI 没有文件名返回 400");
}

/*
函数：testDelete
用途：验证 DELETE 成功、缺失文件、目录拒绝、query 去除和 204 响应格式。
参数：server 是完整测试配置。
返回值：无。
实现逻辑：分别删除普通文件、带 query 的文件，尝试删除不存在文件和目录；最后检查 204 body/Content-Length/序列化状态行。
*/
static void testDelete(const ServerConfig& server)
{
    beginGroup("DELETE");

    Request request = makeRequest("DELETE", "/delete_me.txt", server);
    Response response = buildResponse(request);
    check(response.statusCode == 204, "DELETE 普通文件返回 204");
    check(!exists(ROOT + "/delete_me.txt"), "DELETE 后文件不存在");
    check(response.body.empty() && response.headers["Content-Length"] == "0", "204 没有 body 且长度为 0");
    check(response.responseToString().find("HTTP/1.1 204 No Content\r\n") == 0, "204 状态行序列化正确");

    request = makeRequest("DELETE", "/query_delete.txt?force=1", server);
    response = buildResponse(request);
    check(response.statusCode == 204 && !exists(ROOT + "/query_delete.txt"), "DELETE 路径会去掉 query string");

    request = makeRequest("DELETE", "/missing-delete.txt", server);
    response = buildResponse(request);
    check(response.statusCode == 404, "DELETE 不存在文件返回 404");

    request = makeRequest("DELETE", "/delete_dir", server);
    response = buildResponse(request);
    check(response.statusCode == 403, "DELETE 目录返回 403");
}

/*
函数：testUnavailableSessionAndCgi
用途：验证当前 mandatory 阶段没有 SessionManager 和 CGI 执行模块时，Response 仍有明确且安全的行为。
参数：server 是完整测试配置，其中 /cgi/ location 配置了 .py 扩展名，但没有链接任何 CGI 源文件。
返回值：无。
实现逻辑：
    1. 请求不存在的 /session，确认它只按普通静态路径处理并返回 404，不生成 Set-Cookie。
    2. 请求配置为 CGI 的真实 .py 文件，确认返回 501 Not Implemented。
    3. 确认 501 response 不包含脚本源码标记，避免 CGI 未完成时把源码当静态文件发送。
    4. 检查 CGI fallback 的 Content-Length 和 Connection 状态保持一致。
意义：测试程序本身不编译 SessionManager.cpp 或任何 CGI 源文件；如果 Response 仍残留这些依赖，编译或链接会直接失败。
*/
static void testUnavailableSessionAndCgi(const ServerConfig& server)
{
    beginGroup("未接入 Session / CGI 时的安全边界");

    Request request = makeRequest("GET", "/session", server);
    Response response = buildResponse(request);
    check(response.statusCode == 404, "没有 SessionManager 时 /session 按普通路径返回 404");
    check(response.headers.find("Set-Cookie") == response.headers.end(),
          "没有 SessionManager 时不会凭空生成 Set-Cookie");

    request = makeRequest("GET", "/cgi/script.py?name=test", server);
    response = buildResponse(request);
    check(response.statusCode == 501, "配置为 CGI 但执行模块未实现时返回 501");
    check(response.statusMessage == "Not Implemented", "CGI fallback 的 501 状态短语正确");
    check(response.body.find("SECRET_CGI_SOURCE") == std::string::npos,
          "CGI 未实现时不会把脚本源码作为静态文件泄露");
    check(response.headers["Content-Length"] == toString<size_t>(response.body.size()),
          "CGI fallback 的 Content-Length 等于实际 body 大小");
    check(response.headers["Connection"] == "keep-alive" && !response.closingConnection,
          "CGI fallback 的 Connection header 与对象状态一致");
}

/*
函数：testPathSafetyAndConnection
用途：测试直接构造 Request 时 Response 仍会阻止路径穿越，并保持 Connection 状态一致。
参数：server 是完整测试配置。
返回值：无。
实现逻辑：传入 /../secret 检查 403；再把 request.closeConnection 设为 true，检查响应对象和 header 都为 close。
*/
static void testPathSafetyAndConnection(const ServerConfig& server)
{
    beginGroup("路径安全与 Connection");

    Request request = makeRequest("GET", "/../secret", server);
    Response response = buildResponse(request);
    check(response.statusCode == 403, "Response 路径层再次拒绝路径穿越");

    request = makeRequest("GET", "/ping.html", server);
    request.closeConnection = true;
    response = buildResponse(request);
    check(response.statusCode == 200, "Connection: close 不影响成功 GET");
    check(response.closingConnection && response.headers["Connection"] == "close",
          "Response 对象与 Connection header 同步为 close");

    ServerConfig noRoot;
    noRoot.has_root = false;
    request = makeRequest("GET", "/ping.html", noRoot);
    response = buildResponse(request);
    check(response.statusCode == 500, "没有有效 root 的配置返回 500，而不是崩溃");
}

/*
函数：main
用途：按 Response 数据对象、GET、目录、路由、POST、DELETE、未实现模块边界和路径安全的顺序运行全部独立模块测试。
参数：无。
返回值：全部断言通过返回 0；存在失败返回 1。
实现逻辑：创建配置和测试文件，依次调用所有测试组，最后打印总数、通过数和失败数。
*/
int main()
{
    ServerConfig server = makeServer();
    prepareFiles(server);

    testResponseObjectBasics(server);
    testStaticGetAndMime(server);
    testDirectoryIndexAndAutoindex(server);
    testRoutingAliasRedirectAndMethods(server);
    testPostUploads(server);
    testDelete(server);
    testUnavailableSessionAndCgi(server);
    testPathSafetyAndConnection(server);

    std::cout << "\n========== Response 测试汇总 ==========" << std::endl;
    std::cout << "总断言：" << g_total << std::endl;
    std::cout << "通过：" << (g_total - g_failed) << std::endl;
    std::cout << "失败：" << g_failed << std::endl;
    return g_failed == 0 ? 0 : 1;
}
