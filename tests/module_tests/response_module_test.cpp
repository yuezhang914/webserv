/*
文件：tests/module_tests/response_module_test.cpp
用途：测试 Response 模块、Session 接入层及其直接依赖的真实 Config、RequestParser、SessionStore 和文件系统行为。
测试边界：不包含 CgiHandler、CgiFds、pipe、fork、poll、execve 或 waitpid；CGI 部分只验证 Response 的分发结果，以及原始 CGI stdout 到最终 Response 的转换。
测试原则：不使用 tests/test_support；配置由真实 Config 读取，请求由真实 RequestParser 构造，Response 使用项目正式源码。
注释规则：每个辅助函数和测试函数都在函数头说明用途、参数来源、变量含义和逐步实现逻辑。
*/
#include "Config.hpp"
#include "RequestParser.hpp"
#include "Response.hpp"
#include "SessionStore.hpp"

#include <fstream>
#include <iostream>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

static int g_total = 0;
static int g_failed = 0;
static const std::string ROOT = "tests/tmp_response_www";
static const std::string CONFIG_PATH =
    "tests/module_tests/response_module_test.conf";

/*
函数：beginGroup
用途：在测试输出中打印一个功能组标题。
参数来源：name 由各 testXxx() 函数写死传入，例如 "CGI 接口接入"。
变量说明：无局部变量。
实现逻辑：输出空行、分隔符和组名，不改变断言计数。
*/
static void beginGroup(const std::string &name)
{
    std::cout << "\n========== " << name << " ==========" << std::endl;
}

/*
函数：check
用途：记录并打印一条断言的通过或失败状态。
参数来源：
    - condition：各测试刚计算出的布尔结果。
    - name：对该结果的中文说明。
变量说明：使用全局 g_total/g_failed 保存总数和失败数。
实现逻辑：
    1. 总断言数加一。
    2. condition 为 true 输出 PASS。
    3. condition 为 false 输出 FAIL 并增加失败数。
*/
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

/*
函数：writeFile
用途：以二进制方式创建或覆盖测试文件和 CGI 脚本。
参数来源：path/content 由 prepareFixtureFiles() 或 writeConfigFile() 传入。
变量说明：output 是绑定 path 的二进制 ofstream。
实现逻辑：
    1. 打开文件，失败返回 false。
    2. 用 write(data,size) 写入全部字节，允许 NUL。
    3. 关闭文件并返回流状态。
*/
static bool writeFile(const std::string &path,
                      const std::string &content)
{
    std::ofstream output(path.c_str(), std::ios::binary);
    if (!output)
        return false;
    output.write(content.data(),
        static_cast<std::streamsize>(content.size()));
    output.close();
    return static_cast<bool>(output);
}

/*
函数：readFile
用途：读取 POST/GET 测试产生的完整二进制文件内容。
参数来源：path 由测试根据 ROOT 和预期文件名拼出。
变量说明：input 是二进制 ifstream；output 累积 rdbuf() 内容。
实现逻辑：打开失败返回空字符串；成功则读到 EOF 并返回全部字节。
*/
static std::string readFile(const std::string &path)
{
    std::ifstream input(path.c_str(), std::ios::binary);
    if (!input)
        return "";
    std::ostringstream output;
    output << input.rdbuf();
    return output.str();
}

/*
函数：pathExists
用途：判断测试目标文件或目录当前是否存在。
参数来源：path 由 DELETE、POST 或准备阶段传入。
变量说明：info 接收 stat() 结果。
实现逻辑：stat 成功返回 true，任何失败返回 false。
*/
static bool pathExists(const std::string &path)
{
    struct stat info;
    return stat(path.c_str(), &info) == 0;
}

/*
函数：lowerAscii
用途：把测试比较用的 header name 转成 ASCII 小写。
参数来源：value 来自 countHeaderName() 的目标名称和 Response header map key。
变量说明：result 是可修改副本；i 是字符下标。
实现逻辑：遍历并只转换 A-Z，其他字符保持原样。
*/
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

/*
函数：parseRequest
用途：用真实 RequestParser 从 HTTP wire text 构造只读 Request，避免测试直接写 Request 私有字段。
参数来源：
    - method/target/extraHeaders/wireBody：各测试描述的请求内容。
    - server：由真实 Config 解析出的 ServerConfig。
    - request：调用方提供的输出对象。
变量说明：raw 是完整 HTTP/1.1 请求；consumed 是 parser 输出的首个请求长度；status 是解析状态。
实现逻辑：
    1. 拼请求行、必需 Host、额外 headers、空行和 body。
    2. 调用 RequestParser::parseBuffer()。
    3. 只有状态 REQUEST_OK 且 consumed 等于 raw 总长度才返回 true。
*/
static bool parseRequest(const std::string &method,
                         const std::string &target,
                         const std::string &extraHeaders,
                         const std::string &wireBody,
                         const ServerConfig &server,
                         Request &request)
{
    std::string raw = method + " " + target + " HTTP/1.1\r\n";
    raw += "Host: localhost\r\n";
    raw += extraHeaders;
    raw += "\r\n";
    raw += wireBody;
    size_t consumed = 0;
    int status = RequestParser::parseBuffer(
        raw, request, &server, consumed);
    return status == REQUEST_OK && consumed == raw.size();
}

/*
函数：headerEquals
用途：大小写不敏感读取 Response header，并比较预期值。
参数来源：response 是被测结果；name/expected 由具体断言提供。
变量说明：value 接收 getHeader() 输出。
实现逻辑：header 存在且 value 完全等于 expected 时返回 true。
*/
static bool headerEquals(const Response &response,
                         const std::string &name,
                         const std::string &expected)
{
    std::string value;
    return response.getHeader(name, value) && value == expected;
}

/*
函数：headerMissing
用途：断言某个 Response header 不存在。
参数来源：response/name 由无 body 状态或封装测试传入。
变量说明：value 是 getHeader() 的临时输出。
实现逻辑：getHeader() 返回 false 时返回 true。
*/
static bool headerMissing(const Response &response,
                          const std::string &name)
{
    std::string value;
    return !response.getHeader(name, value);
}

/*
函数：countHeaderName
用途：验证同名不同大小写 header 在 Response map 中只有一份。
参数来源：response 来自被测结果；name 是要统计的逻辑 header 名。
变量说明：count 为匹配数量；expected 为小写目标；it 遍历 getHeaders()。
实现逻辑：把每个 map key 小写后比较，累计并返回匹配数。
*/
static size_t countHeaderName(const Response &response,
                              const std::string &name)
{
    size_t count = 0;
    std::string expected = lowerAscii(name);
    Response::HeaderMap::const_iterator it =
        response.getHeaders().begin();
    while (it != response.getHeaders().end())
    {
        if (lowerAscii(it->first) == expected)
            ++count;
        ++it;
    }
    return count;
}

