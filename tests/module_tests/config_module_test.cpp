#include "Config.hpp"
#include "ConfigRouteUtils.hpp"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>

struct ParseResult
{
    bool ok;
    std::vector<ServerConfig> servers;
    std::string diagnostics;
};

class TestRunner
{
private:
    int _passed;
    int _failed;

public:
    TestRunner() : _passed(0), _failed(0) {}

    void expect(bool condition, const std::string &name,
                const std::string &details = "")
    {
        if (condition)
        {
            ++_passed;
            std::cout << "[PASS] " << name << std::endl;
        }
        else
        {
            ++_failed;
            std::cout << "[FAIL] " << name;
            if (!details.empty())
                std::cout << " -- " << details;
            std::cout << std::endl;
        }
    }

    int passed() const { return _passed; }
    int failed() const { return _failed; }
    int total() const { return _passed + _failed; }
};

static std::string g_tmp_dir;
static int g_file_counter = 0;

static std::string makeTempPath()
{
    std::ostringstream path;
    path << g_tmp_dir << "/case_" << g_file_counter++ << ".conf";
    return path.str();
}

static bool writeFile(const std::string &path, const std::string &content)
{
    std::ofstream file(path.c_str(), std::ios::out | std::ios::binary);
    if (!file.is_open())
        return false;
    file.write(content.data(), static_cast<std::streamsize>(content.size()));
    bool ok = file.good();
    file.close();
    return ok;
}

static ParseResult parseConfigText(const std::string &content)
{
    ParseResult result;
    result.ok = false;

    const std::string path = makeTempPath();
    if (!writeFile(path, content))
    {
        result.diagnostics = "cannot create temporary config";
        return result;
    }

    std::ostringstream captured;
    std::streambuf *old_cerr = std::cerr.rdbuf(captured.rdbuf());
    std::streambuf *old_cout = std::cout.rdbuf(captured.rdbuf());

    {
        Config config(path);
        result.ok = !config.error;
        result.servers = config.getServers();
    }

    std::cout.rdbuf(old_cout);
    std::cerr.rdbuf(old_cerr);
    result.diagnostics = captured.str();
    std::remove(path.c_str());
    return result;
}

static std::string oneServer(const std::string &extra)
{
    return "server {\n"
           "listen 8080;\n"
           "root srv/www;\n" +
           extra +
           "\n}\n";
}

static std::string oneLocation(const std::string &extra)
{
    return oneServer("location /test/ {\n" + extra + "\n}\n");
}

static void expectValid(TestRunner &runner, const std::string &name,
                        const std::string &config)
{
    ParseResult result = parseConfigText(config);
    runner.expect(result.ok, name,
                  result.ok ? "" : result.diagnostics);
}

static void expectInvalid(TestRunner &runner, const std::string &name,
                          const std::string &config)
{
    ParseResult result = parseConfigText(config);
    runner.expect(!result.ok, name,
                  !result.ok ? "" : "parser accepted an invalid config");
}

