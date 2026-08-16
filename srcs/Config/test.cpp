#include "Config.hpp"
#include <iostream>

// Prints config.
void Config::printConfig() const
{
    std::cout << "\n====================================================================" << std::endl;
    std::cout << "        🏆 WEBSERV CONFIGURATION TOPOLOGY DUMP (资产自检大阅兵)      " << std::endl;
    std::cout << "====================================================================" << std::endl;
    std::cout << "[📊 全局域名防伪锁状态]" << std::endl;
    if (this->all_server_names.empty())
    {
        std::cout << "  (账本空无一物)" << std::endl;
    }
    else
    {
        for (std::map<int, std::set<std::string> >::const_iterator m_it = this->all_server_names.begin(); m_it != this->all_server_names.end(); ++m_it)
        {
            std::cout << "  ├─ 🔹 监听端口: [" << m_it->first << "]" << std::endl;
            std::cout << "  │  └─ 🔒 已绑定防伪域名集: { ";
            for (std::set<std::string>::const_iterator s_it = m_it->second.begin(); s_it != m_it->second.end(); ++s_it)
            {
                std::cout << "\"" << *s_it << "\" ";
            }
            std::cout << "}" << std::endl;
        }
    }
    std::cout << "--------------------------------------------------------------------" << std::endl;
    std::cout << "[🏛️ 内存虚拟主机别墅群 (共 " << this->servers.size() << " 个 server 块)]" << std::endl;
    for (size_t i = 0; i < this->servers.size(); ++i)
    {
        const ServerConfig &srv = this->servers[i];
        std::cout << "  🏠 [Server Block #" << i + 1 << "]" << std::endl;
        std::cout << "    ├─ 📡 Listen Port : " << srv.port << std::endl;
        std::cout << "    ├─ 🏷️ Server Names: [ ";
        for (size_t j = 0; j < srv.server_names.size(); ++j)
        {
            std::cout << "\"" << srv.server_names[j] << "\" ";
        }
        std::cout << "]" << std::endl;
        std::cout << "    ├─ 📂 Root Path   : " << srv.root << std::endl;
        std::cout << "    ├─ ⚖️ Max Body Size: " << srv.max_body_size << " Bytes" << std::endl;
        std::cout << "    └─ 🚪 Locations 小房间 (共 " << srv.locations.size() << " 个):" << std::endl;
        for (size_t k = 0; k < srv.locations.size(); ++k)
        {
            const LocationConfig &loc = srv.locations[k];
            std::cout << "        ├─ 📍 房间路由 [" << loc.path << "]" << std::endl;
            std::cout << "        │  ├─ 📂 房间私有 Root: " << (loc.root.empty() ? "(继承 Server 根)" : loc.root) << std::endl;
            std::cout << "        │  └─ ⚖️ 房间 Body Size: " << loc.max_body_size << " Bytes" << std::endl;
        }
        std::cout << "        " << std::endl;
    }
    std::cout << "====================================================================\n" << std::endl;
}
