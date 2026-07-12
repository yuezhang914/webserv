#ifndef CONFIGROUTEUTILS_HPP
#define CONFIGROUTEUTILS_HPP

#include "ServerConfig.hpp"
#include "LocationConfig.hpp"

#include <string>
#include <vector>
/*
文件：srcs/Config/ConfigRouteUtils.hpp
作用：提供只依赖 Config 数据结构的路由辅助函数。
为什么新增：原来最长前缀 location 匹配函数放在 CgiProtocol.cpp，导致 Request 如果要在读取 body 前使用 location 规则，就必须反向依赖 CGI/Response。
修改意义：把 location 匹配和 effective body limit 计算移动到 Config 层公共工具中，让 Request、Response、CGI 都能复用同一套规则。
*/

/*
函数：findMatchingLocation
用途：根据 URI 在 locations 中找最长前缀匹配的 location。
参数来源：uri 来自 Request.uri；locations 来自当前 ServerConfig.locations；use_location 是输出标志。
返回：匹配到的 LocationConfig 指针；没有匹配返回 NULL。
*/
LocationConfig* findMatchingLocation(const std::string& uri, const std::vector<LocationConfig>& locations, bool &use_location);

/*
函数：getEffectiveBodyLimit
用途：计算当前请求真正使用的 body size 限制。
参数来源：server 是当前 client 对应的 ServerConfig；uri 是已经解析出的 Request.uri。
规则：先按最长前缀匹配 location；如果 location 显式写了 max_body_size，就用 location 的值；否则继承 server.max_body_size。
*/
unsigned long getEffectiveBodyLimit(const ServerConfig* server, const std::string& uri);

#endif