static void testValidStructureAndStorage(TestRunner &runner)
{
    const std::string full =
        "server {\n"
        "listen 127.0.0.1:8081;\n"
        "server_name example.com www.example.com;\n"
        "root srv/www;\n"
        "index index.html home.html;\n"
        "autoindex off;\n"
        "max_body_size 2M;\n"
        "allow_methods get POST delete;\n"
        "upload_path srv/uploads;\n"
        "error_page 404 500 srv/errors/error.html;\n"
        "location /upload/ {\n"
        "alias srv/private;\n"
        "index upload.html fallback.html;\n"
        "autoindex on;\n"
        "max_body_size 3K;\n"
        "allow_methods POST DELETE;\n"
        "upload_path srv/private/uploads;\n"
        "cgi_extension .py /usr/bin/python3;\n"
        "return 302 /moved;\n"
        "}\n"
        "}\n";

    ParseResult result = parseConfigText(full);
    runner.expect(result.ok, "完整 server/location 配置可解析", result.diagnostics);
    runner.expect(result.servers.size() == 1, "完整配置生成一个 ServerConfig");
    if (!result.ok || result.servers.size() != 1)
        return;

    const ServerConfig &server = result.servers[0];
    runner.expect(server.host == "127.0.0.1", "listen 保存 IPv4");
    runner.expect(server.port == 8081, "listen 保存端口");
    runner.expect(server.countport == 1, "listen 出现计数正确");
    runner.expect(server.server_names.size() == 2, "保存多个 server_name");
    runner.expect(server.server_names.size() == 2 &&
                      server.server_names[0] == "example.com" &&
                      server.server_names[1] == "www.example.com",
                  "server_name 保持配置顺序");
    runner.expect(server.root == "srv/www" && server.has_root,
                  "保存 server root 与 has_root");
    runner.expect(server.index.size() == 2 &&
                      server.index[0] == "index.html" &&
                      server.index[1] == "home.html",
                  "server 多 index 保持顺序");
    runner.expect(!server.autoindex && server.has_autoindex,
                  "server 明确保存 autoindex off");
    runner.expect(server.max_body_size == 2UL * 1024UL * 1024UL &&
                      server.has_body_size,
                  "server max_body_size 转换为字节");
    runner.expect(server.allow_methods.size() == 3,
                  "server 保存三个允许方法");
    runner.expect(server.allow_methods.count("GET") == 1 &&
                      server.allow_methods.count("POST") == 1 &&
                      server.allow_methods.count("DELETE") == 1,
                  "allow_methods 统一保存为大写");
    runner.expect(server.upload_path == "srv/uploads",
                  "保存 server upload_path");
    runner.expect(server.error_pages.size() == 2,
                  "一个 error_page 可映射多个状态码");
    runner.expect(server.error_pages.find(404) != server.error_pages.end() &&
                      server.error_pages.find(500) != server.error_pages.end() &&
                      server.error_pages.find(404)->second == "srv/errors/error.html" &&
                      server.error_pages.find(500)->second == "srv/errors/error.html",
                  "error_page 状态码映射正确");
    runner.expect(server.locations.size() == 1,
                  "生成一个 LocationConfig");
    if (server.locations.size() != 1)
        return;

    const LocationConfig &loc = server.locations[0];
    runner.expect(loc.path == "/upload/", "保存 location path");
    runner.expect(loc.alias == "srv/private" && loc.has_alias,
                  "保存 location alias 与 has_alias");
    runner.expect(!loc.has_root, "alias location 没有误设 has_root");
    runner.expect(loc.index.size() == 2 &&
                      loc.index[0] == "upload.html" &&
                      loc.index[1] == "fallback.html",
                  "location 多 index 保持顺序");
    runner.expect(loc.autoindex && loc.has_autoindex,
                  "location 明确保存 autoindex on");
    runner.expect(loc.max_body_size == 3UL * 1024UL && loc.has_body_size,
                  "location max_body_size 转换并标记覆盖");
    runner.expect(loc.allow_methods.size() == 2 &&
                      loc.allow_methods.count("POST") == 1 &&
                      loc.allow_methods.count("DELETE") == 1,
                  "location allow_methods 保存正确");
    runner.expect(loc.upload_path == "srv/private/uploads",
                  "保存 location upload_path");
    runner.expect(loc.cgi_extensions.size() == 1 &&
                      loc.cgi_extensions.find(".py") != loc.cgi_extensions.end() &&
                      loc.cgi_extensions.find(".py")->second == "/usr/bin/python3",
                  "保存 CGI 扩展名与解释器映射");
    runner.expect(loc.redirect_status == 302 && loc.redirect_url == "/moved",
                  "保存 location return 重定向");
}

