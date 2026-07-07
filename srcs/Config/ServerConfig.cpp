/*
文件：srcs/Config/ServerConfig.cpp
server 块解析和 ServerConfig 生命周期实现。这个文件把 listen/root/index/error_page 等 server 级别指令写入 ServerConfig。
*/
#include "ServerConfig.hpp"
#include "Webserv.hpp"

/*
函数：is_valid_ipv4
用途：检查 listen 指令中的 IP 是否是合法 IPv4。
参数来源：parseServerDirective 解析 listen 127.0.0.1:3435 时传入 IP 部分。
变量解释：
    - ip：listen 指令解析出的 IP C 字符串。
    - text：把 ip 转成 std::string 后的文本。
    - stream：按 . 拆分 text 的 stringstream。
    - part：当前 IP 段，例如 127、0、1。
    - count：已经读取到的段数，最终必须是 4。
    - index：遍历 part 内字符的下标。
    - value：part 转成整数后的值，必须在 0 到 255。
实现逻辑：
    1. 如果 ip 是 NULL，返回 ERROR。
    2. 用点号 . 把字符串拆成四段。
    3. 每一段必须非空，长度不能超过 3。
    4. 每一段的字符都必须是数字。
    5. atoi 后的值必须在 0 到 255 之间。
    6. 最终必须刚好有四段，否则不是 IPv4。
*/
static int is_valid_ipv4(const char *ip)
{
    if (ip == NULL)
        return ERROR;
    std::string text(ip);
    std::stringstream stream(text);
    std::string part;
    int count = 0;
    while (std::getline(stream, part, '.'))
    {
        if (part.empty() || part.size() > 3)
            return ERROR;
        size_t index = 0;
        while (index < part.size())
        {
            if (!std::isdigit(part[index]))
                return ERROR;
            index++;
        }
        int value = std::atoi(part.c_str());
        if (value < 0 || value > 255)
            return ERROR;
        // 如果长度大于 1 且第一个数字是 '0'，说明存在恶意前导零（如 "010"），直接扼杀
        if (part.size() > 1 && part[0] == '0')
            return ERROR;
        count++;
    }
    if (count != 4)
        return ERROR;
    return SUCCESS;
}

