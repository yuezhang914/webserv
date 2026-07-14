#ifndef CONFIGROUTEUTILS_HPP
#define CONFIGROUTEUTILS_HPP

#include "ServerConfig.hpp"
#include "LocationConfig.hpp"

#include <string>
#include <vector>

/*
函数：findMatchingLocation
用途：在 locations 中查找与 normalized path 最长匹配的 location。
参数：path 必须已经与 query 分离；locations 来自当前 ServerConfig。
返回：匹配到的只读 LocationConfig 指针；没有匹配返回 NULL。
*/
const LocationConfig *findMatchingLocation(const std::string &path, const std::vector<LocationConfig> &locations);

/*
函数：getEffectiveBodyLimit
用途：根据 normalized path 计算 server/location 最终生效的 body size limit。
规则：匹配 location 显式定义 max_body_size 时使用 location 值，否则使用 server 值。
*/
unsigned long getEffectiveBodyLimit(const ServerConfig *server, const std::string &path);

#endif