static void testValidGrammar(TestRunner &runner)
{
    expectValid(runner, "最小合法配置", oneServer(""));
    expectValid(runner, "紧凑格式无需逐行书写",
                "server{listen 8080;root srv/www;location /x/{index a.html b.html;}}\n");
    expectValid(runner, "空格、tab 与换行可混合",
                "server\t{\nlisten\t8080 ;\nroot\tsrv/www ;\n}\n");
    expectValid(runner, "行尾注释被忽略", oneServer("index index.html; # comment ; { }\n"));
    expectValid(runner, "注释中的单双引号不会触发 quoted-string 错误",
                "# comments may contain \"double\" and 'single' quotes\n" + oneServer(""));
    expectValid(runner, "同一个配置可包含多个 server",
                "server { listen 8001; root a; }\nserver { listen 8002; root b; }\n");
    expectValid(runner, "相同 server_name 在不同端口允许复用",
                "server { listen 8001; server_name same.test; root a; }\n"
                "server { listen 8002; server_name same.test; root b; }\n");
    expectValid(runner, "不同 server 可使用相同 location path",
                "server { listen 8001; root a; location /x/ { index a; } }\n"
                "server { listen 8002; root b; location /x/ { index b; } }\n");
    expectValid(runner, "listen 只写端口", oneServer(""));
    expectValid(runner, "listen 只写 IPv4 时采用默认端口",
                "server { listen 127.0.0.1; root srv/www; }\n");
    expectValid(runner, "listen 支持 IPv4:port",
                "server { listen 0.0.0.0:65535; root srv/www; }\n");
    expectValid(runner, "max_body_size 接受无单位数字",
                oneServer("max_body_size 1024;"));
    expectValid(runner, "max_body_size 接受 K 单位",
                oneServer("max_body_size 2K;"));
    expectValid(runner, "max_body_size 接受小写 k 单位并转大写处理",
                oneServer("max_body_size 2k;"));
    expectValid(runner, "max_body_size 接受 M 单位",
                oneServer("max_body_size 2M;"));
    expectValid(runner, "max_body_size 接受 G 单位",
                oneServer("max_body_size 1G;"));
    expectValid(runner, "max_body_size 接受零",
                oneServer("max_body_size 0;"));
    expectValid(runner, "allow_methods 方法名可写小写",
                oneServer("allow_methods get post delete;"));
    expectValid(runner, "重复方法由 set 去重",
                oneServer("allow_methods GET GET POST;"));
    expectValid(runner, "server autoindex on 合法",
                oneServer("autoindex on;"));
    expectValid(runner, "server autoindex off 合法",
                oneServer("autoindex off;"));
    expectValid(runner, "location 可以继承 server root",
                oneLocation("allow_methods GET;"));
    expectValid(runner, "location 可以单独覆盖 root",
                oneLocation("root srv/other;"));
    expectValid(runner, "location 可以使用 alias",
                oneLocation("alias srv/alias;"));
    expectValid(runner, "return 接受 300",
                oneLocation("return 300 /target;"));
    expectValid(runner, "return 接受 399",
                oneLocation("return 399 /target;"));
}