/*
函数：extractSessionIdFromSetCookie
用途：从 Response 的 Set-Cookie value 中提取 WEBSERV_SESSION 的值，供后续请求模拟浏览器回传。
参数来源：response 是 buildResponse(request, store) 返回结果；sessionId 是调用方提供的输出变量。
变量说明：headerValue 保存完整 Set-Cookie；prefix 是固定 Cookie pair 前缀；end 是分号位置。
实现逻辑：读取 Set-Cookie，确认以固定前缀开头，截取到第一个分号；空值和格式错误返回 false。
*/
static bool extractSessionIdFromSetCookie(const Response &response,
                                          std::string &sessionId)
{
    sessionId.clear();
    std::string headerValue;
    if (!response.getHeader("Set-Cookie", headerValue))
        return false;
    const std::string prefix = "WEBSERV_SESSION=";
    if (headerValue.compare(0, prefix.size(), prefix) != 0)
        return false;
    size_t end = headerValue.find(';', prefix.size());
    if (end == std::string::npos)
        end = headerValue.size();
    sessionId = headerValue.substr(prefix.size(), end - prefix.size());
    return !sessionId.empty();
}

/*
函数：makeSessionCookieHeader
用途：生成 parseRequest() 可直接拼入请求的 Cookie header，模拟浏览器保存并回传 Session ID。
参数来源：sessionId 由 extractSessionIdFromSetCookie() 从上一响应取得。
变量说明：无局部变量。
实现逻辑：在 Session Cookie 前加入一个无关 theme Cookie，验证解析器能从多个 Cookie 中找到目标值。
*/
static std::string makeSessionCookieHeader(const std::string &sessionId)
{
    return "Cookie: theme=dark; WEBSERV_SESSION=" + sessionId + "\r\n";
}

/*
函数：buildReadyResponse
用途：使用最终唯一接口构造一个不需要跨请求 Session 状态的普通响应。
参数来源：request 由 parseRequest() 使用真实 RequestParser 构造。
变量说明：store 是本辅助函数临时创建的 SessionStore；普通静态、上传、删除和 CGI 路径不会依赖其长期状态。
实现逻辑：
    1. 创建仅供本次普通请求使用的 SessionStore。
    2. 调用 buildResponse(request, store)，确保所有测试都经过最终公开接口。
    3. 返回得到的 Response 副本。
测试边界：需要验证 Session 跨请求持久化的场景不会使用本函数，而是在测试组内共享同一个 store。
*/
static Response buildReadyResponse(const Request &request)
{
    SessionStore store;
    return buildResponse(request, store);
}

/*
函数：writeConfigFile
用途：创建本测试使用的真实 webserv 配置，覆盖 server、location、alias、redirect、upload 和 CGI 路由配置。
参数来源：无参数；使用 ROOT 与 CONFIG_PATH 全局常量。
变量说明：config 是按项目 Config parser 语法拼出的配置文本。
实现逻辑：
    1. 定义 server 级 root/index/error_page/body/method/upload/autoindex。
    2. 定义只读、重定向、autoindex、文本 index、upload、alias 与 CGI locations。
    3. 调用 writeFile() 写入 conf，返回是否成功。
*/
static bool writeConfigFile()
{
    std::string config;
    config += "server {\n";
    config += "listen 18080;\n";
    config += "server_name localhost;\n";
    config += "root " + ROOT + ";\n";
    config += "error_page 404 " + ROOT + "/custom404.html;\n";
    config += "max_body_size 1M;\n";
    config += "index missing-index.html index.html;\n";
    config += "allow_methods GET POST DELETE;\n";
    config += "upload_path /upload/;\n";
    config += "autoindex off;\n";
    config += "location /readonly/ { allow_methods GET; }\n";
    config += "location /old/ { return 301 /new/; }\n";
    config += "location /not-modified/ { return 304 /cached/; }\n";
    config += "location /list/ { autoindex on; }\n";
    config += "location /text-index/ { index readme.txt; }\n";
    config += "location /joined/ { upload_path joined_upload; }\n";
    config += "location /missing-upload/ { upload_path does-not-exist; }\n";
    config += "location /alias/ { alias " + ROOT
        + "/alias_target/; allow_methods GET; }\n";
    config += "location /cgi/ { allow_methods GET POST; "
        "cgi_extension .sh /bin/sh; }\n";
    config += "}\n";
    return writeFile(CONFIG_PATH, config);
}

/*
函数：prepareFixtureFiles
用途：建立 Response 测试需要的静态文件、上传文件、错误页和 CGI 路由占位文件。
参数来源：无参数；目录由运行脚本预先创建，所有路径基于 ROOT 全局常量。
变量说明：
    - executableScript：只用于让 buildResponse() 验证 CGI 路径是可执行普通文件；本测试不会执行它。
实现逻辑：
    1. 调用 writeConfigFile() 写入真实配置。
    2. 创建 GET、index、alias、autoindex、POST、DELETE 和自定义错误页所需文件。
    3. 创建可执行 CGI 占位脚本，供 Response 验证并返回 RESPONSE_BUILD_CGI。
    4. 创建不可执行脚本，验证 Response 在 CGI 启动前返回 403。
    5. 创建 CGI location 内的普通静态文件和 FIFO，验证扩展名分发及特殊文件保护。
*/
static void prepareFixtureFiles()
{
    beginGroup("准备真实 Config 与文件夹具");
    check(writeConfigFile(), "写入真实 Config 测试文件");
    check(writeFile(ROOT + "/index.html", "home\n"),
        "创建 index.html");
    check(writeFile(ROOT + "/ping.html", "pong\n"),
        "创建静态文件");
    check(writeFile(ROOT + "/hello world.txt", "space"),
        "创建 percent-decoding 目标文件");
    check(writeFile(ROOT + "/style.css", "body{}"),
        "创建 CSS 文件");
    check(writeFile(ROOT + "/image.png", std::string("\x89PNG", 4)),
        "创建二进制 PNG 文件");
    check(writeFile(ROOT + "/delete_me.txt", "delete"),
        "创建 DELETE 文件");
    check(writeFile(ROOT + "/query_delete.txt", "delete query"),
        "创建 query DELETE 文件");
    check(writeFile(ROOT + "/upload/same.txt", "old"),
        "创建同名上传文件");
    check(writeFile(ROOT + "/list/<unsafe>.txt", "x"),
        "创建 autoindex HTML 转义文件");
    check(writeFile(ROOT + "/list/a?b.txt", "x"),
        "创建 autoindex URL 编码文件");
    check(writeFile(ROOT + "/text-index/readme.txt", "plain index"),
        "创建 text index 文件");
    check(writeFile(ROOT + "/alias_target/hello.txt", "alias works"),
        "创建 alias 文件");
    check(writeFile(ROOT + "/custom404.html",
        "<html>custom 404</html>"), "创建自定义 404");

    const std::string executableScript = "#!/bin/sh\nexit 0\n";
    check(writeFile(ROOT + "/cgi/env.sh", executableScript),
        "创建可执行 CGI 路由占位文件");
    check(chmod((ROOT + "/cgi/env.sh").c_str(), 0755) == 0,
        "设置 CGI 占位文件可执行");
    check(writeFile(ROOT + "/cgi/noexec.sh", executableScript),
        "创建不可执行 CGI 路由占位文件");
    check(chmod((ROOT + "/cgi/noexec.sh").c_str(), 0644) == 0,
        "保持 CGI 占位文件不可执行");
    check(writeFile(ROOT + "/cgi/static.txt", "STATIC_NOT_CGI"),
        "创建 CGI location 内普通静态文件");
    check(mkfifo((ROOT + "/blocked.fifo").c_str(), 0600) == 0,
        "创建 FIFO 防阻塞目标");
}

