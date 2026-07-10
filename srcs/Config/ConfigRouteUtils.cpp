/*
文件：srcs/Config/ConfigRouteUtils.cpp
配置路由公共辅助函数实现。这里不生成 Response、不执行 CGI，只根据 URI 和 Config 数据计算匹配到的 location 或最终 body 限制。
*/
#include "ConfigRouteUtils.hpp"

/*
函数：stripQueryString
用途：从 URI 中去掉 ? 后面的 query string，只保留路径部分。
参数来源：findMatchingLocation 传入的 uri，例如 /api/upload?x=1。
变量解释：
    - uri：原始请求 URI。
    - query：? 的位置；找不到时为 npos。
实现逻辑：如果 URI 含有 ?，返回 ? 前面的路径；否则返回原 URI。
*/
static std::string stripQueryString(const std::string& uri)
{
    size_t query = uri.find('?');
    if (query != std::string::npos)
        return uri.substr(0, query);
    return uri;
}

/*
函数：findMatchingLocation
用途：根据 URI 找最长匹配 location。
参数来源：
    - uri：浏览器请求路径，可能带 query string。
    - locations：当前 server 的所有 LocationConfig。
    - use_location：输出变量；找到 location 时设 true，否则设 false。
变量解释：
    - best_match：目前找到的最长匹配 location。
    - longest_match：best_match 的 path 长度。
    - path：去掉 query string 后的 URI 路径。
实现逻辑：
    1. 去掉 uri 中 ? 后面的 query string。
    2. 遍历 locations。
    3. 如果 path 以 location.path 开头，说明匹配。
    4. 只保留 path 最长的匹配，保证 /upload/images/ 优先于 /upload/。
    5. 找到则设置 use_location=true 并返回指针；否则返回 NULL。
意义：RequestBody 在读取 body 前也需要 location 匹配，不能依赖 CGI/Response 模块。
*/
LocationConfig* findMatchingLocation(const std::string& uri, const std::vector<LocationConfig>& locations, bool &use_location)
{
    LocationConfig* best_match = NULL;
    use_location = false;
    size_t longest_match = 0;
    std::string path = stripQueryString(uri);

    for (size_t i = 0; i < locations.size(); ++i)
    {
        if (path.find(locations[i].path) == 0 && locations[i].path.length() > longest_match)
        {
            best_match = const_cast<LocationConfig*>(&locations[i]);
            use_location = true;
            longest_match = locations[i].path.length();
        }
    }
    return best_match;
}

/*
函数：getEffectiveBodyLimit
用途：返回当前请求应该使用的最大 body 大小。
参数来源：
    - server：当前连接所属的 ServerConfig，来自 Request.config。
    - uri：RequestParser 已经从请求行解析出的 URI。
变量解释：
    - use_location：findMatchingLocation 的输出标志；这里只需要配合函数调用。
    - loc：根据 uri 找到的最长匹配 location。
实现逻辑：
    1. 如果 server 为空，返回默认 MAX_BODY_SIZE。
    2. 用 findMatchingLocation 提前匹配 location。
    3. 如果 location 存在且 has_body_size=true，说明 location 明确覆盖 body 限制，返回 loc->max_body_size。
    4. 否则返回 server->max_body_size。
意义：Config 里存 location.max_body_size 后，RequestBody 会在读取 body 前调用这里，避免“只解析不生效”。
*/
unsigned long getEffectiveBodyLimit(const ServerConfig* server, const std::string& uri)
{
    if (server == NULL)
        return MAX_BODY_SIZE;

    bool use_location = false;
    LocationConfig* loc = findMatchingLocation(uri, server->locations, use_location);
    if (loc != NULL && loc->has_body_size)
        return loc->max_body_size;
    return server->max_body_size;
}