static void testInvalidTopLevelAndBlocks(TestRunner &runner)
{
    expectInvalid(runner, "空配置文件被拒绝", "");
    expectInvalid(runner, "只有注释的配置被拒绝", "# no server here\n");
    expectInvalid(runner, "不支持 http 顶层包装 block",
                  "http { server { listen 8080; root srv/www; } }\n");
    expectInvalid(runner, "顶层普通 directive 被拒绝",
                  "root srv/www;\n" + oneServer(""));
    expectInvalid(runner, "顶层 location 被拒绝",
                  "location / { root srv/www; }\n");
    expectInvalid(runner, "顶层多余右大括号被拒绝",
                  oneServer("") + "}\n");
    expectInvalid(runner, "server 后缺少左大括号",
                  "server listen 8080; root srv/www; }\n");
    expectInvalid(runner, "server block 缺少右大括号",
                  "server { listen 8080; root srv/www;\n");
    expectInvalid(runner, "server 内嵌套 server 被拒绝",
                  "server { listen 8080; root a; server { listen 8081; root b; } }\n");
    expectInvalid(runner, "location 内嵌套 location 被拒绝",
                  "server { listen 8080; root a; location /a/ { location /b/ { index x; } } }\n");
    expectInvalid(runner, "location 内嵌套 server 被拒绝",
                  "server { listen 8080; root a; location /a/ { server { root b; } } }\n");
    expectInvalid(runner, "server 内孤立左大括号被拒绝",
                  "server { listen 8080; root a; { index x; } }\n");
    expectInvalid(runner, "location 内孤立左大括号被拒绝",
                  "server { listen 8080; root a; location /a/ { { index x; } } }\n");
    expectInvalid(runner, "普通 directive 缺少分号",
                  "server { listen 8080 root srv/www; }\n");
    expectInvalid(runner, "文件结束前 directive 缺少分号",
                  "server { listen 8080; root srv/www\n");
    expectInvalid(runner, "孤立分号被拒绝",
                  "server { listen 8080; root srv/www; ; }\n");
    expectInvalid(runner, "directive 只有名称没有参数被拒绝",
                  "server { listen 8080; root; }\n");
    expectInvalid(runner, "location 后缺少 path",
                  "server { listen 8080; root a; location { index x; } }\n");
    expectInvalid(runner, "location path 后缺少左大括号",
                  "server { listen 8080; root a; location /x/ index x; }\n");
    expectInvalid(runner, "location block 缺少右大括号",
                  "server { listen 8080; root a; location /x/ { index x; }\n");
    expectInvalid(runner, "同一 server 内重复 location path 被拒绝",
                  "server { listen 8080; root a; location /x/ { index a; } location /x/ { index b; } }\n");
    expectInvalid(runner, "location path 必须以斜杠开头",
                  "server { listen 8080; root a; location relative { index x; } }\n");
    expectInvalid(runner, "配置正文不支持双引号",
                  "server { listen 8080; root \"srv/www\"; }\n");
    expectInvalid(runner, "配置正文不支持单引号",
                  "server { listen 8080; root 'srv/www'; }\n");
}

