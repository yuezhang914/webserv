#ifndef SERVERCONFIG_HPP
#define SERVERCONFIG_HPP


#include <string>
#include <vector>
#include <map>
#include <set>

#define MAX_BODY_SIZE 1048576
class LocationConfig;

/*
类：ServerConfig
作用：保存一个 server { ... } 块的所有规则。
从哪来：Config::parseFile() 每读到一个 server 块，就创建一个 ServerConfig；Config::parseServerDirective() 把 listen/root/index 等指令写进它。
给谁用：setupSockets() 用 host/port 创建监听 socket；Request/Response/Router 用 root/index/error_pages/locations 等规则处理请求；析构时关闭 socketFd。
*/
class ServerConfig {
public:
	/* 端口号。来源：listen 指令，例如 listen 127.0.0.1:3435; 会让 port = 3435。setupSockets() 用它 bind 端口。 */
	int port;
	/* 记录 listen 指令出现次数。来源：parseServerDirective("listen") 每成功解析一次就递增；用于阻止同一个 server 配多个 listen。 */
	int countport;
	/* 监听地址文本。来源：listen 指令中的 IP 部分；为空或 INADDR_ANY 表示监听所有本地地址。 */
	std::string host;
	/* server_name 列表。来源：server_name 指令；请求里的 Host header 会用它选择匹配的 server。 */
	std::vector<std::string> server_names;
	/* 网站根目录。来源：root 指令；/ping.html 会和 root 拼成真实路径 srv/www/ping.html。 */
	std::string root;
	/* 错误页表。来源：error_page 404 path; key 是状态码，value 是错误页文件路径。Response::createResponse 生成错误响应时使用。 */
	std::map<int, std::string> error_pages;
	/* 请求 body 最大字节数。来源：max_body_size 指令；Request parser 读取 POST body 时检查是否超过这个值。 */
	unsigned long max_body_size;
	/* 当前 server 下所有 location 规则。来源：parseFile() 遇到 location /xxx/ 时 push_back。Router 根据 URI 做最长前缀匹配。 */
	std::vector<LocationConfig> locations;
	/* 默认首页文件名。来源：index 指令；请求目录 / 时会尝试读取 root + / + index。 */
	std::string index;
	/* 默认上传目录。来源：upload_path 指令；POST handler 在 location 没有 upload_path 时使用它。 */
	std::string upload_path;
	/* server 默认允许的方法集合。来源：allow_methods 指令；location 没有覆盖时使用。 */
	std::set<std::string> allow_methods;
	/* 监听 socket 的 fd。来源：setupSockets() 调用 socket/bind/listen 成功后写入；serverLoop 把它放进 poll。 */
	int socketFd;
	/* 是否配置过 root。来源：parseServerDirective("root") 设置 true；Config 构造完成后用它检查配置是否合法。 */
	bool has_root;
	/* 是否允许目录列表。来源：autoindex on/off；请求目录且没有 index 时决定是否生成目录列表。 */
	bool autoindex;


	/* 构造函数：设置默认端口 8080、默认 body 限制、socketFd=0、autoindex=false，并清空各容器。 */
	ServerConfig();
	/* 拷贝构造：复制配置字段，但 socketFd 重新置 0，避免两个对象同时关闭同一个 fd。 */
	ServerConfig(const ServerConfig& src);
	/* 析构函数：如果 socketFd > 0，关闭监听 socket，防止程序退出时 fd 泄漏。 */
	virtual ~ServerConfig();
	/* 赋值运算符：复制配置内容；同样不复制已有 socketFd，而是置 0，避免 fd 所有权混乱。 */
	ServerConfig& operator=(const ServerConfig& rhs);
};

#endif