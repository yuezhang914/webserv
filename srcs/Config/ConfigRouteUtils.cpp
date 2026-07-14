/*
文件：srcs/Config/ConfigRouteUtils.cpp
用途：提供 Request 在读取 body 前需要的 location 匹配与有效 body limit 计算。
说明：传入参数已经是 RequestParser 生成的 normalized path，因此这里不能再次按 '?' 截断。
*/
#include "ConfigRouteUtils.hpp"

/*
函数：locationPathMatches
用途：判断 normalized path 是否落在 location 的真实路径边界内。
规则：/ 匹配全部绝对路径；完全相等匹配；普通前缀后必须是 '/'，避免 /api 误匹配 /api-other。
*/
static bool locationPathMatches(
    const std::string &path,
    const std::string &location_path)
{
    if (location_path.empty())
        return false;
    if (location_path == "/")
        return !path.empty() && path[0] == '/';
    if (path.compare(0, location_path.size(), location_path) != 0)
        return false;
    if (path.size() == location_path.size())
        return true;
    if (location_path[location_path.size() - 1] == '/')
        return true;
    return path[location_path.size()] == '/';
}

/*
函数：findMatchingLocation
用途：遍历 locations，返回路径最长的合法匹配。
参数：path 是已经解码、规范化并与 query 分离的路径。
*/
const LocationConfig *findMatchingLocation(
    const std::string &path,
    const std::vector<LocationConfig> &locations)
{
    const LocationConfig *best_match = NULL;
    size_t longest_match = 0;
    size_t i = 0;
    while (i < locations.size())
    {
        if (locationPathMatches(path, locations[i].path)
            && locations[i].path.size() > longest_match)
        {
            best_match = &locations[i];
            longest_match = locations[i].path.size();
        }
        ++i;
    }
    return best_match;
}

/*
函数：getEffectiveBodyLimit
用途：返回当前 normalized path 应使用的最大 body 大小。
*/
unsigned long getEffectiveBodyLimit(
    const ServerConfig *server,
    const std::string &path)
{
    if (server == NULL)
        return MAX_BODY_SIZE;

    const LocationConfig *location =
        findMatchingLocation(path, server->locations);
    if (location != NULL && location->has_body_size)
        return location->max_body_size;
    return server->max_body_size;
}