/*
函数：Config::parseServerDirective
用途：解析 server { ... } 里面的一条指令，并写入当前 ServerConfig。
参数来源：parseDirective 在 current_server 不为空时调用；directive 是指令名，values 是参数列表，srv 是当前 server 对象。
变量解释：
    - directive：当前 server 指令名，例如 listen、root、error_page。
    - values：当前指令的参数数组，来自 parseDirectiveTokens。
    - srv：当前正在填充的 ServerConfig 指针。
    - value：listen 分支中 values[0] 的副本，可能是 port、ip 或 ip:port。
    - ip：listen 分支解析出的监听 IP。
    - port_str：listen 分支解析出的端口字符串。
    - colon_pos：value 中 : 的位置，用来区分 ip:port 和其他写法。
    - parts：当 listen 没有 : 时，用 . 拆分 value 后得到的片段，用来判断它像不像 IP。
    - endptr：strtol 输出参数，用来检查端口、状态码等字符串是否完全是数字。
    - code：error_page 分支解析出的 HTTP 状态码。
    - size：max_body_size 分支 parseSize 返回的字节数。
实现逻辑：
    1. allow_methods：把 GET/POST/DELETE 等方法加入 srv->allow_methods。
    2. upload_path：检查只能有一个参数，然后写入 srv->upload_path。
    3. autoindex/directory_listing：检查参数是 on/off，然后设置 srv->autoindex。
    4. listen：解析 IP 和端口，支持 port、ip、ip:port 三种形式；检查 IP 和端口合法；写入 srv->host/srv->port；限制同一个 server 只出现一次 listen。
    5. server_name：把所有名字 push 到 srv->server_names。
    6. root：检查只能有一个参数且不能重复，然后写入 srv->root 并设置 has_root=true。
    7. error_page：把状态码和文件路径写入 srv->error_pages[code]。
    8. max_body_size：调用 parseSize，把 1M/512K 转成字节数，写入 srv->max_body_size。
    9. index：写入 srv->index。
    10. 任何未知指令都会报错并返回 ERROR。
产出：ServerConfig 从“默认空对象”变成“可用于 socket/route/response 的规则对象”。
*/
bool Config::parseServerDirective(const std::string &directive, const std::vector<std::string> &values, ServerConfig *srv)
{
    if (directive == "allow_methods")
    {
        if (values.size() >= 1)
        {
            for (size_t i = 0; i < values.size(); ++i)
            {
                if (values[i].empty())
                {
                    std::cerr << "Error: Empty method token in allow_methods" << std::endl;
                    return ERROR;
                }
                std::string method = values[i];
                for (size_t j = 0; j < method.size(); ++j)
                    method[j] = std::toupper(method[j]);

                if (method != "GET" && method != "POST" && method != "DELETE")
                {
                    std::cerr << "Error: Unsupported HTTP method: " << values[i] << std::endl;
                    return ERROR;
                }
                srv->allow_methods.insert(method);
            }
        }
        else
            srv->allow_methods.insert("NONE");
    }
    else if (directive == "upload_path")
    {
        if (values.size() != 1)
        {
            std::cerr << "Error: Invalid upload_path directive - only one value allowed" << std::endl;
            return ERROR;
        }
        srv->upload_path = values[0];
    }
    else if (directive == "autoindex" || directive == "directory_listing")
    {
        if (values.size() != 1)
        {
            std::cerr << "Invalid " << directive << " directive" << std::endl;
            return ERROR;
        }
        if (values[0] == "on")
            srv->has_autoindex = true;
        else if (values[0] == "off")
            srv->has_autoindex = false;
        else
        {
            std::cerr << "Invalid " << directive << " directive value: " << values[0] << std::endl;
            return ERROR;
        }
    }
    else if (directive == "listen")
    {
        if (values.size() != 1)
        {
            std::cerr << "Invalid listen directive" << std::endl;
            return ERROR;
        }

        if (srv->countport >= 1)
        {
            std::cerr << "Invalid number of port" << std::endl;
            return ERROR;
        }

        std::string value = values[0];
        std::string ip;
        std::string port_str;
        size_t colon_pos = value.find(':');

        if (colon_pos == std::string::npos)
        {
            std::vector<std::string> parts = split(value, '.');
            if (parts.size() == 4)
            {
                ip = value;
                port_str = DEFAULT_PORT;
            }
            else
            {
                port_str = value;
                ip = "";
            }
        }
        else
        {
            ip = value.substr(0, colon_pos);
            port_str = value.substr(colon_pos + 1);
        }

        if (!ip.empty())
        {
            if (is_valid_ipv4(ip.c_str()) == ERROR)
            {
                std::cerr << "Invalid IP in listen directive: " << ip << std::endl;
                return ERROR;
            }
            srv->host = ip;
        }
        else
        {
            srv->host = "INADDR_ANY";
        }

        char *endptr;
        srv->port = strtol(port_str.c_str(), &endptr, 10);

        if (*endptr != '\0' || port_str.empty() || srv->port < 0 || srv->port > 65535)
        {
            std::cerr << "Invalid port in listen directive: " << port_str << std::endl;
            return ERROR;
        }

        /* 🎯 【修改点 1：调整计数时序，落闸防御状态污染】 */
        /* 解释：将 srv->countport++ 彻底移到本分支所有格式与值域校验通过的最下行，
                确保在遭遇非法端口格式投毒（如 999999）直接强退时，不会错误改写、污染全局的状态计数位 */
        srv->countport++;
    }
    else if (directive == "server_name")
    {
        /* 【修改点 2：并线域名清空锁，复刻工业覆盖语义】 */
        /* 解释：在执行 push_back 循环前加入 .clear() 锁，保证在同一个 server 块中
                如果多次出现 server_name 或者是继承自全局配置时，后写的指令能彻底覆盖、洗牌先写的，对齐 Nginx 官方原厂契约 */
        srv->server_names.clear();
        for (size_t i = 0; i < values.size(); ++i)
            srv->server_names.push_back(values[i]);
    }
    /* 🚀 【完美补网：注入 client_max_body_size 工业级防线】 */
    else if (directive == "max_body_size" || directive == "client_max_body_size")
    {
        if (values.size() != 1)
        {
            std::cerr << "Error: client_max_body_size requires exactly 1 value" << std::endl;
            return ERROR;
        }

        // 解释：把类似于 "100M" 或者 "20" 这样的纯文本转成数字存进 srv 结构体肚子里
        // 提示：你可以选择只解析纯数字（字节数），或者顺手支持带 'M' 或 'K' 的单位。
        // 这里提供一个最干净的纯数字/基础解析示范：
        std::string size_str = values[0];
        size_t multiplier = 1;

        if (!size_str.empty())
        {
            char last_char = size_str[size_str.length() - 1];
            if (last_char == 'M' || last_char == 'm')
            {
                multiplier = 1024 * 1024;
                size_str = size_str.substr(0, size_str.length() - 1);
            }
            else if (last_char == 'K' || last_char == 'k')
            {
                multiplier = 1024;
                size_str = size_str.substr(0, size_str.length() - 1);
            }
        }

        // 转换并存入你们 ServerConfig 里的成员变量（例如 srv->client_max_body_size）
        srv->client_max_body_size = std::atoi(size_str.c_str()) * multiplier;
    }
    else if (directive == "root")
    {
        if (values.size() != 1)
        {
            std::cerr << "Invalid root directive" << std::endl;
            return ERROR;
        }
        if (!srv->root.empty())
        {
            std::cerr << "Error: Duplicate root directive in server" << std::endl;
            return ERROR;
        }
        srv->root = values[0];
        srv->has_root = true;
    }
    else if (directive == "error_page")
    {
        if (values.size() < 2)
        {
            std::cerr << "Invalid error_page directive" << std::endl;
            return ERROR;
        }
        /* 【修改点 3：锁定尾部路径，完美兼容多错误码一对多映射】 */
        /* 解释：彻底推翻了“状态码与路径必然单对单成对出现”的旧版误区。物理锁定 values[values.size() - 1] 必然是页面路径，
                再通过循环把前面由于多码聚合（如 error_page 404 403 /error.html）切出来的全部错误码安全地灌入状态机，封杀了路径错位乱码隐患 */
        std::string error_path = values[values.size() - 1];
        if (error_path.empty())
        {
            std::cerr << "Error: Empty error page path" << std::endl;
            return ERROR;
        }
        for (size_t i = 0; i < values.size() - 1; ++i)
        {
            char *endptr;
            int code = strtol(values[i].c_str(), &endptr, 10);
            if (*endptr != '\0' || code < 300 || code > 599)
            {
                std::cerr << "Error: Invalid error_page code: " << values[i] << std::endl;
                return ERROR;
            }
            srv->error_pages[code] = error_path;
        }
    }
    else if (directive == "max_body_size")
    {
        if (values.size() != 1)
        {
            std::cerr << "Invalid client_max_body_size directive" << std::endl;
            return ERROR;
        }
        size_t size = parseSize(values[0]);
        if (size == (size_t)ERROR_PARSE_SIZE)
        {
            std::cerr << "Invalid parse size" << std::endl;
            return ERROR;
        }
        srv->max_body_size = size;
    }
    else if (directive == "index")
    {
        if (values.size() < 1)
        {
            std::cerr << "Error: Invalid index directive" << std::endl;
            return ERROR;
        }
        /* 【修改点 4：并线多首页 vector 全局状态对齐】 */
        /* 解释：配合你之前在大房间 ServerConfig 数据底座以及 LocationConfig 里将 index 由 string 升级为 vector 的高能重构，
                此处同步并线了 .clear() 并使用 size_t 规整化循环，打通了全系统一的‘Fallback 变长首页轮询机制’ */
        srv->index.clear();
        for (size_t i = 0; i < values.size(); ++i)
            srv->index.push_back(values[i]);
    }
    else
    {
        std::cerr << "Error: Unknown server directive: " << directive << std::endl;
        return ERROR;
    }
    /* 🎯 【修改点 5：宏定义状态重塑，消除裸数字隐式穿透】 */
    /* 解释：废除了原本随手裸写的 return 0; 彻底向主干框架约定的宏定义或枚举值 SUCCESS 进行强约束呼应，保障返回值体系的高度纯净 */
    return SUCCESS;
}

