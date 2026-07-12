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

/*
结构体：ParseResult
用途：
    保存一次配置解析测试的完整结果，便于测试函数同时检查解析状态、
    生成的 ServerConfig 列表和错误输出。
成员：
    - ok：Config 是否成功解析；true 表示 config.error 为 0。
    - servers：解析后生成的全部 ServerConfig 副本。
    - diagnostics：解析过程中捕获到的标准输出和错误输出。
*/
struct ParseResult
{
    bool ok;
    std::vector<ServerConfig> servers;
    std::string diagnostics;
};

/*
类：TestRunner
用途：
    统一记录并输出模块测试结果。
成员：
    - _passed：已经通过的断言数量。
    - _failed：已经失败的断言数量。
实现逻辑：
    每次调用 expect() 都根据 condition 增加通过或失败计数，
    main() 最后通过这些计数打印汇总并决定程序退出状态。
*/
class TestRunner
{
private:
    int _passed;
    int _failed;

public:
    /*
    函数：TestRunner::TestRunner
    用途：创建一个尚未执行任何测试的测试记录器。
    实现逻辑：把通过数和失败数都初始化为 0。
    */
    TestRunner() : _passed(0), _failed(0) {}

    /*
    函数：TestRunner::expect
    用途：
        检查一项测试条件，输出 [PASS] 或 [FAIL]，并更新计数。
    参数来源：
        - condition：各测试函数计算出的断言结果。
        - name：当前测试项的中文名称。
        - details：失败时附加显示的诊断信息；默认空字符串。
    实现逻辑：
        condition 为 true 时增加 _passed；否则增加 _failed，
        并在 details 非空时把失败原因输出在测试名称后面。
    */
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

    /*
    函数：TestRunner::passed
    用途：返回当前已通过的测试数量。
    返回值：_passed。
    */
    int passed() const { return _passed; }
    /*
    函数：TestRunner::failed
    用途：返回当前已失败的测试数量。
    返回值：_failed。
    */
    int failed() const { return _failed; }
    /*
    函数：TestRunner::total
    用途：返回当前已经执行的测试总数。
    返回值：通过数与失败数之和。
    */
    int total() const { return _passed + _failed; }
};

static std::string g_tmp_dir;
static int g_file_counter = 0;

/*
函数：makeTempPath
用途：
    为下一条测试配置生成一个不会与前面测试重复的临时文件路径。
变量来源：
    - g_tmp_dir：prepareTempDirectory() 创建的本次测试专用目录。
    - g_file_counter：全局递增编号，每生成一个路径后加 1。
    - path：用来拼接目录、文件编号和 .conf 后缀。
返回值：
    形如 /tmp/webserv_config_module_test_PID/case_0.conf 的路径。
*/
static std::string makeTempPath()
{
    std::ostringstream path;
    path << g_tmp_dir << "/case_" << g_file_counter++ << ".conf";
    return path.str();
}

/*
函数：writeFile
用途：
    把一段测试配置文本写入指定临时文件，供 Config 按真实文件方式解析。
参数来源：
    - path：makeTempPath() 生成的临时配置文件路径。
    - content：某条测试构造出的配置文本。
变量解释：
    - file：以二进制输出模式打开的 ofstream。
    - ok：write 完成后文件流是否仍处于正常状态。
返回值：
    true 表示文件成功打开并写完；false 表示创建或写入失败。
*/
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

/*
函数：parseConfigText
用途：
    把内存中的配置字符串写成临时 .conf 文件，再调用真实 Config
    构造函数解析，并收集解析结果和错误信息。
参数来源：
    - content：各测试函数传入的完整配置文本。
变量解释：
    - result：保存解析成功状态、servers 和诊断输出。
    - path：本次测试使用的临时文件路径。
    - captured：临时接收 std::cout 和 std::cerr 的输出。
    - old_cerr / old_cout：保存原来的输出缓冲区，测试结束后恢复。
实现逻辑：
    1. 创建临时配置文件并写入 content。
    2. 临时把标准输出和错误输出重定向到 captured。
    3. 构造 Config，让项目真实 parser 读取该文件。
    4. 保存 config.error 和 config.getServers()。
    5. 恢复输出流，删除临时文件并返回 ParseResult。
*/
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

