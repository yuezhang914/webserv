#ifndef SERVERCONFIG_HPP
#define SERVERCONFIG_HPP


#include <string>
#include <vector>
#include <map>
#include <set>
#include <sstream>
#include <cctype>
#include <unistd.h>

class LocationConfig;

#define MAX_BODY_SIZE 1048576


/*
类：ServerConfig
作用：保存一个 server { ... } 块的所有规则。
从哪来：Config::parseFile() 每读到一个 server 块，就创建一个 ServerConfig；Config::parseServerDirective() 把 listen/root/index 等指令写进它。
给谁用：setupSockets() 用 host/port 创建监听 socket；Request/Response/Router 用 root/index/error_pages/locations 等规则处理请求；析构时关闭 socketFd。
*/
class ServerConfig {
public:
    int port;
    int countport;
    std::string host;
    std::vector<std::string> server_names; // 💡 绝妙的域名集群组合，完美兼容单行多域名访问
    std::string root;
    std::map<int, std::string> error_pages;
    unsigned long max_body_size;
    std::vector<LocationConfig> locations;
    
    /* 🛠️ 【修改点 1】：与 Location 块强类型对齐，由单体 string 完美升级为向量容器，承接全局多首页Fallback */
    std::vector<std::string> index; 
    
    std::string upload_path;
    std::set<std::string> allow_methods;
    
    /* 🛠️ 【修改点 2】：安全文件描述符。必须在构造函数中初始化为 -1，绝对不能用 0 混淆标准输入 */
    int socketFd; 
    
    bool has_root;
    bool has_autoindex; // 同步并线我们在 location 中沉淀的 has_ 状态锁

    ServerConfig();
    
    /* 🛠️ 在实现这个拷贝构造时，内部的 socketFd 必须安全改写为 -1 锁死！ */
    ServerConfig(const ServerConfig& src);
    
    /* 🛠️ 在实现析构时，只有当 socketFd != -1 且 > 0 时（防止关掉0, 1, 2）才执行 close() */
    virtual ~ServerConfig();
    
    /* 🛠️ 赋值运算符中， rhs 的已监听 fd 同样不能传递，新对象强制洗牌初始化为 -1 */
    ServerConfig& operator=(const ServerConfig& rhs);
};

#endif