static void testInvalidServerDirectives(TestRunner &runner)
{
    expectInvalid(runner, "未知 server directive 被拒绝",
                  oneServer("unknown_directive value;"));
    expectInvalid(runner, "旧 client_max_body_size 指令被拒绝",
                  oneServer("client_max_body_size 1M;"));
    expectInvalid(runner, "旧 directory_listing 别名被拒绝",
                  oneServer("directory_listing on;"));

    expectInvalid(runner, "listen 缺少参数",
                  "server { listen; root a; }\n");
    expectInvalid(runner, "listen 参数过多",
                  "server { listen 127.0.0.1 8080; root a; }\n");
    expectInvalid(runner, "同一 server 重复 listen",
                  "server { listen 8000; listen 8001; root a; }\n");
    expectInvalid(runner, "listen 端口不能为负数",
                  "server { listen -1; root a; }\n");
    expectInvalid(runner, "listen 端口不能带加号",
                  "server { listen +80; root a; }\n");
    expectInvalid(runner, "listen 端口不能为零",
                  "server { listen 0; root a; }\n");
    expectInvalid(runner, "listen 端口不能超过 65535",
                  "server { listen 65536; root a; }\n");
    expectInvalid(runner, "listen 端口必须是纯数字",
                  "server { listen 80x; root a; }\n");
    expectInvalid(runner, "IPv4 不能少于四段",
                  "server { listen 127.0.1:8080; root a; }\n");
    expectInvalid(runner, "IPv4 不能多于四段",
                  "server { listen 127.0.0.1.2:8080; root a; }\n");
    expectInvalid(runner, "IPv4 每段不能超过 255",
                  "server { listen 256.0.0.1:8080; root a; }\n");
    expectInvalid(runner, "IPv4 段不能为空",
                  "server { listen 127..0.1:8080; root a; }\n");
    expectInvalid(runner, "IPv4 不允许尾随点号",
                  "server { listen 127.0.0.1.:8080; root a; }\n");
    expectInvalid(runner, "IPv4 不允许前导零",
                  "server { listen 127.00.0.1:8080; root a; }\n");

    expectInvalid(runner, "server_name 缺少参数",
                  "server { listen 8080; server_name; root a; }\n");
    expectInvalid(runner, "同一 server 重复 server_name directive",
                  "server { listen 8080; server_name a; server_name b; root a; }\n");
    expectInvalid(runner, "同一条 server_name 中重复名称",
                  "server { listen 8080; server_name same same; root a; }\n");
    expectInvalid(runner, "同一端口的不同 server 不能使用相同 server_name",
                  "server { listen 8000; server_name same; root a; }\n"
                  "server { listen 8000; server_name same; root b; }\n");

    expectInvalid(runner, "server 缺少 root",
                  "server { listen 8080; index index.html; }\n");
    expectInvalid(runner, "root 缺少参数",
                  "server { listen 8080; root; }\n");
    expectInvalid(runner, "root 参数过多",
                  "server { listen 8080; root a b; }\n");
    expectInvalid(runner, "同一 server 重复 root",
                  "server { listen 8080; root a; root b; }\n");

    expectInvalid(runner, "index 缺少参数",
                  "server { listen 8080; root a; index; }\n");
    expectInvalid(runner, "同一 server 重复 index directive",
                  "server { listen 8080; root a; index a; index b; }\n");

    expectInvalid(runner, "autoindex 缺少参数",
                  "server { listen 8080; root a; autoindex; }\n");
    expectInvalid(runner, "autoindex 参数过多",
                  "server { listen 8080; root a; autoindex on off; }\n");
    expectInvalid(runner, "autoindex 只接受 on/off",
                  "server { listen 8080; root a; autoindex yes; }\n");
    expectInvalid(runner, "同一 server 重复 autoindex",
                  "server { listen 8080; root a; autoindex on; autoindex off; }\n");

    expectInvalid(runner, "allow_methods 缺少参数",
                  "server { listen 8080; root a; allow_methods; }\n");
    expectInvalid(runner, "allow_methods 拒绝未实现方法",
                  "server { listen 8080; root a; allow_methods GET PUT; }\n");

    expectInvalid(runner, "upload_path 缺少参数",
                  "server { listen 8080; root a; upload_path; }\n");
    expectInvalid(runner, "upload_path 参数过多",
                  "server { listen 8080; root a; upload_path a b; }\n");
    expectInvalid(runner, "同一 server 重复 upload_path",
                  "server { listen 8080; root a; upload_path one; upload_path two; }\n");

    expectInvalid(runner, "error_page 至少需要状态码和路径",
                  "server { listen 8080; root a; error_page 404; }\n");
    expectInvalid(runner, "error_page 状态码必须为纯数字",
                  "server { listen 8080; root a; error_page 4x4 error.html; }\n");
    expectInvalid(runner, "error_page 状态码不能低于 300",
                  "server { listen 8080; root a; error_page 299 error.html; }\n");
    expectInvalid(runner, "error_page 状态码不能高于 599",
                  "server { listen 8080; root a; error_page 600 error.html; }\n");

    expectInvalid(runner, "max_body_size 缺少参数",
                  "server { listen 8080; root a; max_body_size; }\n");
    expectInvalid(runner, "max_body_size 参数过多",
                  "server { listen 8080; root a; max_body_size 1 M; }\n");
    expectInvalid(runner, "同一 server 重复 max_body_size",
                  "server { listen 8080; root a; max_body_size 1K; max_body_size 2K; }\n");
    expectInvalid(runner, "max_body_size 不能为负数",
                  "server { listen 8080; root a; max_body_size -1; }\n");
    expectInvalid(runner, "max_body_size 不能带加号",
                  "server { listen 8080; root a; max_body_size +1; }\n");
    expectInvalid(runner, "max_body_size 不能是小数",
                  "server { listen 8080; root a; max_body_size 1.5M; }\n");
    expectInvalid(runner, "max_body_size 不能只有单位",
                  "server { listen 8080; root a; max_body_size M; }\n");
    expectInvalid(runner, "max_body_size 拒绝未知单位",
                  "server { listen 8080; root a; max_body_size 1T; }\n");
    expectInvalid(runner, "max_body_size 拒绝双字符单位",
                  "server { listen 8080; root a; max_body_size 1MB; }\n");
    expectInvalid(runner, "max_body_size 拒绝超出 unsigned long 的数字",
                  "server { listen 8080; root a; max_body_size 999999999999999999999999999999999999; }\n");
    expectInvalid(runner, "max_body_size 单位乘法溢出被拒绝",
                  "server { listen 8080; root a; max_body_size 18446744073709551615G; }\n");
}