/*
函数：oneServer
用途：
    生成一段带有合法 listen 和 root 的基础 server 配置，
    并把调用者给出的额外指令插入 server 内部。
参数来源：
    - extra：某项测试需要追加的 server 指令或 location block。
返回值：
    可直接交给 parseConfigText() 的完整 server 配置字符串。
*/
static std::string oneServer(const std::string &extra)
{
    return "server {\n"
           "listen 8080;\n"
           "root srv/www;\n" +
           extra +
           "\n}\n";
}

/*
函数：oneLocation
用途：
    生成一个合法 server，并在其中创建 location /test/，
    再把调用者给出的指令插入该 location。
参数来源：
    - extra：某项 location 测试需要检查的指令文本。
返回值：
    包含一个 server 和一个 location 的完整配置字符串。
*/
static std::string oneLocation(const std::string &extra)
{
    return oneServer("location /test/ {\n" + extra + "\n}\n");
}

/*
函数：expectValid
用途：
    验证一段应该合法的配置确实被 Config 接受。
参数来源：
    - runner：main() 中创建的统一测试记录器。
    - name：当前测试项名称。
    - config：预期可以成功解析的配置文本。
实现逻辑：
    调用 parseConfigText()；result.ok 为 true 时通过，
    否则把 parser 输出的 diagnostics 作为失败详情。
*/
static void expectValid(TestRunner &runner, const std::string &name,
                        const std::string &config)
{
    ParseResult result = parseConfigText(config);
    runner.expect(result.ok, name,
                  result.ok ? "" : result.diagnostics);
}

/*
函数：expectInvalid
用途：
    验证一段应该非法的配置确实被 Config 拒绝。
参数来源：
    - runner：main() 中创建的统一测试记录器。
    - name：当前测试项名称。
    - config：预期解析失败的配置文本。
实现逻辑：
    调用 parseConfigText()；result.ok 为 false 时通过；
    如果 parser 错误接受了该配置，则记录失败。
*/
static void expectInvalid(TestRunner &runner, const std::string &name,
                          const std::string &config)
{
    ParseResult result = parseConfigText(config);
    runner.expect(!result.ok, name,
                  !result.ok ? "" : "parser accepted an invalid config");
}

/*
函数：testValidStructureAndStorage
用途：
    使用一份包含完整 server/location 指令的合法配置，
    检查 parser 不仅能成功解析，还把每个值保存到了正确字段。
参数来源：
    - runner：main() 创建的 TestRunner。
主要检查：
    listen、server_name、root、index、autoindex、max_body_size、
    allow_methods、upload_path、error_page、location、alias、
    CGI 映射和 return 重定向等字段。
实现逻辑：
    1. 构造完整配置并调用 parseConfigText()。
    2. 先确认成功生成一个 ServerConfig。
    3. 逐项检查 server 字段。
    4. 再检查其中唯一 LocationConfig 的字段。
*/
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

/*
函数：testValidGrammar
用途：
    集中验证当前 Config 规格明确支持的合法语法形式。
参数来源：
    - runner：main() 创建的 TestRunner。
覆盖内容：
    最小配置、紧凑格式、空白和注释、多 server、
    listen 的三种格式、body size 单位、方法名大小写、
    root/alias 使用以及合法重定向状态码。
实现逻辑：
    每种合法语法分别构造配置，并通过 expectValid() 检查。
*/
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

/*
函数：testInvalidTopLevelAndBlocks
用途：
    验证顶层结构、server/location block 和分号规则中的非法情况
    都会在 Config 阶段被拒绝。
参数来源：
    - runner：main() 创建的 TestRunner。
覆盖内容：
    空配置、http 外层、顶层 directive、缺失或多余大括号、
    block 嵌套、孤立符号、缺少分号、非法 location 开头及引号。
实现逻辑：
    每个错误场景分别构造配置，并通过 expectInvalid() 检查。
*/
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

