#ifndef LOCATIONCONFIG_HPP
#define LOCATIONCONFIG_HPP

#include <set>
#include <string>
#include <vector>
#include <map>

/*
类：LocationConfig
作用：保存一个 location /path/ { ... } 块的特殊规则。
从哪来：Config::parseFile() 读到 location 指令时创建；Config::parseLocationDirective() 把 allow_methods/root/cgi_extension 等写进它。
给谁用：Router/EffectiveRoute 根据请求 URI 匹配 location，然后用这里的规则覆盖或补充 ServerConfig 的默认规则。
*/
class LocationConfig {
	public:
	/* 当前 location 允许的 HTTP 方法。来源：allow_methods GET POST DELETE;。Response 检查方法时优先使用它。 */
	std::set<std::string> allow_methods;
	/* location 自己的 root。来源：location 内 root 指令；如果存在，会覆盖 server.root。 */
	std::string root;
	/* 当前 location 是否允许 autoindex。来源：autoindex on/off；只有 has_autoindex=true 时才覆盖 server.autoindex。 */
	bool autoindex;
	/* 是否显式写过 autoindex。设计目的：区分“location 没写 autoindex”和“location 明确写 autoindex off”。 */
	bool has_autoindex;
	/* 当前 location 的 index 文件。来源：index 指令；如果为空则使用 server.index 或默认 index.html。 */
	std::vector<std::string> index;
	/* CGI 后缀到解释器路径的映射。来源：cgi_extension .py /usr/bin/python3;。CGI 判断和 execve 用它。 */
	std::map<std::string, std::string> cgi_extensions;
	/* 上传目录。来源：upload_path 指令；POST 保存文件时使用。 */
	std::string upload_path;
	/* location 的匹配前缀。来源：location /readOnly/ 这一行中的 /readOnly/。Router 做最长前缀匹配时使用。 */
	std::string path;
	/* 重定向状态码。来源：return 301 url;。0 表示没有重定向。 */
	int redirect_status;
	/* 重定向目标 URL。来源：return 指令的第二个参数。Response 会写到 Location header。 */
	std::string redirect_url;
	/* alias 路径。来源：alias 指令；和 root 互斥，用于把 location 前缀替换成另一个真实目录。 */
	std::string alias;
	/* 是否显式写过 root。来源：parseLocationDirective("root")；EffectiveRoute 用它判断是否覆盖 server.root。 */
	bool has_root;
	/* 是否显式写过 alias。设计目的：区分 alias 为空和没配置 alias；当前 parser 写 alias 时应配合设置它。 */
	bool has_alias;

	/* 🛠️ 修改点：重新支持 location 级 max_body_size。
	   意义：location 可以对特定 URI 前缀覆盖 server 的 body 限制；RequestBody 会在读取 body 前通过 ConfigRouteUtils 做最长前缀匹配并使用该值。 */
	unsigned long max_body_size;
	/* 是否显式写过 max_body_size。设计目的：区分“location 没写，继承 server 限制”和“location 明确配置自己的限制”。 */
	bool has_body_size;

	/* 构造函数：把所有规则设置为安全默认值，例如 autoindex=false、has_autoindex=false、max_body_size=MAX_BODY_SIZE、redirect_status=0、字符串为空。 */
	LocationConfig();
	/* 拷贝构造：复制所有配置字段，供 vector 扩容或返回时使用。 */
	LocationConfig(const LocationConfig& src);
	/* 赋值运算符：把 rhs 的所有配置字段复制到当前对象。 */
	LocationConfig& operator=(const LocationConfig& rhs);
	/* 析构函数：没有手动管理的资源，默认清理 string/map/set/vector 即可。 */
	virtual ~LocationConfig();
};

#endif