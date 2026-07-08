/*
文件：srcs/Config/Config.cpp
配置对象基础工具实现。这个文件负责 Config 构造、析构、字符串 trim/split 等底层辅助逻辑；真正的配置语法解析在 ConfigParser.cpp；server/location 指令含义分别在 ServerConfig.cpp、LocationConfig.cpp。
*/
#include "Config.hpp"
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
Config::Config(const std::string& path) 
    : all_server_names(), error(0) 
{
    std::vector<ConfigToken> tokens; // 👈 局部变量，临时的词法脚手架

    // 🟢 【第一步：词法切包】让 parseFile 把切好的词塞进局部变量 tokens 中
    if (parseFile(path) == ERROR)
    {
        std::cerr << "Error: Failed to read or tokenize config file" << std::endl;
        this->error = 1;
        return; 
    }

    // 🟢 【第三步：业务终审】进行全站最后的功能性宏观体检
    if (serversHaveRoot() == ERROR)
    {
        std::cerr << "Error: At least one server must have a root directive" << std::endl;
        this->error = 1;
    }
    // 🔒 离开构造函数，局部变量 tokens 的内存被系统自动回收，干净利落！
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
Config::~Config() {}

/*
函数：Config::trim
用途：清理字符串两端的空白字符和分号。
参数来源：split() 或旧辅助场景传入的文本；健壮版配置 parser 主要由 tokenizeConfig 处理 ;。
变量解释：
    - str：待清理的输入字符串。
    - first：第一个有效字符的位置。
    - last：最后一个有效字符的位置。
    - 返回值：str 去掉首尾空白和分号后的结果。
实现逻辑：
    1. find_first_not_of 找到第一个不是空格、tab、换行、分号的位置。
    2. 如果整段都没有有效字符，返回空字符串。
    3. find_last_not_of 找到最后一个不是空白/分号的位置。
    4. substr 截取有效内容。
例子："  root srv/www;  " 经过 trim 后变成 "root srv/www"。
*/
std::string Config::trim(const std::string& str) const {
	size_t first = str.find_first_not_of(" \t\n;");
	if (first == std::string::npos) return "";
	size_t last = str.find_last_not_of(" \t\n;");
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
    3. 每一段先 trim，去掉空白和末尾分号。
    4. 如果 token 非空，就 push_back 到 tokens。
    5. 返回 tokens 给 parseDirective。
例子："listen 127.0.0.1:3435;" 会变成 ["listen", "127.0.0.1:3435"]。
*/
std::vector<std::string> Config::split(const std::string& str, char delimiter) const {
	std::vector<std::string> tokens;
	std::stringstream ss(str);
	std::string token;

	while (std::getline(ss, token, delimiter)) {
		token = trim(token);
		if (!token.empty()) {
			tokens.push_back(token);
		}
	}
	return(tokens);
}

// srcs/Config/Config.cpp
#include "Config.hpp"
#include <iostream>

void Config::printConfig() const
{
    std::cout << "\n====================================================================" << std::endl;
    std::cout << "        🏆 WEBSERV CONFIGURATION TOPOLOGY DUMP (资产自检大阅兵)      " << std::endl;
    std::cout << "====================================================================" << std::endl;

    // 🟢 1. 打印私有核心资产：all_server_names 全局防伪大账本
    std::cout << "[📊 全局域名防伪锁状态]" << std::endl;
    if (this->all_server_names.empty()) {
        std::cout << "  (账本空无一物)" << std::endl;
    } else {
        for (std::map<int, std::set<std::string> >::const_iterator m_it = this->all_server_names.begin();
             m_it != this->all_server_names.end(); ++m_it)
        {
            std::cout << "  ├─ 🔹 监听端口: [" << m_it->first << "]" << std::endl;
            std::cout << "  │  └─ 🔒 已绑定防伪域名集: { ";
            for (std::set<std::string>::const_iterator s_it = m_it->second.begin(); s_it != m_it->second.end(); ++s_it) {
                std::cout << "\"" << *s_it << "\" ";
            }
            std::cout << "}" << std::endl;
        }
    }
    std::cout << "--------------------------------------------------------------------" << std::endl;

    // 🟢 2. 宏观解构：遍历整个 servers 别墅群
    std::cout << "[🏛️ 内存虚拟主机别墅群 (共 " << this->servers.size() << " 个 server 块)]" << std::endl;
    for (size_t i = 0; i < this->servers.size(); ++i)
    {
        // 这里的 srv 变量名字可以根据你们真实的 ServerConfig 调整
        const ServerConfig &srv = this->servers[i]; 
        
        std::cout << "  🏠 [Server Block #" << i + 1 << "]" << std::endl;
        // 假设你们解析出的 port 存在 srv.port (如果没有，请替换为你们对应的变量名)
        std::cout << "    ├─ 📡 Listen Port : " << srv.port << std::endl; 
        
        // 打印该别墅持有的独立域名
        std::cout << "    ├─ 🏷️ Server Names: [ ";
        for (size_t j = 0; j < srv.server_names.size(); ++j) {
            std::cout << "\"" << srv.server_names[j] << "\" ";
        }
        std::cout << "]" << std::endl;

        std::cout << "    ├─ 📂 Root Path   : " << srv.root << std::endl;
        
        // 假设你们成功补全了 client_max_body_size
        std::cout << "    ├─ ⚖️ Max Body Size: " << srv.max_body_size << " Bytes" << std::endl;

        // 🟢 3. 微观解构：遍历当前别墅肚子里所有内嵌的 location 小房间
        // 这里的 locations 名字可以根据你们真实的 LocationConfig 向量调整
        std::cout << "    └─ 🚪 Locations 小房间 (共 " << srv.locations.size() << " 个):" << std::endl;
        for (size_t k = 0; k < srv.locations.size(); ++k)
        {
            const LocationConfig &loc = srv.locations[k];
            std::cout << "        ├─ 📍 房间路由 [" << loc.path << "]" << std::endl; // 比如 /api
            std::cout << "        │  ├─ 📂 房间私有 Root: " << (loc.root.empty() ? "(继承 Server 根)" : loc.root) << std::endl;
            std::cout << "        │  └─ ⚖️ 房间 Body Size: " << loc.max_body_size << " Bytes" << std::endl;
        }
        std::cout << "        " << std::endl; // 别墅空行隔离
    }
    std::cout << "====================================================================\n" << std::endl;
}