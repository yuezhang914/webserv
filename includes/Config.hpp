
#ifndef CONFIG_HPP
#define CONFIG_HPP



#include "ServerConfig.hpp"
#include "LocationConfig.hpp"
#include "Defines.hpp"
/*
结构体：ConfigToken
作用：保存配置文件词法分析后的一个 token 以及它来自哪一行。
为什么需要：健壮版 parser 不再按“每行一个指令”解析，而是先把整个文件拆成 token 流；line 用来在语法错误时报出更容易定位的位置。
*/
struct ConfigToken
{
    /* token 的文本值，例如 server、{、listen、127.0.0.1:3435、;、}。 */
    std::string value;
    /* token 在配置文件中的行号，从 1 开始，用于错误提示。 */
    size_t line;

    ConfigToken();
    ConfigToken(const std::string &token_value, size_t token_line);
};

/*
类：Config
作用：保存整个配置文件解析后的结果，是 default.conf 在 C++ 内存里的表示。
从哪来：main() 里执行 Config config(config_path) 时创建；config_path 通常来自 ./webserv default.conf 的 argv[1]。
给谁用：setupSockets() 用 servers 里的 host/port 创建监听 socket；serverLoop() 保存 servers 并在每次请求时交给 Router/Response/CGI 查规则。
核心理解：default.conf 是文本；Config 把文本变成 vector<ServerConfig>。程序运行后不反复读配置文件，而是一直查这个对象里的规则。
*/
class Config
{
private:
    /*
    成员：all_server_names
    含义：以端口为绝对地理边界、以域名为微观防伪原子的二维联合交叉索引账本（二维端口-域名隔离锁）。
    来源：parseServerBlock() 在服务器块解析收工、撞见右大括号 "}" 时，会将当前别墅的端口与域名集送审。
    用法：validateServerNameIsNew() 遍历并检索它，精准绝杀“同端口内域名打架抢夺句柄”的工业级违规配置，同时释放跨端口域名复用的最高虚拟主机（Virtual Hosting）可用性。
    */
    std::map<int, std::set<std::string> > all_server_names;
    /*
    成员：servers
    含义：配置文件里所有 server { ... } 块解析后的结果列表。
    来源：parseTokenStream() 读到 server block 时 push_back(ServerConfig())，之后把 listen/root/location 等指令填入当前 ServerConfig。
    用法：setupSockets() 遍历它创建 socket；serverLoop() 保存它；Response/CGI 通过 Request.config 找到当前连接对应的 ServerConfig。
    */
    std::vector<ServerConfig> servers;

    /*
    函数：trim
    输入：一段配置文本或一个 token。
    输出：去掉开头和结尾空格、tab、回车、换行后的字符串。
    实现逻辑：先找第一个非空白字符，再找最后一个非空白字符，最后 substr 截取中间部分；分号由 tokenizer 单独处理。
    */
    std::string trim(const std::string &str) const;

    /*
    函数：split
    输入：一段文本和分隔符，例如用点号拆分 "127.0.0.1"。
    输出：拆好的 token 数组，例如 ["127", "0", "0", "1"]。
    实现逻辑：用 stringstream 按 delimiter 逐段读取；每段先 trim；空段跳过；有效段 push 到 vector。
    */
    std::vector<std::string> split(const std::string &str, char delimiter) const;

    /*
    函数：parseSize
    输入：配置里的大小字符串，例如 "1M"、"512K"、"1024"。
    输出：换算后的字节数；格式非法时返回 ERROR_PARSE_SIZE。
    实现逻辑：严格检查纯数字和可选 K/M/G 单位；逐位转换并分别检查数字累积与单位乘法是否溢出。
    */
    unsigned long parseSize(const std::string &size_str) const;

    /*
    函数：parseDirective
    输入：parseDirectiveTokens 读出的一条 directive tokens，以及当前正在解析的 server/location 指针。
    输出：成功返回 SUCCESS，失败返回 ERROR。
    实现逻辑：先取 tokens[0] 作为指令名；tokens 后半段作为参数；如果当前在 location 中就分发给 parseLocationDirective；如果当前在 server 中就分发给 parseServerDirective；如果两者都没有，说明指令写在 server 外面，报错。
    */
    bool parseDirective(const std::vector<std::string> &tokens, ServerConfig *current_server, LocationConfig *current_location);

    /*
    函数：parseFile
    输入：配置文件路径。
    输出：把 servers 填好；成功返回 SUCCESS，失败返回 ERROR。
    实现逻辑：打开整个文件；调用 tokenizeConfig 拆成 token 流；再调用 parseTokenStream 按 server/location/block/directive 语法解析。
    */
    bool parseFile(const std::string &path);

    /*
    函数：tokenizeConfig
    输入：整个配置文件文本。
    输出：ConfigToken 数组；{、}、; 会被单独拆成 token，# 到行尾会被当作注释跳过。
    用途：让 parser 支持 server{、root srv/www;} 等排版；当前格式不支持单引号或双引号字符串，parseFile 会在 tokenize 前拒绝。
    */
    std::vector<ConfigToken> tokenizeConfig(const std::string &content) const;

