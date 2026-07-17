/*
文件：srcs/Response/EffectiveRoute.cpp
用途：合并 server/location 配置，并把 Request.getPath() 提供的 normalized path 转成真实文件系统路径。
清理说明：不再重复截断 query、重复检查 URI，也不保存与路由无关的连接或方法临时状态。
路径边界：GET 在这里验证目标类型；POST 由上传目录负责验证，DELETE 由删除处理函数验证目标。
*/

/*
 * 【引用的库】
 * <cerrno> : 宿主，用这个库来获取系统底层操作（比如找文件）失败时的具体错误码（比如ENOENT代表找不到文件）。
 * <sys/stat.h> : 用它来获取文件或目录的属性信息，比如判断一个路径到底是个普通文件还是个文件夹。
 */
#include "EffectiveRoute.hpp"

#include <cerrno>
#include <sys/stat.h>

/*
 * 【：函数用途】
 * 这是 EffectiveRoute 类的构造函数，宿主可以把它理解为“出厂设置”，用来初始化类的所有内部变量。
 * 
 * 【参数与变量说明】
 * 本函数没有传入参数。内部操作的都是类的成员变量（如 server, location, isDir 等）。
 * 
 * 【实现逻辑】
 * 1. 利用初始化列表，把所有指针设为NULL，布尔值设为false，数字设为0。
 * 2. 字符串和容器类型会自动调用它们自己的默认构造函数清空。
 */
EffectiveRoute::EffectiveRoute()
    : server(NULL), location(NULL), root(), alias(), use_alias(false),
      autoindex(false), allow_methods(), index(), upload_path(),
      location_prefix("/"), redirect_status(0), redirect_url(),
      targetPath(), isDir(false)
{
}

/*
 * 【：函数用途】
 * 宿主，这个函数用来把特定的地址规则（location）覆盖到服务器的默认规则（server）上，生成最终用来找文件的“导航路线”。
 * 
 * 【参数与变量说明】
 * - srv (传入参数)：指向服务器整体配置的指针。
 * - loc (传入参数)：指向特定地址路径配置的指针。
 * 
 * 【实现逻辑】
 * 1. 第一步，检查传入的服务器配置和位置配置是不是空的，如果是空的就直接报错返回。
 * 2. 第二步，把传入的配置存到成员变量里，并清空所有旧的路由数据（防止数据污染）。
 * 3. 第三步，确定基础路径：如果位置配置里有别名（alias），就标记使用别名并记录；如果没有，就按位置配置或服务器配置找根目录（root）。
 * 4. 第四步，确定允许的方法：优先看位置配置，其次看服务器配置，都没写就默认给 GET/POST/DELETE。
 * 5. 第五步，确定默认首页（index）：也是位置优先，服务器其次，最后保底使用 "index.html"。
 * 6. 第六步，继承 autoindex 和上传路径的设定，并记录重定向的状态和网址，最后返回成功。
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
 * 【：函数用途】
 * 当用户的请求没有匹配到任何具体的 location 规则时，就用这个函数，仅使用全局的 server 规则来生成“导航路线”。
 * 
 * 【参数与变量说明】
 * - srv (传入参数)：指向服务器整体配置的指针。
 * 
 * 【实现逻辑】
 * 1. 检查服务器配置是否为空，为空则返回失败。
 * 2. 清空各种位置相关的属性，标记不使用别名。
 * 3. 必须要有根目录（root），没有就失败，有就记录下来。
 * 4. 设置允许的方法和默认首页，如果没有配置就用系统保底的默认值。
 * 5. 设置上传路径、自动索引等全局信息，然后返回成功。
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
 * 【：函数用途】
 * 这个函数负责把用户请求的虚拟路径，拼接到我们服务器在电脑上的真实路径（root 或 alias）上，算出文件真正在哪。
 * 
 * 【参数与变量说明】
 * - requestPath (传入参数)：用户请求的路径（比如 "/images/a.png"）。
 * - action (传入参数)：用户想干嘛（GET获取、POST上传、DELETE删除）。
 * - base (局部变量)：基础目录的路径。
 * - suffix (局部变量)：请求路径需要拼接的后缀部分。
 * 
 * 【实现逻辑】
 * 1. 检查用户的路径合不合法（不能为空且必须以斜杠开头）。
 * 2. 如果开启了别名（alias），就把请求路径里匹配到的前缀裁掉，剩下的部分当作后缀。
 * 3. 如果没开启别名，就直接把整个请求路径当后缀，基础目录就是 root。
 * 4. 调用 joinPaths 函数把基础目录和后缀拼起来，存到目标路径（targetPath）里。
 * 5. 最后调用 isValidPath 检查这个拼出来的路径在磁盘上是否合法，并返回HTTP状态码。
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

/*
 * 【：函数用途】
 * 宿主，这是个辅助工具函数，专门用来完美地拼接两个路径字符串，防止中间出现两个斜杠或者少了斜杠。
 * 
 * 【参数与变量说明】
 * - base (传入参数)：路径的前半截。
 * - suffix (传入参数)：路径的后半截。
 * - baseSlash (局部变量)：判断前半截最后是不是斜杠。
 * - suffixSlash (局部变量)：判断后半截开头是不是斜杠。
 * 
 * 【实现逻辑】
 * 1. 如果前半截空着，返回空；如果后半截空着，直接返回前半截。
 * 2. 看看两边有没有斜杠。如果两边都有斜杠，就把后半截的第一个斜杠切掉再拼。
 * 3. 如果两边都没斜杠，就在中间主动加一个斜杠再拼。
 * 4. 只有一边有斜杠的话，直接拼起来就行。
 */
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
 * 【：函数用途】
 * 此函数用来验证算出来的真实磁盘路径能不能用。只有GET请求需要在这里严格查磁盘，POST/DELETE因为有不同规则，在这先放行。
 * 
 * 【参数与变量说明】
 * - action (传入参数)：请求动作类型。
 * - pathInfo (局部变量)：用来装系统返回的文件属性信息。
 * 
 * 【实现逻辑】
 * 1. 先把是否是目录的标记（isDir）初始化为 false。
 * 2. 如果不是获取文件（GET）的请求，直接说路径没问题（放行交给后续专业函数处理）。
 * 3. 如果是GET请求，就去操作系统的磁盘上查这个文件（stat）。
 * 4. 如果查不到或者没有权限，根据操作系统的错误码返回对应的HTTP报错码（404或403）。
 * 5. 如果查到了，判断它是不是一个普通文件或文件夹。如果都不是，报错403。
 * 6. 如果查出来是个文件夹，就在路径最后贴心地补上一个斜杠，最后返回成功（PATH_OK）。
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