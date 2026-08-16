/*
文件：srcs/Config/Config.cpp
配置对象基础工具实现。这个文件负责 Config 构造、析构、字符串 trim/split 等底层辅助逻辑；真正的配置语法解析在 ConfigParser.cpp；server/location 指令含义分别在 ServerConfig.cpp、LocationConfig.cpp。
*/
#include "Webserv.hpp"

/*
函数：isWildcardHost
用途：判断配置中的 host 是否代表所有 IPv4 接口。
参数来源：serversHaveUniqueListenPairs() 传入 ServerConfig.host。
实现逻辑：Config 统一用字符串 0.0.0.0 表示 IPv4 通配监听，因此只检查这个标准表示。
*/
static bool isWildcardHost(const std::string &host)
{
    return host == "0.0.0.0";
}
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
    4. 解析成功后不再强制每个 server 都有 server-level root；location 可以独立提供 root/alias。
    5. 调用 serversHaveUniqueListenPairs()，拒绝重复或会实际冲突的 interface:port。
    6. 请求真正需要文件路径时，由 EffectiveRoute 检查 location root、server root 是否可用。
    7. 任一整体校验失败都设置 error=1，main 不会创建监听 socket。
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

    // 不在 Config 阶段强制 server-level root。
    // location 可以独立提供 root/alias；真正处理请求时再由 EffectiveRoute
    // 判断当前请求是否能得到有效的文件系统基础路径。

    // 本项目不实现 virtual host。
    // 因此不同 server 不能共享同一个实际监听端点，
    // 否则请求会始终落到其中一个 ServerConfig。
    if (serversHaveUniqueListenPairs() == ERROR)
        this->error = 1;
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
函数：Config::serversHaveUniqueListenPairs
用途：拒绝多个 server 共享同一个实际监听端点。
变量解释：i/j 两两比较 servers；left/right 是当前两个配置。
实现逻辑：
    1. 端口不同不会冲突。
    2. 端口相同且 host 相同，属于重复 interface:port。
    3. 端口相同且任一 host 是通配地址，也会占用另一端点。
    4. 发现冲突输出两个端点并返回 ERROR；全部通过返回 SUCCESS。
为什么需要：本项目没有实现 virtual host，不能在共享监听 socket 后根据 Host header 选择 ServerConfig。
*/
bool Config::serversHaveUniqueListenPairs() const
{
    size_t i = 0;
    while (i < servers.size())
    {
        size_t j = i + 1;
        while (j < servers.size())
        {
            const ServerConfig &left = servers[i];
            const ServerConfig &right = servers[j];
            if (left.port == right.port
                && (left.host == right.host
                    || isWildcardHost(left.host)
                    || isWildcardHost(right.host)))
            {
                std::cerr << "Error: Conflicting listen endpoints: "
                          << left.host << ":" << left.port << " and "
                          << right.host << ":" << right.port << std::endl;
                return ERROR;
            }
            ++j;
        }
        ++i;
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