    /*
    函数：parseTokenStream
    输入：tokenizeConfig 生成的完整 token 流。
    输出：把所有 server block 解析进 servers；语法错误返回 ERROR。
    实现逻辑：顶层只允许 server block；每个 server block 由 parseServerBlock 负责继续解析。
    */
    bool parseTokenStream(const std::vector<ConfigToken> &tokens);

    /*
    函数：parseServerBlock
    输入：tokens 和当前 index；index 指向 server 关键字。
    输出：创建并填充一个 ServerConfig；函数结束时 index 移到 server block 之后。
    实现逻辑：检查 server 后必须是 {；读取 server 内的 directive 和 location block；遇到 } 时关闭 server。
    */
    bool parseServerBlock(const std::vector<ConfigToken> &tokens, size_t &index, std::map<int, std::set<std::string> > &all_server_names);

    /*
    函数：parseLocationBlock
    输入：tokens、当前 index 和所属 server；index 指向 location 关键字。
    输出：创建并填充一个 LocationConfig；函数结束时 index 移到 location block 之后。
    实现逻辑：检查 location 后必须有 path 和 {；读取 location 内 directive；遇到 } 时关闭 location。
    */
    bool parseLocationBlock(const std::vector<ConfigToken> &tokens, size_t &index, ServerConfig &server, std::set<std::string> &current_location_paths);

    /*
    函数：parseDirectiveTokens
    输入：tokens 和当前 index；index 指向一条 directive 的名字。
    输出：把 directive 到下一个 ; 之间的内容转成普通 string tokens；函数结束时 index 移到 ; 后面。
    实现逻辑：directive 必须用 ; 结束；在 ; 前遇到 { 或 } 会报语法错误。
    */
    bool parseDirectiveTokens(const std::vector<ConfigToken> &tokens, size_t &index, std::vector<std::string> &directive_tokens) const;

    /*
    函数：validateServerNameIsNew
    输入：刚解析完的 server 和已经出现过的 server_name 集合。
    输出：server_name 不重复返回 SUCCESS；重复返回 ERROR。
    实现逻辑：server block 关闭时登记它的所有 server_name，防止不同 server 重名。
    */
    bool validateServerNameIsNew(ServerConfig &server, std::map<int, std::set<std::string> > &all_server_names) const;

    /*
    函数：parseServerDirective
    输入：server 块里的指令名、参数列表、当前 ServerConfig 指针。
    输出：修改 srv 对象；成功返回 SUCCESS，失败返回 ERROR。
    实现逻辑：根据 directive 分别处理 listen/server_name/root/error_page/max_body_size/index/allow_methods/upload_path/autoindex；每个分支检查参数个数和格式，再写入 ServerConfig 对应成员。当前规格支持 server 级 max_body_size，不支持 client_max_body_size。
    */
    bool parseServerDirective(const std::string &directive, const std::vector<std::string> &values, ServerConfig *srv);

    /*
    函数：parseLocationDirective
    输入：location 块里的指令名、参数列表、当前 LocationConfig 指针。
    输出：修改 loc 对象；成功返回 SUCCESS，失败返回 ERROR。
    实现逻辑：根据 directive 分别处理 allow_methods/root/alias/index/autoindex/cgi_extension/upload_path/return/max_body_size；检查 root 和 alias 不能同时使用；location 级 max_body_size 会被 RequestParser 通过 ConfigRouteUtils 真正使用。
    */
    bool parseLocationDirective(const std::string &directive, const std::vector<std::string> &values, LocationConfig *srv);

    /*
    函数：serversHaveRoot
    输入：无，检查当前 Config.servers。
    输出：所有 server 都有 root 返回 SUCCESS；任意 server 缺少 root 返回 ERROR。
    实现逻辑：遍历 servers，检查每个 ServerConfig.has_root。
    */
    bool serversHaveRoot() const;

public:
    /*
    构造函数：Config
    输入：配置文件路径，例如 "default.conf"。
    输出：构造出一个可被后续模块使用的 Config 对象。
    实现逻辑：先把 error 初始化为 false；调用 parseFile(path) 填充 servers；如果解析失败或缺少 root，就设置 error，main 会据此停止启动。
    */
    Config(const std::string &path);

    /*
    析构函数：~Config
    作用：Config 自身没有手动 new 出来的资源，所以这里不需要额外释放；servers 里的对象由 vector 自动析构。
    */
    virtual ~Config();

    /*
    函数：getServers 非 const 版本
    输出：servers 的可修改引用。
    用途：setupSockets() 需要给每个 ServerConfig 写入 socketFd，所以需要非 const 引用。
    */
    std::vector<ServerConfig> &getServers();

    /*
    函数：getServers const 版本
    输出：servers 的只读引用。
    用途：serverLoop 等只读取配置，不应该修改配置时使用。
    */
    const std::vector<ServerConfig> &getServers() const;

    /*
    成员：error
    含义：配置解析是否失败的标志。
    来源：构造函数中根据 parseFile() 和 serversHaveRoot() 的结果设置。
    用法：main() 检查 config.error；如果有错误就不进入 setupSockets/serverLoop。
    */
    bool error;
   void printConfig() const;
};

#endif