/*
函数：testConfigFixture
用途：确认测试确实使用项目真实 Config parser，而不是手工伪造或 test_support 类型。
参数来源：config 是 main() 通过 Config(CONFIG_PATH) 创建的真实对象。
变量说明：servers 是 Config::getServers() 返回的只读 vector；cgiLocation 指向找到的 /cgi/ location。
实现逻辑：
    1. 检查 config.error 为 false 且只有一个 server。
    2. 检查 root、index 和自定义错误页已解析。
    3. 遍历 locations 找 /cgi/。
    4. 验证 .sh 到 /bin/sh 的 cgi_extension 映射存在。
*/
static void testConfigFixture(const Config &config)
{
    beginGroup("真实 Config 解析");
    const std::vector<ServerConfig> &servers = config.getServers();
    check(!config.error, "Config 没有解析错误");
    check(servers.size() == 1, "Config 解析出一个 server");
    if (servers.empty())
        return;
    check(servers[0].has_root && servers[0].root == ROOT,
        "server root 来自 conf");
    check(servers[0].index.size() == 2,
        "server 多 index 来自 conf");
    check(servers[0].error_pages.find(404)
        != servers[0].error_pages.end(), "error_page 来自 conf");

    const LocationConfig *cgiLocation = NULL;
    size_t i = 0;
    while (i < servers[0].locations.size())
    {
        if (servers[0].locations[i].path == "/cgi/")
            cgiLocation = &servers[0].locations[i];
        ++i;
    }
    check(cgiLocation != NULL, "真实 Config 解析 /cgi/ location");
    check(cgiLocation != NULL
        && cgiLocation->cgi_extensions.find(".sh")
            != cgiLocation->cgi_extensions.end()
        && cgiLocation->cgi_extensions.find(".sh")->second == "/bin/sh",
        "真实 Config 解析 cgi_extension 接口数据");
}

/*
函数：testResponseClass
用途：验证 Response class 的默认合法状态、header 封装、无 body 状态和二进制序列化。
参数来源：server 是真实 Config 的第一个 ServerConfig，用于读取 error_pages。
变量说明：response/bad/custom/noContent/notModified/binary 分别覆盖不同状态；raw/split 检查二进制序列化边界。
实现逻辑：
    1. 检查默认 200、Content-Length 和 keep-alive。
    2. 检查 header 大小写统一与受控字段不可被外部破坏。
    3. 检查错误页和强制 close。
    4. 检查 204/304 禁止 body。
    5. 检查 NUL 后字节仍被序列化。
*/
static void testResponseClass(const ServerConfig &server)
{
    beginGroup("Response class 封装");
    Response response;
    check(response.getVersion() == "HTTP/1.1", "默认版本正确");
    check(response.getStatusCode() == 200
        && response.getStatusMessage() == "OK", "默认 200 OK 合法");
    check(headerEquals(response, "Content-Length", "0"),
        "默认 Content-Length 为 0");
    check(headerEquals(response, "Connection", "keep-alive"),
        "默认 Connection 为 keep-alive");

    response.setHeader("content-type", "text/plain");
    response.setHeader("CONTENT-TYPE", "application/json");
    check(headerEquals(response, "Content-Type", "application/json"),
        "普通 header 大小写统一并覆盖");
    check(countHeaderName(response, "content-type") == 1,
        "同名 header 只保存一份");

    response.setBody("abc");
    response.setHeader("Content-Length", "999");
    response.removeHeader("content-length");
    check(headerEquals(response, "Content-Length", "3"),
        "外部不能破坏 Content-Length");
    response.setHeader("Connection", "close");
    check(!response.shouldCloseConnection()
        && headerEquals(response, "Connection", "keep-alive"),
        "外部不能绕过连接策略");
    response.setCloseConnection(true);
    check(response.shouldCloseConnection()
        && headerEquals(response, "Connection", "close"),
        "setCloseConnection 同步内部状态和 header");

    response.setHeader("Bad Header", "x");
    response.setHeader("X-Test", "ok\r\nInjected: yes");
    check(headerMissing(response, "Bad Header")
        && headerMissing(response, "X-Test"),
        "非法 header 名和值被拒绝");

    Response bad;
    bad.createResponse(400, "bad", server.error_pages);
    check(bad.getStatusCode() == 400 && bad.shouldCloseConnection(),
        "400 自动关闭连接");
    Response custom;
    custom.createResponse(404, "old", server.error_pages);
    check(custom.getBody() == "<html>custom 404</html>",
        "404 使用真实 Config 的自定义错误页");

    Response noContent;
    noContent.setBody("must disappear");
    noContent.setStatus(204);
    noContent.setBody("still forbidden");
    check(noContent.getBody().empty(), "204 始终禁止 body");
    check(headerMissing(noContent, "Content-Length"),
        "204 不发送 Content-Length");

    Response notModified;
    notModified.setBody("must disappear");
    notModified.setStatus(304);
    check(notModified.getBody().empty()
        && headerMissing(notModified, "Content-Length"),
        "304 不携带 body 和 Content-Length");

    Response binary;
    binary.appendBody("a\0b", 3);
    std::string raw = binary.responseToString();
    size_t split = raw.find("\r\n\r\n");
    check(split != std::string::npos
        && raw.size() - split - 4 == 3,
        "序列化保留 NUL 后二进制字节");
}

