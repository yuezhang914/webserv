/*
文件：srcs/Response/EffectiveRoute.cpp
用途：构造 EffectiveRoute，并合并 server 与 location 配置，得到当前请求最终使用的路由规则。
拆分说明：真实路径生成和 stat 验证已移动到 EffectivePath.cpp；配置继承逻辑保持不变。
*/
/*
包含：EffectiveRoute.hpp
用途：取得 EffectiveRoute、ServerConfig、LocationConfig 和成员函数声明。
*/
#include "EffectiveRoute.hpp"

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

