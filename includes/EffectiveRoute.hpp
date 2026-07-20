/*
文件：includes/EffectiveRoute.hpp
用途：声明 Response 模块内部的请求动作、合并后路由数据，以及 URL 到真实文件系统路径的协作接口。
拆分说明：配置合并实现在 EffectiveRoute.cpp，真实路径生成与 stat 校验实现在 EffectivePath.cpp。
*/
#ifndef EFFECTIVE_ROUTE_HPP
#define EFFECTIVE_ROUTE_HPP

/*
包含：<set>
用途：使用 std::set 保存最终允许的 HTTP 方法集合。
*/
#include <set>

/*
包含：<string>
用途：保存 root、alias、redirect、upload_path 和最终 targetPath。
*/
#include <string>

/*
包含：<vector>
用途：保存按配置顺序尝试的 index 文件名列表。
*/
#include <vector>

/*
包含：LocationConfig.hpp
用途：读取最长匹配 location 的 root、alias、methods、index、autoindex、upload 和 redirect 覆盖项。
*/
#include "LocationConfig.hpp"

/*
包含：ServerConfig.hpp
用途：读取当前 Request 绑定的 server 默认配置和错误页来源。
*/
#include "ServerConfig.hpp"

/*
枚举：RequestAction
用途：把 Request::getMethod() 映射为 Response 内部动作，替代旧 GET/POST/DELETE 宏。
数据来源：requestActionFromMethod() 根据解析后的 method 返回其中一个值。
*/
enum RequestAction
{
    ACTION_UNSUPPORTED = 0,
    ACTION_GET,
    ACTION_POST,
    ACTION_DELETE
};

/*
枚举：EffectivePathStatus
用途：表示路径创建和验证成功；失败直接使用对应 HTTP 状态码返回。
设计说明：PATH_OK 为 0，其他返回值是 400、403、404 或 500。
*/
enum EffectivePathStatus
{
    PATH_OK = 0
};

/*
结构体：EffectiveRoute
用途：保存 server 配置与可选 location 覆盖合并后的最终处理规则。
成员来源：createEffectiveRoute() 从 ServerConfig/LocationConfig 写入；createEffectivePath() 写入 targetPath 和 isDir。
模块边界：它只描述路由和文件系统目标，不保存 Response body、headers 或连接状态。
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
    int redirect_status;                 /* 3xx 状态；0 表示没有重定向。 */
    std::string redirect_url;            /* 重定向目标。 */
    std::string targetPath;              /* root/alias 与 normalized path 合成的真实路径。 */
    bool isDir;                          /* GET 目标是否为目录。 */

    /*
    函数：EffectiveRoute
    用途：创建尚未绑定配置和路径的空路由对象。
    参数：无。
    实现逻辑：指针设为 NULL、布尔值使用安全默认、字符串和容器置空。
    */
    EffectiveRoute();

    /*
    函数：createEffectiveRoute(server, location)
    用途：把 location 显式配置覆盖到 server 默认配置上。
    参数：srv 来自 Request::getConfig()；loc 来自最长 location 匹配。
    返回：配置完整时返回 true，指针为空或 root/alias 缺失时返回 false。
    */
    bool createEffectiveRoute(const ServerConfig *srv,
                              const LocationConfig *loc);

    /*
    函数：createEffectiveRoute(server)
    用途：没有匹配 location 时仅使用 server 级配置建立路由。
    参数：srv 来自 Request::getConfig()。
    返回：server 和 root 有效时返回 true，否则返回 false。
    */
    bool createEffectiveRoute(const ServerConfig *srv);

    /*
    函数：createEffectivePath
    用途：把 normalized URL path 映射到 root 或 alias 下的真实路径。
    参数：requestPath 来自 Request::getPath()；action 是当前内部动作。
    返回：成功返回 PATH_OK，失败返回对应 HTTP 状态码。
    */
    int createEffectivePath(const std::string &requestPath,
                            RequestAction action);

    /*
    函数：isValidPath
    用途：验证 GET 目标存在且为普通文件或目录，并写入 isDir。
    参数：action 由 createEffectivePath() 传入。
    返回：成功返回 PATH_OK，失败返回 403、404 或 500；POST/DELETE 暂缓到 handler 检查。
    */
    int isValidPath(RequestAction action);
};

/*
函数：joinPaths
用途：连接两个路径片段，并统一处理交界处缺少或重复斜杠。
参数：base 是 root/alias/upload 基础目录；suffix 是 normalized path 或文件名片段。
返回：base 为空时返回空字符串，否则返回拼接后的路径。
*/
std::string joinPaths(const std::string &base,
                      const std::string &suffix);

#endif