/*
函数：testGetAndRouting
用途：验证 normalized path、MIME、index、autoindex、alias、redirect、405/501、错误页和 Connection token。
参数来源：server 来自真实 Config；每个 Request 通过 parseRequest() 构造。
变量说明：request 复用为解析输出；response 保存每次 buildResponse() 结果。
实现逻辑：按场景逐个解析请求、调用 buildResponse、检查状态/header/body 和磁盘路径效果。
*/
static void testGetAndRouting(const ServerConfig &server)
{
    beginGroup("GET 与路由");
    Request request;
    Response response;

    check(parseRequest("GET", "/ping.html", "", "", server, request),
        "解析静态 GET");
    response = buildReadyResponse(request);
    check(response.getStatusCode() == 200
        && response.getBody() == "pong\n", "静态文件返回成功");
    check(headerEquals(response, "Content-Type", "text/html"),
        "HTML MIME 正确");

    check(parseRequest("GET", "/hello%20world.txt?x=1", "", "",
        server, request), "解析 encoded path 和 query");
    response = buildReadyResponse(request);
    check(response.getBody() == "space",
        "文件映射使用 getPath 而不是 raw URI");

    check(parseRequest("GET", "/image.png", "", "", server, request),
        "解析二进制 GET");
    response = buildReadyResponse(request);
    check(response.getBody() == std::string("\x89PNG", 4)
        && headerEquals(response, "Content-Type", "image/png"),
        "二进制文件和 MIME 正确");

    check(parseRequest("GET", "/", "", "", server, request),
        "解析目录 GET");
    response = buildReadyResponse(request);
    check(response.getBody() == "home\n",
        "多 index fallback 命中 index.html");

    check(parseRequest("GET", "/text-index/", "", "", server, request),
        "解析 text index");
    response = buildReadyResponse(request);
    check(response.getBody() == "plain index"
        && headerEquals(response, "Content-Type", "text/plain"),
        "index 使用真实文件 MIME");

    check(parseRequest("GET", "/list/", "", "", server, request),
        "解析 autoindex");
    response = buildReadyResponse(request);
    check(response.getStatusCode() == 200,
        "autoindex 返回 200");
    check(response.getBody().find("Index of /list/")
        != std::string::npos, "autoindex 显示 URL path");
    check(response.getBody().find(ROOT + "/list/")
        == std::string::npos, "autoindex 不泄露磁盘路径");
    check(response.getBody().find("&lt;unsafe&gt;.txt")
        != std::string::npos, "autoindex 显示名 HTML 转义");
    check(response.getBody().find("href=\"a%3Fb.txt\"")
        != std::string::npos, "autoindex href URL 编码");

    check(parseRequest("GET", "/blocked.fifo", "", "", server, request),
        "解析 FIFO GET");
    response = buildReadyResponse(request);
    check(response.getStatusCode() == 403,
        "GET 特殊文件不会阻塞读取");

    check(parseRequest("GET", "/alias/hello.txt", "", "",
        server, request), "解析 alias GET");
    response = buildReadyResponse(request);
    check(response.getStatusCode() == 200
        && response.getBody() == "alias works", "alias 路径正确");

    check(parseRequest("GET", "/old/item", "", "", server, request),
        "解析 301");
    response = buildReadyResponse(request);
    check(response.getStatusCode() == 301
        && headerEquals(response, "Location", "/new/"),
        "301 状态和 Location 正确");

    check(parseRequest("GET", "/not-modified/item", "", "",
        server, request), "解析 304");
    response = buildReadyResponse(request);
    check(response.getStatusCode() == 304
        && response.getBody().empty()
        && headerMissing(response, "Content-Length"),
        "304 配置不生成非法 body");

    check(parseRequest("POST", "/readonly/file.txt",
        "Content-Length: 1\r\n", "x", server, request),
        "解析只读 location POST");
    response = buildReadyResponse(request);
    check(response.getStatusCode() == 405
        && headerEquals(response, "Allow", "GET"),
        "405 返回 Allow");

    check(parseRequest("PATCH", "/ping.html", "", "", server, request),
        "解析未实现 method");
    response = buildReadyResponse(request);
    check(response.getStatusCode() == 501,
        "合法但未实现 method 返回 501");

    check(parseRequest("GET", "/missing.txt", "", "", server, request),
        "解析缺失资源");
    response = buildReadyResponse(request);
    check(response.getStatusCode() == 404
        && response.getBody() == "<html>custom 404</html>",
        "404 使用自定义错误页");

    check(parseRequest("GET", "/ping.html",
        "Connection: keep-alive, close\r\n", "", server, request),
        "解析 Connection token 列表");
    response = buildReadyResponse(request);
    check(response.shouldCloseConnection(),
        "Connection 列表包含 close 时关闭");
}