static void testInvalidLocationDirectives(TestRunner &runner)
{
    expectInvalid(runner, "未知 location directive 被拒绝",
                  oneLocation("unknown value;"));
    expectInvalid(runner, "location 不支持旧 directory_listing 别名",
                  oneLocation("directory_listing on;"));

    expectInvalid(runner, "location root 缺少参数",
                  oneLocation("root;"));
    expectInvalid(runner, "location root 参数过多",
                  oneLocation("root a b;"));
    expectInvalid(runner, "location 重复 root",
                  oneLocation("root a; root b;"));
    expectInvalid(runner, "location root 与 alias 不能同时存在",
                  oneLocation("root a; alias b;"));
    expectInvalid(runner, "location alias 与 root 不能同时存在（反向顺序）",
                  oneLocation("alias b; root a;"));

    expectInvalid(runner, "alias 缺少参数",
                  oneLocation("alias;"));
    expectInvalid(runner, "alias 参数过多",
                  oneLocation("alias a b;"));
    expectInvalid(runner, "location 重复 alias",
                  oneLocation("alias a; alias b;"));

    expectInvalid(runner, "location index 缺少参数",
                  oneLocation("index;"));
    expectInvalid(runner, "location 重复 index directive",
                  oneLocation("index a; index b;"));

    expectInvalid(runner, "location autoindex 缺少参数",
                  oneLocation("autoindex;"));
    expectInvalid(runner, "location autoindex 参数过多",
                  oneLocation("autoindex on off;"));
    expectInvalid(runner, "location autoindex 只接受 on/off",
                  oneLocation("autoindex yes;"));
    expectInvalid(runner, "location 重复 autoindex",
                  oneLocation("autoindex on; autoindex off;"));

    expectInvalid(runner, "location allow_methods 缺少参数",
                  oneLocation("allow_methods;"));
    expectInvalid(runner, "location allow_methods 拒绝 PUT",
                  oneLocation("allow_methods GET PUT;"));

    expectInvalid(runner, "location upload_path 缺少参数",
                  oneLocation("upload_path;"));
    expectInvalid(runner, "location upload_path 参数过多",
                  oneLocation("upload_path a b;"));
    expectInvalid(runner, "location 重复 upload_path",
                  oneLocation("upload_path a; upload_path b;"));

    expectInvalid(runner, "location max_body_size 缺少参数",
                  oneLocation("max_body_size;"));
    expectInvalid(runner, "location max_body_size 参数过多",
                  oneLocation("max_body_size 1 M;"));
    expectInvalid(runner, "location 重复 max_body_size",
                  oneLocation("max_body_size 1K; max_body_size 2K;"));
    expectInvalid(runner, "location max_body_size 拒绝负数",
                  oneLocation("max_body_size -1;"));
    expectInvalid(runner, "location max_body_size 拒绝加号",
                  oneLocation("max_body_size +1;"));
    expectInvalid(runner, "location max_body_size 拒绝非法单位",
                  oneLocation("max_body_size 1MB;"));

    expectInvalid(runner, "cgi_extension 缺少参数",
                  oneLocation("cgi_extension .py;"));
    expectInvalid(runner, "cgi_extension 参数过多",
                  oneLocation("cgi_extension .py /python extra;"));
    expectInvalid(runner, "cgi_extension 扩展名必须以点开头",
                  oneLocation("cgi_extension py /python;"));
    expectInvalid(runner, "同一 location 重复 CGI 扩展名",
                  oneLocation("cgi_extension .py /python1; cgi_extension .py /python2;"));

    expectInvalid(runner, "return 缺少 URL",
                  oneLocation("return 302;"));
    expectInvalid(runner, "return 参数过多",
                  oneLocation("return 302 /a extra;"));
    expectInvalid(runner, "return 状态码必须为纯数字",
                  oneLocation("return 3x2 /a;"));
    expectInvalid(runner, "return 状态码不能低于 300",
                  oneLocation("return 299 /a;"));
    expectInvalid(runner, "return 状态码不能高于 399",
                  oneLocation("return 400 /a;"));
}