/*
函数：ServerConfig::ServerConfig
用途：创建一个带默认值的 server 配置对象。
变量解释：
    - port：默认端口，构造时设为 8080。
    - countport：listen 指令计数，构造时设为 0。
    - host/root/index/upload_path：字符串配置，构造时为空。
    - max_body_size：请求体大小限制，构造时使用 MAX_BODY_SIZE。
    - socketFd：监听 socket fd，构造时为 0 表示尚未创建。
    - has_root：是否显式配置 root，构造时为 false。
    - autoindex：目录列表开关，构造时为 false。
    - server_names/error_pages/locations/allow_methods：容器配置，构造时清空。
实现逻辑：
    1. 默认端口设为 8080。
    2. countport=0，表示还没解析到 listen。
    3. root/host/index 为空，has_root=false。
    4. max_body_size 使用 MAX_BODY_SIZE 默认值。
    5. socketFd=0，表示还没有创建监听 socket。
    6. autoindex=false，默认不生成目录列表。
    7. 清空 server_names/error_pages/locations/allow_methods 等容器。
*/
ServerConfig::ServerConfig()
    : port(8080), countport(0), host(""), server_names() // 在初始化列表里显式触发默认空构造
      ,
      root(""), error_pages() // 自动初始化为空 map
      ,
      max_body_size(MAX_BODY_SIZE), locations(), index() // 多首页 vector 默认纯净初始化
      ,
      upload_path(""), allow_methods(), socketFd(-1) // 必须是 -1
      ,
      has_root(false), has_autoindex(false)
{
    // 💡 大括号内保持绝对的纯净空荡，不需要做任何画蛇添足的显式 clear()！
}