/*
函数：testPost
用途：验证普通/chunked 上传、upload_path、唯一文件名、media type 和错误状态。
参数来源：server 来自真实 Config；请求由 parseRequest() 构造。
变量说明：request/response 复用；readFile/pathExists 检查磁盘结果。
实现逻辑：逐个发送 POST，检查 Response 状态和实际保存文件内容。
*/
static void testPost(const ServerConfig &server)
{
    beginGroup("POST 上传");
    Request request;
    Response response;

    check(parseRequest("POST", "/upload/new.txt",
        "Content-Length: 3\r\nContent-Type: text/plain\r\n",
        "abc", server, request), "解析普通 POST");
    response = buildReadyResponse(request);
    check(response.getStatusCode() == 200
        && readFile(ROOT + "/upload/new.txt") == "abc",
        "普通 POST 写入配置 upload_path");

    check(parseRequest("POST", "/upload/chunk.txt",
        "Transfer-Encoding: chunked\r\nContent-Type: text/plain\r\n",
        "2\r\nhe\r\n3\r\nllo\r\n0\r\n\r\n", server, request),
        "解析 chunked POST");
    response = buildReadyResponse(request);
    check(response.getStatusCode() == 200
        && readFile(ROOT + "/upload/chunk.txt") == "hello",
        "chunked 解码 body 写盘");

    check(parseRequest("POST", "/missing-parent/name.txt",
        "Content-Length: 1\r\nContent-Type: text/plain\r\n",
        "x", server, request), "解析 URI 父目录不存在 POST");
    response = buildReadyResponse(request);
    check(response.getStatusCode() == 200
        && readFile(ROOT + "/upload/name.txt") == "x",
        "POST 不错误依赖 URI 父目录");

    check(parseRequest("POST", "/joined/child/joined.txt",
        "Content-Length: 4\r\nContent-Type: text/plain\r\n",
        "join", server, request), "解析相对 upload_path POST");
    response = buildReadyResponse(request);
    check(response.getStatusCode() == 200
        && readFile(ROOT + "/joined_upload/joined.txt") == "join",
        "joinPaths 正确拼 upload_path");

    check(parseRequest("POST", "/upload/a..b.txt",
        "Content-Length: 2\r\nContent-Type: text/plain\r\n",
        "ok", server, request), "解析连续点文件名");
    response = buildReadyResponse(request);
    check(response.getStatusCode() == 200
        && readFile(ROOT + "/upload/a..b.txt") == "ok",
        "文件名内部连续点允许");

    check(parseRequest("POST", "/upload/multipart.bin",
        "Content-Length: 3\r\n"
        "Content-Type: multipart/form-data; boundary=x\r\n",
        "abc", server, request), "解析 multipart POST");
    response = buildReadyResponse(request);
    check(response.getStatusCode() == 415
        && !pathExists(ROOT + "/upload/multipart.bin"),
        "未实现 multipart 时返回 415 且不写原始 framing");

    check(parseRequest("POST", "/missing-upload/file.txt",
        "Content-Length: 1\r\nContent-Type: text/plain\r\n",
        "x", server, request), "解析缺失 upload_path");
    response = buildReadyResponse(request);
    check(response.getStatusCode() == 409,
        "真实上传目录不存在返回 409");

    check(parseRequest("POST", "/upload/no-length.txt", "", "",
        server, request), "解析无 framing POST");
    response = buildReadyResponse(request);
    check(response.getStatusCode() == 411,
        "无 Content-Length/TE 返回 411");

    check(parseRequest("POST", "/upload/same.txt",
        "Content-Length: 3\r\n", "new", server, request),
        "解析同名上传");
    response = buildReadyResponse(request);
    check(readFile(ROOT + "/upload/same.txt") == "old"
        && readFile(ROOT + "/upload/same_1.txt") == "new",
        "同名上传生成 _1 且不覆盖");
}

/*
函数：testDelete
用途：验证 DELETE 普通文件、query 去除、缺失资源和目录拒绝。
参数来源：server 来自真实 Config；请求由 parseRequest() 构造。
变量说明：request/response 复用；pathExists 检查 unlink 结果。
实现逻辑：依次调用 buildResponse() 并检查状态码、body 边界和磁盘状态。
*/
static void testDelete(const ServerConfig &server)
{
    beginGroup("DELETE");
    Request request;
    Response response;

    check(parseRequest("DELETE", "/delete_me.txt", "", "",
        server, request), "解析 DELETE");
    response = buildReadyResponse(request);
    check(response.getStatusCode() == 204
        && !pathExists(ROOT + "/delete_me.txt"),
        "DELETE 成功并返回 204");
    check(response.getBody().empty()
        && headerMissing(response, "Content-Length"),
        "DELETE 204 无 body/Content-Length");

    check(parseRequest("DELETE", "/query_delete.txt?force=1", "", "",
        server, request), "解析带 query DELETE");
    response = buildReadyResponse(request);
    check(response.getStatusCode() == 204
        && !pathExists(ROOT + "/query_delete.txt"),
        "DELETE 使用 normalized path");

    check(parseRequest("DELETE", "/missing-delete.txt", "", "",
        server, request), "解析缺失 DELETE");
    response = buildReadyResponse(request);
    check(response.getStatusCode() == 404,
        "DELETE 缺失文件返回 404");

    check(parseRequest("DELETE", "/delete_dir", "", "",
        server, request), "解析目录 DELETE");
    response = buildReadyResponse(request);
    check(response.getStatusCode() == 403,
        "DELETE 目录返回 403");
}

