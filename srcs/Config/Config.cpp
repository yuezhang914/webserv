/*
文件：srcs/Config/Config.cpp
配置对象基础工具实现。这个文件负责 Config 构造、析构、字符串 trim/split 等底层辅助逻辑；真正的配置语法解析在 ConfigParser.cpp；server/location 指令含义分别在 ServerConfig.cpp、LocationConfig.cpp。
*/
#include "Webserv.hpp"
/*
函数：Config::Config
用途：启动时读取配置文件并建立内存中的配置对象。
参数来源：path 来自 main()，通常是命令行 ./webserv default.conf 的 argv[1]；如果用户没传，main 会用 "default.conf"。
变量解释：
    - path：main 传入的配置文件路径，例如 default.conf。
    - error：Config 成员变量，记录配置是否有错误；0 表示正常，1 表示错误。
    - servers：parseFile 成功后填充的 server 配置列表。
实现逻辑：
    1. 先把 error 设为 0，表示暂时没有错误。
    2. 调用 parseFile(path)，读取并解析配置文件，把结果放进 servers。
    3. 如果 parseFile 返回 ERROR，说明文件打不开、语法错误或指令非法，打印错误并设置 error=1。
    4. 如果解析成功，再调用 serversHaveRoot()，确认每个 server 都有 root。
    5. 如果缺少 root，也设置 error=1，因为后面无法把 URI 映射到真实文件路径。
后续影响：main() 会检查 config.error；只有没有错误才会继续 setupSockets() 和 serverLoop()。
*/
Config::Config(const std::string &path)
    : all_server_names(), error(0)
{
    // 读取配置文件，先切分 token，再按 server/location 结构解析，
    // 最终把 ServerConfig 和 LocationConfig 保存到 Config::servers。
    if (parseFile(path) == ERROR)
    {
        std::cerr << "Error: Failed to parse config file" << std::endl;
        this->error = 1;
        return;
    }

    // 第三步：
    // 对已经生成的所有 ServerConfig 做整体校验。
    if (serversHaveRoot() == ERROR)
    {
        std::cerr << "Error: A server is missing the root directive" << std::endl;
        this->error = 1;
    }
}
/*
函数：Config::~Config
用途：销毁 Config 对象。
变量解释：
    - servers：Config 成员，会由 vector 自动析构。
    - error：普通 bool/int 标志，不需要额外释放。
实现逻辑：
    1. Config 自己没有手动 new 的内存，也没有自己打开的 fd。
    2. vector/string/map/set 会自动析构。
    3. ServerConfig 的析构函数会负责关闭自己的 socketFd。
*/
Config::~Config() 
{}

std::vector<ServerConfig> &Config::getServers()
{
    return servers;
}

const std::vector<ServerConfig> &Config::getServers() const
{
    return servers;
}

/*
函数：Config::serversHaveRoot
用途：检查每个 server 是否有 root 指令。
变量解释：
    - servers：Config 的成员变量，保存所有已经解析出的 ServerConfig。
    - it：遍历 servers 的 const_iterator。
    - it->has_root：每个 server 是否显式配置过 root 的标志。
实现逻辑：
    1. 遍历 servers。
    2. 如果某个 ServerConfig.has_root 是 false，说明没有配置 root，返回 ERROR。
    3. 所有 server 都有 root，返回 SUCCESS。
为什么重要：没有 root 时，Webserv 无法把 /ping.html 映射成 srv/www/ping.html 这种真实文件路径。
*/
bool Config::serversHaveRoot() const
{
    for (std::vector<ServerConfig>::const_iterator it = servers.begin(); it != servers.end(); ++it)
    {
        if (it->has_root == false)
            return ERROR;
    }
    return SUCCESS;
}



/*
函数：Config::trim
用途：清理字符串两端的空白字符。
参数来源：split() 拆分 listen 参数时，把每一段传入本函数。
变量解释：
    - str：待清理的输入字符串。
    - first：第一个非空白字符的位置。
    - last：最后一个非空白字符的位置。
    - 返回值：str 去掉首尾空格、tab、回车和换行后的结果。
实现逻辑：
    1. find_first_not_of 找到第一个非空白字符。
    2. 如果整段都是空白，返回空字符串。
    3. find_last_not_of 找到最后一个非空白字符。
    4. substr 截取中间的有效内容。
说明：分号已经由 tokenizeConfig 单独拆成 token，这里不再兼容或删除分号。
*/
std::string Config::trim(const std::string &str) const
{
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, last - first + 1);
}

/*
函数：Config::split
用途：按指定分隔符拆分字符串。
参数来源：listen 指令中判断 IP 是否像 IPv4 时会用 split(value, '.')；配置主 parser 已改由 tokenizeConfig 负责。
变量解释：
    - str：待拆分的字符串，例如 127.0.0.1。
    - delimiter：分隔符，例如 .。
    - tokens：返回用的数组，保存拆分出的非空片段。
    - ss：包装 str 的 stringstream。
    - token：getline 每次读出的一个片段，trim 后再放入 tokens。
实现逻辑：
    1. 用 stringstream 包装输入字符串。
    2. 用 getline 按 delimiter 读取每一段。
    3. 每一段先 trim，只去掉首尾空白。
    4. 如果 token 非空，就 push_back 到 tokens。
    5. 返回 tokens 给 parseDirective。
例子："listen 127.0.0.1:3435;" 会变成 ["listen", "127.0.0.1:3435"]。
*/
std::vector<std::string> Config::split(const std::string &str, char delimiter) const
{
    std::vector<std::string> tokens;
    std::stringstream ss(str);
    std::string token;

    while (std::getline(ss, token, delimiter))
    {
        token = trim(token);
        if (!token.empty())
        {
            tokens.push_back(token);
        }
    }
    return (tokens);
}