
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