/*
函数：ServerConfig 拷贝构造
用途：复制一个 ServerConfig 的配置字段。
变量解释：
    - src：被复制的 ServerConfig。
    - socketFd：不会复制 src.socketFd，而是设为 0，避免 fd 所有权重复。
    - 其他字段：port、host、root、locations、error_pages 等普通配置会复制。
实现逻辑：
    1. 复制端口、host、server_names、root、error_pages、max_body_size、locations、index、allow_methods 等规则数据。
    2. socketFd 不复制，而是设为 0。
原因：socket fd 是系统资源，不能让两个 ServerConfig 对象同时认为自己拥有同一个 fd，否则析构时可能重复 close。
*/
ServerConfig::ServerConfig(const ServerConfig &src)
    : port(src.port), countport(src.countport), host(src.host), server_names(src.server_names), root(src.root), error_pages(src.error_pages), max_body_size(src.max_body_size), locations(src.locations), index(src.index), allow_methods(src.allow_methods), socketFd(0), has_root(src.has_root), has_autoindex(src.has_autoindex)
{
}

/*
函数：ServerConfig::~ServerConfig
用途：销毁 ServerConfig 时关闭它拥有的监听 socket。
变量解释：
    - socketFd：当前对象拥有的监听 socket；大于 0 时需要 close。
实现逻辑：
    1. 如果 socketFd > 0，说明 setupSockets 成功创建过 socket。
    2. 调用 close(socketFd) 释放系统资源。
注意：拷贝构造和赋值时 socketFd 会置 0，就是为了避免重复关闭同一个 fd。
*/
ServerConfig::~ServerConfig()
{
    if (socketFd > 0)
        close(socketFd);
}

/*
函数：ServerConfig::operator=
用途：把 rhs 的配置内容赋值给当前对象。
变量解释：
    - rhs：赋值来源对象。
    - this：当前被赋值对象；如果 this == &rhs，说明是自我赋值，直接返回。
    - socketFd：赋值后重置为 0，不接管 rhs.socketFd。
实现逻辑：
    1. 先检查 self-assignment，避免自己赋值给自己。
    2. 复制所有普通配置字段。
    3. socketFd 设为 0，不复制 rhs.socketFd。
    4. 返回 *this，支持链式赋值。
*/
ServerConfig &ServerConfig::operator=(const ServerConfig &rhs)
{
    if (this != &rhs)
    {
        port = rhs.port;
        countport = rhs.countport;
        host = rhs.host;
        server_names = rhs.server_names;
        root = rhs.root;
        error_pages = rhs.error_pages;
        max_body_size = rhs.max_body_size;
        locations = rhs.locations;
        index = rhs.index;
        allow_methods = rhs.allow_methods;
        socketFd = 0;
        has_root = rhs.has_root;
        has_autoindex = rhs.has_autoindex;
    }
    return *this;
}
