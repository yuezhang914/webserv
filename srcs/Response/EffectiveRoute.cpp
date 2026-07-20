/*
文件：srcs/Response/EffectiveRoute.cpp
用途：合并 server/location 配置，并把 Request.getPath() 提供的 normalized path 转成真实文件系统路径。
清理说明：不重复截断 query、不重复 percent-decoding，也不保存 Response 状态、连接策略或 CGI 进程状态。
路径边界：GET 在这里验证普通文件/目录；POST 验证配置 upload_path；DELETE 在真正 unlink 前检查目标。
*/
#include "EffectiveRoute.hpp"

#include <cerrno>
#include <sys/stat.h>

/*
函数：EffectiveRoute::EffectiveRoute
用途：创建一个尚未绑定 ServerConfig/LocationConfig 的空有效路由对象。
参数来源：无参数；由 buildResponse() 在每次请求分发时创建。
变量说明：所有字段都在初始化列表中设置；server/location 先为 NULL，布尔值为安全默认，容器和字符串为空。
实现逻辑：
    1. 清空所有配置覆盖结果。
    2. 默认不使用 alias、不 autoindex、不重定向。
    3. location_prefix 设为根路径，targetPath 尚未生成，isDir=false。
*/
EffectiveRoute::EffectiveRoute()
    : server(NULL), location(NULL), root(), alias(), use_alias(false),
      autoindex(false), allow_methods(), index(), upload_path(),
      location_prefix("/"), redirect_status(0), redirect_url(),
      targetPath(), isDir(false)
{
}

/*
函数：EffectiveRoute::createEffectiveRoute(ServerConfig*, LocationConfig*)
用途：把匹配 location 的显式规则覆盖到 server 默认规则上，得到本次请求最终路由。
参数来源：
    - srv：request.getConfig() 返回的当前 ServerConfig。
    - loc：findMatchingLocation(request.getPath(), srv->locations) 返回的最长匹配 location。
变量说明：函数主要写当前对象字段，没有额外复杂局部变量。
实现逻辑：
    1. 任一指针为空时返回 false。
    2. 重置上一次可能留下的 root/alias/method/index/targetPath/isDir。
    3. location 配置 alias 时启用 alias；否则优先 location.root，再继承 server.root。
    4. allow_methods、index、autoindex、upload_path 依次采用 location 显式值、server 默认值或项目安全默认。
    5. 保存 location path 供 alias 去前缀，保存 redirect 状态和 URL。
    6. 成功生成完整规则后返回 true。
*/
bool EffectiveRoute::createEffectiveRoute(const ServerConfig *srv,
                                          const LocationConfig *loc)
{
    if (srv == NULL || loc == NULL)
        return false;
    server = srv;
    location = loc;
    root.clear();
    alias.clear();
    allow_methods.clear();
    index.clear();
    targetPath.clear();
    isDir = false;

    if (loc->has_alias)
    {
        use_alias = true;
        alias = loc->alias;
    }
    else
    {
        use_alias = false;
        if (loc->has_root)
            root = loc->root;
        else if (srv->has_root)
            root = srv->root;
        else
            return false;
    }

    if (!loc->allow_methods.empty())
        allow_methods = loc->allow_methods;
    else if (!srv->allow_methods.empty())
        allow_methods = srv->allow_methods;
    else
    {
        allow_methods.insert("GET");
        allow_methods.insert("POST");
        allow_methods.insert("DELETE");
    }

    if (!loc->index.empty())
        index = loc->index;
    else if (!srv->index.empty())
        index = srv->index;
    else
        index.push_back("index.html");

    autoindex = loc->has_autoindex ? loc->autoindex : srv->autoindex;
    if (!loc->upload_path.empty())
        upload_path = loc->upload_path;
    else if (!srv->upload_path.empty())
        upload_path = srv->upload_path;
    else
        upload_path = "/upload/";

    location_prefix = loc->path;
    redirect_status = loc->redirect_status;
    redirect_url = loc->redirect_url;
    return true;
}

/*
函数：EffectiveRoute::createEffectiveRoute(ServerConfig*)
用途：没有 location 匹配时，仅使用 server 级规则和项目默认值建立路由。
参数来源：srv 来自 request.getConfig()；buildResponse() 在 location==NULL 时调用本重载。
变量说明：无额外复杂局部变量；函数直接填充当前对象字段。
实现逻辑：
    1. srv 为空或没有有效 root 时返回 false。
    2. 清空旧字段，并明确关闭 alias 模式。
    3. 复制 server.root。
    4. allow_methods 为空时默认 GET/POST/DELETE。
    5. index 为空时默认 index.html。
    6. upload_path 为空时默认 /upload/，复制 autoindex。
    7. 清除 redirect，设置根 location 前缀并返回 true。
*/
bool EffectiveRoute::createEffectiveRoute(const ServerConfig *srv)
{
    if (srv == NULL)
        return false;
    server = srv;
    location = NULL;
    root.clear();
    alias.clear();
    allow_methods.clear();
    index.clear();
    targetPath.clear();
    isDir = false;
    use_alias = false;

    if (!srv->has_root)
        return false;
    root = srv->root;

    if (!srv->allow_methods.empty())
        allow_methods = srv->allow_methods;
    else
    {
        allow_methods.insert("GET");
        allow_methods.insert("POST");
        allow_methods.insert("DELETE");
    }

    if (!srv->index.empty())
        index = srv->index;
    else
        index.push_back("index.html");

    upload_path = srv->upload_path.empty() ? "/upload/" : srv->upload_path;
    autoindex = srv->autoindex;
    location_prefix = "/";
    redirect_status = 0;
    redirect_url.clear();
    return true;
}