static void testRouteUtils(TestRunner &runner)
{
    const std::string config =
        "server {\n"
        "listen 8080;\n"
        "root srv/www;\n"
        "max_body_size 10K;\n"
        "location /upload/ { max_body_size 20K; }\n"
        "location /upload/images/ { max_body_size 3K; }\n"
        "location /inherit/ { allow_methods GET; }\n"
        "}\n";

    ParseResult result = parseConfigText(config);
    runner.expect(result.ok, "路由工具测试配置可解析", result.diagnostics);
    if (!result.ok || result.servers.empty())
        return;

    const ServerConfig &server = result.servers[0];
    bool use_location = false;
    LocationConfig *loc = findMatchingLocation("/upload/file.txt", server.locations, use_location);
    runner.expect(use_location && loc != NULL && loc->path == "/upload/",
                  "findMatchingLocation 找到普通前缀");

    use_location = false;
    loc = findMatchingLocation("/upload/images/a.png", server.locations, use_location);
    runner.expect(use_location && loc != NULL && loc->path == "/upload/images/",
                  "findMatchingLocation 选择最长前缀");

    use_location = false;
    loc = findMatchingLocation("/upload/images/a.png?size=small", server.locations, use_location);
    runner.expect(use_location && loc != NULL && loc->path == "/upload/images/",
                  "location 匹配忽略 query string");

    use_location = true;
    loc = findMatchingLocation("/other/a", server.locations, use_location);
    runner.expect(!use_location && loc == NULL,
                  "没有匹配 location 时返回 NULL 并清除标志");

    runner.expect(getEffectiveBodyLimit(&server, "/upload/a") == 20UL * 1024UL,
                  "location 明确覆盖 server body limit");
    runner.expect(getEffectiveBodyLimit(&server, "/upload/images/a") == 3UL * 1024UL,
                  "body limit 使用最长匹配 location");
    runner.expect(getEffectiveBodyLimit(&server, "/inherit/a") == 10UL * 1024UL,
                  "location 未写 body size 时继承 server");
    runner.expect(getEffectiveBodyLimit(&server, "/other/a") == 10UL * 1024UL,
                  "无 location 时使用 server body limit");
    runner.expect(getEffectiveBodyLimit(NULL, "/anything") == MAX_BODY_SIZE,
                  "server 指针为空时返回安全默认 body limit");
}

