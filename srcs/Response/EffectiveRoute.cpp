/*
文件：srcs/Response/EffectiveRoute.cpp
用途：合并 server/location 配置，并把 Request.getPath() 提供的 normalized path 转成真实文件系统路径。
清理说明：不再重复截断 query、重复检查 URI，也不保存与路由无关的连接或方法临时状态。
路径边界：GET 在这里验证目标类型；POST 由上传目录负责验证，DELETE 由删除处理函数验证目标。
*/
#include "EffectiveRoute.hpp"

#include <cerrno>
#include <sys/stat.h>

EffectiveRoute::EffectiveRoute()
    : server(NULL), location(NULL), root(), alias(), use_alias(false),
      autoindex(false), allow_methods(), index(), upload_path(),
      location_prefix("/"), redirect_status(0), redirect_url(),
      targetPath(), isDir(false)
{
}

/* 用 location 显式规则覆盖 server 默认规则，生成最终路由。 */
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

/* 没有 location 匹配时，使用 server 级规则和默认值生成最终路由。 */
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
函数：createEffectivePath
用途：把已经由 RequestParser 解码、规范化并去除 query 的 requestPath 拼到 root 或 alias。
规则：GET 立即验证目标；POST 只保留 URI 文件名，上传目录由 handlePost 验证；DELETE 由 handleDelete 对目标执行 stat/unlink。
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
        if (requestPath.compare(0, location_prefix.size(), location_prefix) != 0)
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

std::string joinPaths(const std::string &base, const std::string &suffix)
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
函数：isValidPath
用途：GET 目标必须存在且是普通文件或目录；POST/DELETE 在各自处理函数中检查真正使用的目标。
返回：PATH_OK 或可直接用于 Response 的 403/404/500 状态码。
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