/*
函数：testCgiResponseCompatibility
用途：验证 Response 在不修改 ServerManager 和 CGI 模块公开接口的情况下，仍能完成 CGI 路径交接与输出解析。
参数来源：server 来自真实 Config 解析出的第一个 ServerConfig。
变量说明：
    - request：由真实 RequestParser 构造的请求。
    - response：buildResponse() 返回的普通响应或 CGI 内部占位响应，也是 parseCgiOutput() 的被修改对象。
    - expectedScript：EffectiveRoute 应计算出的真实 CGI 脚本路径。
    - cgiOutput：模拟 ServerManager 从 CGI 输出管道完整收集到的原始字节。
实现逻辑：
    1. 验证 buildResponse() 的返回类型仍是 Response，并通过 X-Internal-CGI-Path 向现有 ServerManager 交付脚本路径。
    2. 调用现有 ServerManager 已使用的 response.parseCgiOutput()，验证 Status、普通 headers、body 和长度规则。
    3. 验证 CGI 不能覆盖 Content-Length、Transfer-Encoding 和 Connection 等服务器管理字段。
    4. 验证纯 body、LF 分隔、204、空输出、畸形输出、重复 Status 和非法状态码。
    5. 验证解析 CGI 输出后内部路径 header 被清除，不会进入最终客户端响应。
    6. 验证不存在、不可执行、非 CGI 后缀和不允许的方法仍直接返回正常 Response。

它不测试：
CGI 异步启动接口
pipe
fork
execve
poll
向 CGI stdin 写 POST body
从 CGI stdout 读取数据
waitpid
超时处理
这些属于 CGI 和 ServerManager。
*/
static void testCgiResponseCompatibility(const ServerConfig &server)
{
    beginGroup("Response 的 CGI 旧接口兼容与 stdout 适配");
    Request request;
    Response response;
    const std::string expectedScript = ROOT + "/cgi/env.sh";
    std::string cgiOutput;

    check(parseRequest("GET", "/cgi/env.sh?name=Tom", "", "",
        server, request), "解析 CGI GET 路由请求");
    response = buildReadyResponse(request);
    check(response.getStatusCode() == 200
        && headerEquals(response, "X-Internal-CGI-Path", expectedScript),
        "CGI 请求通过既有内部 header 交付真实脚本路径");

    cgiOutput =
        "Content-Type: text/plain\r\n"
        "X-CGI-Test: yes\r\n"
        "Content-Length: 999\r\n"
        "Connection: close\r\n"
        "Transfer-Encoding: chunked\r\n"
        "\r\n"
        "method=GET\nquery=name=Tom";
    response.parseCgiOutput(cgiOutput);
    check(response.getStatusCode() == 200,
        "合法 CGI stdout 默认生成 200");
    check(headerEquals(response, "Content-Type", "text/plain"),
        "CGI Content-Type 导入 Response");
    check(headerEquals(response, "X-CGI-Test", "yes"),
        "CGI 自定义 header 导入 Response");
    check(headerMissing(response, "X-Internal-CGI-Path"),
        "解析 CGI 输出后清除内部脚本路径 header");
    check(response.getBody() == "method=GET\nquery=name=Tom",
        "CGI body 按原始字节保存");
    std::ostringstream actualLength;
    actualLength << response.getBody().size();
    check(headerEquals(response, "Content-Length", actualLength.str()),
        "Response 忽略 CGI 冲突长度并按真实 body 重算");
    check(headerMissing(response, "Transfer-Encoding"),
        "Response 丢弃 CGI Transfer-Encoding");
    check(!response.shouldCloseConnection()
        && headerEquals(response, "Connection", "keep-alive"),
        "CGI Connection 不能覆盖原 Response 连接策略");

    check(parseRequest("GET", "/cgi/env.sh",
        "Connection: close\r\n", "", server, request),
        "解析要求关闭连接的 CGI 请求");
    response = buildReadyResponse(request);
    response.parseCgiOutput(
        "Content-Type: text/plain\r\n\r\nclose-body");
    check(response.shouldCloseConnection()
        && headerEquals(response, "Connection", "close"),
        "parseCgiOutput 继承 CGI 占位 Response 的 close 策略");

    check(parseRequest("POST", "/cgi/env.sh",
        "Content-Length: 5\r\nContent-Type: text/plain\r\n",
        "hello", server, request), "解析 CGI POST 路由请求");
    response = buildReadyResponse(request);
    check(headerEquals(response, "X-Internal-CGI-Path", expectedScript),
        "POST CGI 继续使用现有 ServerManager 识别的路径 header");
    response.parseCgiOutput(
        "Content-Type: application/octet-stream\r\n\r\n"
        + std::string("A\0B", 3));
    check(response.getBody().size() == 3
        && response.getBody()[0] == 'A'
        && response.getBody()[1] == '\0'
        && response.getBody()[2] == 'B',
        "CGI stdout body 支持二进制 NUL");
    check(headerEquals(response, "Content-Length", "3"),
        "二进制 CGI body 的 Content-Length 正确");

    check(parseRequest("GET", "/cgi/env.sh", "", "",
        server, request), "解析纯 body CGI 适配请求");
    response = buildReadyResponse(request);
    response.parseCgiOutput("plain-cgi-body");
    check(response.getStatusCode() == 200
        && response.getBody() == "plain-cgi-body",
        "无 CGI header 输出作为 200 body");
    check(headerEquals(response, "Content-Type", "text/html"),
        "纯 body CGI 使用默认 text/html");

    response = Response(request);
    response.parseCgiOutput(
        "Content-Type: text/plain\nX-LF: yes\n\nlf-body");
    check(response.getBody() == "lf-body"
        && headerEquals(response, "Content-Type", "text/plain")
        && headerEquals(response, "X-LF", "yes"),
        "CGI parser 接受 LF header/body 分隔");

    response = Response(request);
    response.parseCgiOutput(
        "Status: 201 Created\r\n"
        "Content-Type: text/plain\r\n"
        "Location: /created/item\r\n"
        "\r\n"
        "created-body");
    check(response.getStatusCode() == 201
        && response.getStatusMessage() == "Created",
        "Status: 201 转成真正 Response 状态");
    check(headerEquals(response, "Location", "/created/item")
        && response.getBody() == "created-body",
        "Status CGI 保留普通 Location 和 body");

    response = Response(request);
    response.parseCgiOutput(
        "Status: 204 No Content\r\n"
        "Content-Type: text/plain\r\n"
        "\r\n"
        "forbidden-body");
    check(response.getStatusCode() == 204 && response.getBody().empty(),
        "CGI 204 丢弃脚本附带的非法 body");
    check(headerMissing(response, "Content-Type")
        && headerMissing(response, "Content-Length"),
        "CGI 204 保持无 body header 规则");

    response = Response(request);
    response.parseCgiOutput("");
    check(response.getStatusCode() == 502
        && response.getStatusMessage() == "Bad Gateway"
        && response.shouldCloseConnection(),
        "空 CGI stdout 生成 502 并关闭连接");

    response = Response(request);
    response.parseCgiOutput("Broken Header\r\n\r\nbad");
    check(response.getStatusCode() == 502
        && response.shouldCloseConnection(),
        "缺少冒号的 CGI header 生成 502 close");

    response = Response(request);
    response.parseCgiOutput(
        "Status: 200 OK\r\n"
        "Status: 201 Created\r\n\r\nbad");
    check(response.getStatusCode() == 502,
        "重复 Status header 生成 502");

    response = Response(request);
    response.parseCgiOutput("Status: 99 Invalid\r\n\r\nbad");
    check(response.getStatusCode() == 502,
        "超出 100 到 599 的 CGI Status 生成 502");

    response = Response(request);
    response.parseCgiOutput("Bad Header: ok\r\n\r\nbad");
    check(response.getStatusCode() == 502,
        "非法 CGI header name 生成 502");

    check(parseRequest("GET", "/cgi/missing.sh", "", "",
        server, request), "解析不存在 CGI 脚本请求");
    response = buildReadyResponse(request);
    check(response.getStatusCode() == 404,
        "不存在 CGI 脚本在启动前返回 404");

    check(parseRequest("GET", "/cgi/noexec.sh", "", "",
        server, request), "解析由解释器执行的不可执行 CGI 脚本请求");
    response = buildReadyResponse(request);
    check(response.getStatusCode() == 200
        && headerEquals(response, "X-Internal-CGI-Interpreter", "/bin/sh"),
        "配置解释器时脚本只需可读，并向 ServerManager 交付解释器路径");

    ServerConfig directServer = server;
    size_t locationIndex = 0;
    while (locationIndex < directServer.locations.size())
    {
        if (directServer.locations[locationIndex].path == "/cgi/")
        {
            directServer.locations[locationIndex].cgi_extensions[".sh"] = "";
            break;
        }
        ++locationIndex;
    }
    check(parseRequest("GET", "/cgi/noexec.sh", "", "",
        directServer, request), "解析无解释器的不可执行 CGI 脚本请求");
    response = buildReadyResponse(request);
    check(response.getStatusCode() == 403,
        "没有配置解释器时脚本仍必须拥有执行权限");

    check(parseRequest("GET", "/cgi/static.txt", "", "",
        server, request), "解析 CGI location 内普通静态文件");
    response = buildReadyResponse(request);
    check(response.getStatusCode() == 200
        && response.getBody() == "STATIC_NOT_CGI",
        "未匹配 cgi_extension 时仍走静态 GET");

    check(parseRequest("DELETE", "/cgi/env.sh", "", "",
        server, request), "解析 CGI location DELETE");
    response = buildReadyResponse(request);
    check(response.getStatusCode() == 405
        && headerEquals(response, "Allow", "GET, POST"),
        "CGI 分发前仍先遵守 location allow_methods");
}