static void testObjectCopySemantics(TestRunner &runner)
{
    LocationConfig original_loc;
    original_loc.path = "/x/";
    original_loc.root = "root-x";
    original_loc.has_root = true;
    original_loc.autoindex = true;
    original_loc.has_autoindex = true;
    original_loc.index.push_back("a.html");
    original_loc.allow_methods.insert("GET");
    original_loc.cgi_extensions[".py"] = "/python";
    original_loc.upload_path = "uploads";
    original_loc.redirect_status = 302;
    original_loc.redirect_url = "/moved";
    original_loc.max_body_size = 42;
    original_loc.has_body_size = true;

    LocationConfig copied_loc(original_loc);
    runner.expect(copied_loc.path == original_loc.path &&
                      copied_loc.root == original_loc.root &&
                      copied_loc.has_root == original_loc.has_root &&
                      copied_loc.autoindex == original_loc.autoindex &&
                      copied_loc.has_autoindex == original_loc.has_autoindex &&
                      copied_loc.index == original_loc.index &&
                      copied_loc.allow_methods == original_loc.allow_methods &&
                      copied_loc.cgi_extensions == original_loc.cgi_extensions &&
                      copied_loc.upload_path == original_loc.upload_path &&
                      copied_loc.redirect_status == original_loc.redirect_status &&
                      copied_loc.redirect_url == original_loc.redirect_url &&
                      copied_loc.max_body_size == original_loc.max_body_size &&
                      copied_loc.has_body_size == original_loc.has_body_size,
                  "LocationConfig 拷贝构造复制全部配置字段");

    LocationConfig assigned_loc;
    assigned_loc = original_loc;
    runner.expect(assigned_loc.path == original_loc.path &&
                      assigned_loc.root == original_loc.root &&
                      assigned_loc.index == original_loc.index &&
                      assigned_loc.cgi_extensions == original_loc.cgi_extensions &&
                      assigned_loc.max_body_size == original_loc.max_body_size &&
                      assigned_loc.has_body_size == original_loc.has_body_size,
                  "LocationConfig 赋值运算复制关键字段");

    ServerConfig original_server;
    original_server.port = 8123;
    original_server.host = "127.0.0.1";
    original_server.server_names.push_back("copy.test");
    original_server.root = "srv/copy";
    original_server.has_root = true;
    original_server.index.push_back("index.html");
    original_server.allow_methods.insert("GET");
    original_server.error_pages[404] = "404.html";
    original_server.max_body_size = 1234;
    original_server.has_body_size = true;
    original_server.locations.push_back(original_loc);
    original_server.socketFd = 99;

    ServerConfig copied_server(original_server);
    runner.expect(copied_server.port == original_server.port &&
                      copied_server.host == original_server.host &&
                      copied_server.server_names == original_server.server_names &&
                      copied_server.root == original_server.root &&
                      copied_server.index == original_server.index &&
                      copied_server.allow_methods == original_server.allow_methods &&
                      copied_server.error_pages == original_server.error_pages &&
                      copied_server.max_body_size == original_server.max_body_size &&
                      copied_server.has_body_size == original_server.has_body_size &&
                      copied_server.locations.size() == 1,
                  "ServerConfig 拷贝构造复制配置字段");
    runner.expect(copied_server.socketFd == -1,
                  "ServerConfig 拷贝构造不复制 socketFd 所有权");

    ServerConfig assigned_server;
    assigned_server = original_server;
    runner.expect(assigned_server.port == original_server.port &&
                      assigned_server.root == original_server.root &&
                      assigned_server.locations.size() == 1 &&
                      assigned_server.max_body_size == original_server.max_body_size,
                  "ServerConfig 赋值运算复制配置字段");
    runner.expect(assigned_server.socketFd == -1,
                  "ServerConfig 赋值运算不复制 socketFd 所有权");

    original_server.socketFd = -1;
}

static bool prepareTempDirectory()
{
    std::ostringstream path;
    path << "/tmp/webserv_config_module_test_" << static_cast<long>(getpid());
    g_tmp_dir = path.str();
    if (mkdir(g_tmp_dir.c_str(), 0700) == 0)
        return true;
    return errno == EEXIST;
}

static void cleanupTempDirectory()
{
    rmdir(g_tmp_dir.c_str());
}

int main()
{
    if (!prepareTempDirectory())
    {
        std::cerr << "Cannot create temporary test directory: " << g_tmp_dir << std::endl;
        return 2;
    }

    TestRunner runner;

    std::cout << "=== Config strict module tests ===" << std::endl;
    testValidStructureAndStorage(runner);
    testValidGrammar(runner);
    testInvalidTopLevelAndBlocks(runner);
    testInvalidServerDirectives(runner);
    testInvalidLocationDirectives(runner);
    testRouteUtils(runner);
    testObjectCopySemantics(runner);

    cleanupTempDirectory();

    std::cout << "==================================" << std::endl;
    std::cout << "TOTAL: " << runner.total() << std::endl;
    std::cout << "PASS : " << runner.passed() << std::endl;
    std::cout << "FAIL : " << runner.failed() << std::endl;

    return runner.failed() == 0 ? 0 : 1;
}