/*
函数：EffectiveRoute::createEffectivePath
用途：把 normalized request path 映射到 root 或 alias 下的真实 targetPath，并执行当前动作需要的基础路径验证。
参数来源：
    - requestPath：RequestParser 已解码、规范化、去 query 的 request.getPath()。
    - action：buildResponse() 通过 requestActionFromMethod() 得到的 GET/POST/DELETE 动作。
变量说明：
    - base：最终使用的 root 或 alias 目录。
    - suffix：需要追加到 base 的 URL path 部分；alias 模式会先移除 location_prefix。
实现逻辑：
    1. requestPath 必须非空且以 / 开头，否则返回 400。
    2. alias 模式检查 requestPath 确实属于当前 location，再移除 location 前缀。
    3. root 模式直接使用完整 normalized path 作为 suffix。
    4. 调用 joinPaths() 生成 targetPath；空结果返回 500。
    5. 调用 isValidPath(action)：GET 立即 stat，POST/DELETE 暂留给对应处理函数。
*/
int EffectiveRoute::createEffectivePath(const std::string &requestPath,
                                        RequestAction action)
{
    if (requestPath.empty() || requestPath[0] != '/')
        return 400;
    std::string base;
    std::string suffix;

    if (use_alias)
    {
        base = alias;
        if (requestPath.compare(0, location_prefix.size(),
            location_prefix) != 0)
            return 404;
        suffix = requestPath.substr(location_prefix.size());
    }
    else
    {
        base = root;
        suffix = requestPath;
    }
    targetPath = joinPaths(base, suffix);
    if (targetPath.empty())
        return 500;
    return isValidPath(action);
}

/*
函数：joinPaths
用途：连接两个文件系统路径片段，并只处理交界处缺少或重复斜杠的问题。
参数来源：
    - base：EffectiveRoute.root/alias 或上传基础目录。
    - suffix：normalized path、upload_path 或文件名片段。
变量说明：baseSlash/suffixSlash 分别记录两段边界是否已有 /。
实现逻辑：
    1. base 为空时返回空，避免生成无根目标。
    2. suffix 为空时直接返回 base。
    3. 两边都有斜杠时删除 suffix 首斜杠。
    4. 两边都没有斜杠时补一个。
    5. 其余情况直接拼接。
*/
std::string joinPaths(const std::string &base,
                      const std::string &suffix)
{
    if (base.empty())
        return std::string();
    if (suffix.empty())
        return base;
    bool baseSlash = base[base.size() - 1] == '/';
    bool suffixSlash = suffix[0] == '/';
    if (baseSlash && suffixSlash)
        return base + suffix.substr(1);
    if (!baseSlash && !suffixSlash)
        return base + "/" + suffix;
    return base + suffix;
}

/*
函数：EffectiveRoute::isValidPath
用途：验证 GET targetPath 存在且只能是普通文件或目录；POST/DELETE 保留到真正操作时验证。
参数来源：action 由 createEffectivePath() 原样传入；targetPath 已在同一对象中生成。
变量说明：pathInfo 是 stat() 填充的文件类型/权限信息。
实现逻辑：
    1. 重置 isDir=false。
    2. 非 GET 直接返回 PATH_OK，因为 POST 使用 upload_path，DELETE 需要更精确 errno 映射。
    3. GET stat 失败时把不存在映射 404、权限/环/过长映射 403、其他映射 500。
    4. 目标不是普通文件或目录时返回 403，避免 FIFO/设备/socket 被读取。
    5. 目录时设置 isDir=true 并保证 targetPath 末尾有 /。
*/
int EffectiveRoute::isValidPath(RequestAction action)
{
    isDir = false;
    if (action != ACTION_GET)
        return PATH_OK;

    struct stat pathInfo;
    if (::stat(targetPath.c_str(), &pathInfo) != 0)
    {
        switch (errno)
        {
            case ENOENT:
            case ENOTDIR:
                return 404;
            case EACCES:
            case EPERM:
            case ELOOP:
            case ENAMETOOLONG:
                return 403;
            default:
                return 500;
        }
    }

    if (!S_ISREG(pathInfo.st_mode) && !S_ISDIR(pathInfo.st_mode))
        return 403;
    isDir = S_ISDIR(pathInfo.st_mode);
    if (isDir && targetPath[targetPath.size() - 1] != '/')
        targetPath += '/';
    return PATH_OK;
}
