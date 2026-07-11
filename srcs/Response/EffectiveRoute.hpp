#ifndef EFFECTIVEROUTE_HPP
#define EFFECTIVEROUTE_HPP

#include <string>
#include <iostream>
#include <vector>
#include "ServerConfig.hpp"
#include "LocationConfig.hpp"
#include "../Request/Request.hpp"

#define PATH_OK 0

/*
结构体：EffectiveRoute
作用：保存“Request + ServerConfig + LocationConfig”合并后的最终处理规则。
为什么需要：Request.uri 只是浏览器路径，例如 /ping.html；Response/CGI 真正需要的是磁盘路径、最终 root、最终 index、允许方法、是否 autoindex 等。EffectiveRoute 就是把这些算好。
从哪来：buildResponse() 创建 EffectiveRoute，然后调用 createEffectiveRoute() 和 createEffectivePath() 填充。
给谁用：GET/POST/DELETE/CGI 处理函数都读取它。
*/
struct EffectiveRoute {
    /* 当前请求对应的 server 配置。来源：Request.config。 */
    const ServerConfig* server;
    /* 当前请求匹配到的 location。来源：getMatchingLocation；没有匹配时为 NULL。 */
    const LocationConfig* location;
	/* 当前方法是否 GET。来源：buildResponse 根据 request.method 设置；isValidPath 用它区分检查文件还是检查上传目录。 */
	bool getMethod;
	/* 是否使用了 location 规则。来源：最长前缀匹配结果。 */
	bool useLocation;
    /* 最终 root。来源：location.root 优先，否则 server.root。 */
    std::string root;
    /* 最终 alias。来源：location.alias；与 root 互斥。 */
    std::string alias;
    /* 是否使用 alias 拼路径。来源：location.has_alias。 */
    bool use_alias;
    /* 最终 autoindex 规则。来源：location 显式 autoindex 优先，否则继承 server.autoindex。 */
    bool autoindex;
    /* 最终允许方法集合。来源：location.allow_methods 优先，否则 server.allow_methods，否则默认 GET/POST/DELETE。 */
    std::set<std::string> allow_methods;
    /* 🛠️ 修改点：最终 index 候选列表。来源：location.index 优先，否则 server.index，否则默认 index.html。
       意义：支持 index index.html index.htm; 这种多首页 fallback。 */
    std::vector<std::string> index;
    /* 最终上传目录。来源：location.upload_path 优先，否则 server.upload_path，否则 /upload/。 */
    std::string upload_path;
    /* 匹配到的 location 前缀。来源：loc->path；alias 拼接时用于切掉 URI 前缀。 */
    std::string location_prefix;
    /* 重定向状态码。来源：location.return。0 表示不重定向。 */
    int redirect_status;
    /* 重定向目标 URL。来源：location.return。 */
    std::string redirect_url;
	/* 最终真实路径。来源：createEffectivePath(req.uri)；GET 时是文件/目录路径，POST/DELETE 时也是目标路径。 */
	std::string uri;
	/* 最终路径是否目录。来源：stat() 结果；GET 目录时用于 index/autoindex。 */
	bool isDir;
	/* 响应发完后是否关闭连接。来源：Request.closeConnection。 */
	bool closeConnection;

	bool createEffectiveRoute(const ServerConfig* srv, const LocationConfig* loc);
	bool createEffectiveRoute(const ServerConfig* srv);
	int createEffectivePath(std::string req_uri);
	int isValidPath(void);
};

std::string joinPaths(const std::string& base, const std::string& suffix);

#endif