/*
函数：testInvalidServerDirectives
用途：
    验证 server 级指令的参数数量、格式、取值范围和重复规则。
参数来源：
    - runner：main() 创建的 TestRunner。
覆盖内容：
    未知指令、listen/IP/端口、server_name、root、index、
    autoindex、allow_methods、upload_path、error_page 和
    max_body_size 的严格错误场景。
实现逻辑：
    针对每一类非法 server 指令生成独立配置，
    通过 expectInvalid() 确认 parser 拒绝它。
*/
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

/*
函数：testInvalidLocationDirectives
用途：
    验证 location 级指令的参数数量、互斥关系、重复规则和取值范围。
参数来源：
    - runner：main() 创建的 TestRunner。
覆盖内容：
    root/alias 互斥、index、autoindex、allow_methods、
    upload_path、max_body_size、cgi_extension 和 return。
实现逻辑：
    使用 oneLocation() 构造每个错误场景，
    再通过 expectInvalid() 检查 Config 是否正确拒绝。
*/
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

/*
函数：testRouteUtils
用途：
    验证 ConfigRouteUtils 中 location 最长前缀匹配和 body 限制继承逻辑。
参数来源：
    - runner：main() 创建的 TestRunner。
变量解释：
    - config：包含多个相交 location 的测试配置。
    - server：解析得到的唯一 ServerConfig。
    - use_location：findMatchingLocation() 的输出标志。
    - loc：当前 URI 匹配到的 LocationConfig 指针。
主要检查：
    普通匹配、最长匹配、忽略 query string、无匹配返回 NULL，
    以及 location body size 覆盖或继承 server 限制。
*/
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

/*
函数：testObjectCopySemantics
用途：
    验证 LocationConfig 和 ServerConfig 的拷贝构造、赋值运算符
    是否完整复制普通配置，同时不复制 socket fd 所有权。
参数来源：
    - runner：main() 创建的 TestRunner。
变量解释：
    - original_loc / original_server：预先填充字段的源对象。
    - copied_loc / copied_server：通过拷贝构造得到的对象。
    - assigned_loc / assigned_server：通过 operator= 得到的对象。
实现逻辑：
    1. 填充原始 location 并检查拷贝与赋值结果。
    2. 填充原始 server 并检查普通字段。
    3. 确认 copied_server 和 assigned_server 的 socketFd 都为 -1，
       防止两个对象重复拥有并关闭同一个 fd。
*/
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

/*
函数：prepareTempDirectory
用途：
    为本次 Config 模块测试创建一个独立临时目录。
变量解释：
    - path：使用当前进程 PID 拼出的目录路径。
    - g_tmp_dir：保存最终临时目录，供 makeTempPath() 使用。
实现逻辑：
    调用 mkdir() 创建权限为 0700 的目录；
    创建成功或目录已经存在都视为可用。
返回值：
    true 表示临时目录可用，false 表示创建失败。
*/
static bool prepareTempDirectory()
{
    std::ostringstream path;
    path << "/tmp/webserv_config_module_test_" << static_cast<long>(getpid());
    g_tmp_dir = path.str();
    if (mkdir(g_tmp_dir.c_str(), 0700) == 0)
        return true;
    return errno == EEXIST;
}

/*
函数：cleanupTempDirectory
用途：
    测试结束后删除 prepareTempDirectory() 创建的临时目录。
实现逻辑：
    所有临时配置文件已经由 parseConfigText() 单独删除，
    因此这里直接调用 rmdir() 删除空目录。
*/
static void cleanupTempDirectory()
{
    rmdir(g_tmp_dir.c_str());
}

/*
函数：main
用途：
    Config 严格模块测试的程序入口，按固定顺序执行全部测试组并输出汇总。
实现逻辑：
    1. 创建本次测试使用的临时目录。
    2. 创建 TestRunner。
    3. 依次执行合法配置、非法配置、路由工具和拷贝语义测试。
    4. 清理临时目录。
    5. 输出 TOTAL、PASS 和 FAIL。
返回值：
    - 0：全部测试通过。
    - 1：至少一项测试失败。
    - 2：无法创建临时测试目录，测试未能开始。
*/
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