/*
函数：testSessionResponseIntegration
用途：验证 Response 唯一 SessionStore 入口完成 Cookie 往返、状态持久化、登录轮换、退出销毁和缓存保护。
参数来源：server 来自真实 Config；所有 Request 继续通过真实 RequestParser 构造。
变量说明：
    - store：跨多个请求复用的服务器级 SessionStore。
    - request/response：每个场景的输入和输出。
    - firstId/loginId/newId：分别保存匿名、登录后和退出后新建的 Session ID。
    - storedUser/setCookie：验证服务端值和删除 Cookie 属性。
实现逻辑：
    1. 首次 counter 创建 Cookie，第二次回传同一 Cookie 后 visits 递增。
    2. 使用唯一 SessionStore 入口处理普通静态文件，确认原 Response 行为和 store 数量不受影响。
    3. login 保存用户并轮换 ID，旧 ID 失效且 visits 数据被保留。
    4. 验证表单解码、HTML 转义、错误方法、媒体类型和 malformed form。
    5. logout 删除服务端记录并返回 Max-Age=0；旧 Cookie 再访问会创建新 Session。
    6. 验证成功与错误 Session 响应都禁止缓存，错误方法不执行 Session 操作并返回精确 Allow header。
*/
static void testSessionResponseIntegration(const ServerConfig &server)
{
    beginGroup("Response 与 Session Bonus 集成");
    SessionStore store(1800, 64);
    Request request;
    Response response;
    std::string firstId;
    std::string loginId;
    std::string newId;
    std::string storedUser;
    std::string setCookie;

    check(parseRequest("GET", "/session/counter", "", "",
        server, request), "解析首次 Session counter 请求");
    response = buildResponse(request, store);
    check(response.getStatusCode() == 200
        && response.getBody().find("Visits: 1") != std::string::npos,
        "首次 counter 返回 Visits: 1");
    check(response.getBody().find("action=\"/session/login\"")
            != std::string::npos
        && response.getBody().find("action=\"/session/logout\"")
            != std::string::npos,
        "counter Response 包含浏览器可用的 login/logout 表单");
    check(extractSessionIdFromSetCookie(response, firstId)
        && SessionStore::isValidSessionId(firstId),
        "首次 counter 返回合法 Session Cookie");
    check(store.size() == 1
        && headerEquals(response, "Content-Type",
            "text/html; charset=utf-8"),
        "首次 counter 创建一条服务端记录和 HTML 类型");
    check(headerEquals(response, "Cache-Control", "no-store")
        && headerEquals(response, "Pragma", "no-cache"),
        "成功 Session Response 明确禁止缓存");

    SessionStore queryStore(1800, 8);
    check(parseRequest("GET", "/session/counter?source=test", "", "",
        server, request), "解析带 query 的 Session counter");
    response = buildResponse(request, queryStore);
    check(response.getStatusCode() == 200
        && response.getBody().find("Visits: 1") != std::string::npos
        && queryStore.size() == 1,
        "Session 虚拟路由使用 normalized path 并忽略 query");

    check(parseRequest("GET", "/session/counter",
        makeSessionCookieHeader(firstId), "", server, request),
        "解析携带多个 Cookie 的第二次 counter");
    response = buildResponse(request, store);
    check(response.getStatusCode() == 200
        && response.getBody().find("Visits: 2") != std::string::npos,
        "同一 Cookie 恢复 Session 并递增到 Visits: 2");
    check(extractSessionIdFromSetCookie(response, newId)
        && newId == firstId && store.size() == 1,
        "counter 刷新同一 ID 而不重复创建 Session");

    check(parseRequest("GET", "/ping.html", "", "",
        server, request), "解析唯一入口下的普通静态 GET");
    response = buildResponse(request, store);
    check(response.getStatusCode() == 200
        && response.getBody() == "pong\n" && store.size() == 1,
        "唯一 SessionStore 入口不改变普通静态文件行为");

    check(parseRequest("POST", "/session/login",
        makeSessionCookieHeader(firstId)
        + "Content-Length: 5\r\nContent-Type: text/plain\r\n",
        "Alice", server, request), "解析 text/plain Session login");
    response = buildResponse(request, store);
    check(response.getStatusCode() == 200
        && response.getBody().find("Alice") != std::string::npos,
        "login 返回用户名页面");
    check(extractSessionIdFromSetCookie(response, loginId)
        && loginId != firstId,
        "login 轮换 Session ID");
    check(!store.resumeSession(firstId)
        && store.getValue(loginId, "user", storedUser)
        && storedUser == "Alice", "旧 ID 失效且新 Session 保存 user");
    check(store.size() == 1, "Session ID 轮换不增加记录总数");

    check(parseRequest("GET", "/session/counter",
        makeSessionCookieHeader(loginId), "", server, request),
        "解析登录后 counter 请求");
    response = buildResponse(request, store);
    check(response.getBody().find("Visits: 3") != std::string::npos,
        "登录轮换保留匿名 Session 的 visits 数据");

    SessionStore formStore(1800, 16);
    check(parseRequest("POST", "/session/login",
        "Content-Length: 15\r\n"
        "Content-Type: application/x-www-form-urlencoded; charset=UTF-8\r\n",
        "user=%3CA%26%3E", server, request),
        "解析 urlencoded Session login");
    response = buildResponse(request, formStore);
    check(response.getStatusCode() == 200
        && response.getBody().find("&lt;A&amp;&gt;")
            != std::string::npos,
        "urlencoded user 被解码并在 HTML 中转义");

    check(parseRequest("POST", "/session/login",
        "Content-Length: 16\r\n"
        "Content-Type: application/x-www-form-urlencoded\r\n",
        "x=1&user=Tom+Lee", server, request),
        "解析带额外字段和加号的 urlencoded login");
    response = buildResponse(request, formStore);
    check(response.getStatusCode() == 200
        && response.getBody().find("Tom Lee") != std::string::npos,
        "urlencoded login 忽略额外字段并把加号解码为空格");

    size_t formSize = formStore.size();
    check(parseRequest("POST", "/session/login",
        "Content-Length: 17\r\n"
        "Content-Type: application/x-www-form-urlencoded\r\n",
        "user=One&user=Two", server, request),
        "解析重复 user 字段的 urlencoded login");
    response = buildResponse(request, formStore);
    check(response.getStatusCode() == 400
        && formStore.size() == formSize
        && headerMissing(response, "Set-Cookie"),
        "重复 user 字段返回 400 且不创建 Session");

    check(parseRequest("POST", "/session/login",
        "Content-Length: 7\r\nContent-Type: application/json\r\n",
        "\"Alice\"", server, request), "解析不支持媒体类型的 login");
    response = buildResponse(request, formStore);
    check(response.getStatusCode() == 415
        && formStore.size() == formSize
        && headerMissing(response, "Set-Cookie"),
        "不支持媒体类型返回 415 且不创建 Session");

    check(parseRequest("POST", "/session/login",
        "Content-Length: 8\r\n"
        "Content-Type: application/x-www-form-urlencoded\r\n",
        "user=%ZZ", server, request), "解析 malformed form login");
    response = buildResponse(request, formStore);
    check(response.getStatusCode() == 400
        && formStore.size() == formSize
        && headerMissing(response, "Set-Cookie"),
        "malformed form 返回 400 且不创建 Session");

    check(parseRequest("POST", "/session/login",
        "Content-Length: 0\r\nContent-Type: text/plain\r\n",
        "", server, request), "解析空用户名 login");
    response = buildResponse(request, formStore);
    check(response.getStatusCode() == 400
        && formStore.size() == formSize
        && headerMissing(response, "Set-Cookie"),
        "空用户名返回 400 且不创建 Session");

    check(parseRequest("POST", "/session/counter",
        "Content-Length: 0\r\n", "", server, request),
        "解析错误方法 counter");
    response = buildResponse(request, store);
    check(response.getStatusCode() == 405
        && headerEquals(response, "Allow", "GET"),
        "counter 错误方法返回 405 Allow GET");
    check(headerEquals(response, "Cache-Control", "no-store")
        && headerEquals(response, "Pragma", "no-cache"),
        "Session 错误响应同样禁止缓存");

    check(parseRequest("GET", "/session/login", "", "",
        server, request), "解析错误方法 login");
    response = buildResponse(request, store);
    check(response.getStatusCode() == 405
        && headerEquals(response, "Allow", "POST"),
        "login 错误方法返回 405 Allow POST");

    check(parseRequest("GET", "/session/counter",
        makeSessionCookieHeader(loginId) + "Connection: close\r\n",
        "", server, request), "解析要求关闭连接的 Session 请求");
    response = buildResponse(request, store);
    check(response.shouldCloseConnection()
        && headerEquals(response, "Connection", "close"),
        "Session Response 继承 Request 的 Connection 策略");

    check(parseRequest("POST", "/session/logout",
        makeSessionCookieHeader(loginId) + "Content-Length: 0\r\n",
        "", server, request), "解析 Session logout");
    response = buildResponse(request, store);
    check(response.getStatusCode() == 200
        && response.getHeader("Set-Cookie", setCookie)
        && setCookie.find("Max-Age=0") != std::string::npos,
        "logout 返回浏览器删除 Cookie");
    check(store.size() == 0 && !store.resumeSession(loginId),
        "logout 删除服务端 Session");

    check(parseRequest("GET", "/session/counter",
        makeSessionCookieHeader(loginId), "", server, request),
        "解析退出后继续使用旧 Cookie");
    response = buildResponse(request, store);
    check(response.getBody().find("Visits: 1") != std::string::npos
        && extractSessionIdFromSetCookie(response, newId)
        && newId != loginId,
        "退出后的旧 Cookie 创建新 Session");

    check(parseRequest("GET", "/session/counter/extra", "", "",
        server, request), "解析非精确 Session 路径");
    response = buildResponse(request, store);
    check(response.getStatusCode() == 404
        && headerMissing(response, "Set-Cookie"),
        "非精确路径继续走普通文件系统路由");

    check(parseRequest("DELETE", "/session/logout", "", "",
        server, request), "解析错误方法的 logout 请求");
    response = buildResponse(request, store);
    check(response.getStatusCode() == 405
        && headerEquals(response, "Allow", "POST")
        && headerMissing(response, "Set-Cookie"),
        "logout 错误方法返回 405 且不会修改浏览器 Cookie");
}

