/*
文件：includes/EffectiveRoute.hpp
用途：声明 Response 模块内部使用的请求动作类型、合并后路由数据以及 URL 到文件系统路径的公开协作接口。
拆分说明：配置合并实现在 EffectiveRoute.cpp，真实路径生成与 stat 校验实现在 EffectivePath.cpp。
*/
#ifndef EFFECTIVE_ROUTE_HPP
#define EFFECTIVE_ROUTE_HPP

#include <set>
#include <string>
#include <vector>

#include "LocationConfig.hpp"
#include "ServerConfig.hpp"

/*
枚举：RequestAction
用途：把 Request.getMethod() 返回值映射成 Response 模块内部动作，替代旧 GET/POST/DELETE 宏。
放置说明：该类型同时被 EffectiveRoute 和 RequestHandler 使用，因此合并到最先依赖它的 EffectiveRoute.hpp，不再单独保留只有枚举的 RequestAction.hpp。
*/
enum RequestAction
{
    ACTION_UNSUPPORTED = 0,
    ACTION_GET,
    ACTION_POST,
    ACTION_DELETE
};

enum EffectivePathStatus
{
    PATH_OK = 0
};

/*
结构体：EffectiveRoute
用途：保存 server 配置与可选 location 覆盖合并后的最终处理规则。
边界：它只描述路由、文件系统目标和方法规则；Response 的状态、body 和连接策略不属于本结构。
*/
struct EffectiveRoute
{
    const ServerConfig *server;          /* 当前请求使用的 server 配置。 */
    const LocationConfig *location;      /* 最长匹配 location；没有时为 NULL。 */
    std::string root;                    /* 最终 root。 */
    std::string alias;                   /* 最终 alias。 */
    bool use_alias;                      /* 是否使用 alias 拼接路径。 */
    bool autoindex;                      /* 最终 autoindex 规则。 */
    std::set<std::string> allow_methods; /* 最终允许方法集合。 */
    std::vector<std::string> index;      /* 最终 index 候选列表。 */
    std::string upload_path;             /* 最终上传目录。 */
    std::string location_prefix;         /* alias 模式需要移除的 location 前缀。 */
    int redirect_status;                 /* 3xx 重定向状态；0 表示没有。 */
    std::string redirect_url;            /* 重定向目标。 */
    std::string targetPath;              /* root/alias 与 normalized path 合成的真实路径。 */
    bool isDir;                          /* GET 目标是否为目录。 */

    EffectiveRoute();
    bool createEffectiveRoute(const ServerConfig *srv,
                              const LocationConfig *loc);
    bool createEffectiveRoute(const ServerConfig *srv);
    int createEffectivePath(const std::string &requestPath,
                            RequestAction action);
    int isValidPath(RequestAction action);
};

/* 连接两个路径片段，统一处理边界处有无斜杠。 */
std::string joinPaths(const std::string &base, const std::string &suffix);

#endif