/*
函数：testMissingConfig
用途：验证 Request 带有真实 ServerConfig 对象但没有 root 时，Response 返回 500 而不是崩溃。
参数来源：无参数；函数内部构造项目真实 ServerConfig 默认对象。
变量说明：noRoot 是缺少 has_root 的配置；request/response 是解析和输出对象。
实现逻辑：解析普通 GET，调用 buildResponse，检查 route 创建失败映射为 500。
*/
static void testMissingConfig()
{
    beginGroup("配置防御");
    ServerConfig noRoot;
    Request request;
    check(parseRequest("GET", "/ping.html", "", "", noRoot, request),
        "解析无 root 配置请求");
    Response response = buildReadyResponse(request);
    check(response.getStatusCode() == 500,
        "无有效 root 返回 500");
}

/*
函数：main
用途：准备真实文件和配置，依次运行全部 Response 与 Session 集成测试，并返回 shell 可识别状态。
参数来源：无命令行参数；使用 CONFIG_PATH 固定测试配置。
变量说明：config 是真实 Config 对象；servers 是其只读 server 列表。
实现逻辑：
    1. 写入 fixture 文件和 conf。
    2. 构造 Config 并确认测试使用真实配置解析结果。
    3. 配置有效时运行 Response class、GET、POST、DELETE、CGI、Session 集成和配置防御测试。
    4. 打印总计；失败数为零返回 0，否则返回 1。
*/
int main()
{
    prepareFixtureFiles();
    Config config(CONFIG_PATH);
    testConfigFixture(config);
    const std::vector<ServerConfig> &servers = config.getServers();
    if (!config.error && !servers.empty())
    {
        const ServerConfig &server = servers[0];
        testResponseClass(server);
        testGetAndRouting(server);
        testPost(server);
        testDelete(server);
        testCgiResponseCompatibility(server);
        testSessionResponseIntegration(server);
        testMissingConfig();
    }

    std::cout << "\n========== Response/Session 测试汇总 =========="
              << std::endl;
    std::cout << "总断言：" << g_total << std::endl;
    std::cout << "通过：" << (g_total - g_failed) << std::endl;
    std::cout << "失败：" << g_failed << std::endl;
    return g_failed == 0 ? 0 : 1;
}
