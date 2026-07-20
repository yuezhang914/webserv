/*
文件：srcs/Response/ResponseBuilder.cpp
用途：实现一次请求的顶层响应分发，负责合并路由、处理重定向、校验方法与 CGI 目标，并把普通请求交给对应 handler。
拆分说明：函数从原 Response.cpp 按“请求分发”职责原样移动，不改变 buildResponse() 的公开接口或分支顺序。
*/
#include "Response.hpp"
#include "ConfigRouteUtils.hpp"
#include "EffectiveRoute.hpp"
#include "Request.hpp"
#include "RequestHandler.hpp"

#include <cerrno>
#include <sys/stat.h>
#include <unistd.h>

/*
函数：buildAllowHeader
用途：为 405 Method Not Allowed 生成稳定顺序的 Allow header。
参数来源：allowMethods 来自 EffectiveRoute 合并后的 server/location allow_methods。
变量说明：
    - orderedMethods：本项目真正实现的三个方法及输出顺序。
    - result：逐步拼出的 header value，例如 "GET, POST"。
    - i：遍历 orderedMethods 的下标。
实现逻辑：
    1. 按 GET、POST、DELETE 的固定顺序检查集合。
    2. 找到允许的方法时，在非首项前加入逗号和空格。
    3. 返回可以直接交给 Response::setHeader("Allow", ...) 的字符串。
*/
static std::string buildAllowHeader(const std::set<std::string> &allowMethods)
{
    const char *orderedMethods[] = {"GET", "POST", "DELETE"};
    std::string result;
    size_t i = 0;
    while (i < 3)
    {
        if (allowMethods.find(orderedMethods[i]) != allowMethods.end())
        {
            if (!result.empty())
                result += ", ";
            result += orderedMethods[i];
        }
        ++i;
    }
    return result;
}


/*
函数：isConfiguredCgiPath
用途：判断 normalized request path 是否以当前 location 配置的 CGI 后缀结尾。
参数来源：
    - location：buildResponse() 通过 findMatchingLocation() 找到的最长匹配 location。
    - path：RequestParser 已经解码、规范化并去掉 query 的 request.getPath()。
变量说明：
    - it：遍历 location->cgi_extensions 的迭代器；key 是 .py/.sh 等后缀，value 是队友配置的解释器路径。
    - extension：当前检查的 CGI 后缀。
实现逻辑：
    1. 没有 location 或没有 cgi_extension 配置时返回 false。
    2. 逐个检查 path 最后是否完整等于某个扩展名。
    3. 只负责识别是否走 CGI；解释器 value 不在 Response 中使用，因为队友公开接口只接收 script_path。
*/
static bool isConfiguredCgiPath(const LocationConfig *location,
                                const std::string &path)
{
    if (location == NULL || location->cgi_extensions.empty())
        return false;
    std::map<std::string, std::string>::const_iterator it =
        location->cgi_extensions.begin();
    while (it != location->cgi_extensions.end())
    {
        const std::string &extension = it->first;
        if (!extension.empty() && path.size() >= extension.size()
            && path.compare(path.size() - extension.size(),
                extension.size(), extension) == 0)
            return true;
        ++it;
    }
    return false;
}

/*
函数：validateCgiScript
用途：在 fork/execve 前确认 EffectiveRoute 生成的 CGI 目标确实是可执行普通文件。
参数来源：scriptPath 来自 route.targetPath，即 root/alias 与 request.getPath() 合成后的真实磁盘路径。
变量说明：
    - info：stat() 填充的文件类型和权限信息。
实现逻辑：
    1. stat 失败时把不存在/中间目录不存在映射为 404，权限失败映射为 403，其余映射为 500。
    2. 目标不是普通文件时返回 403，避免目录、FIFO、socket 或设备进入 execve。
    3. access(X_OK) 失败时返回 403。
    4. 所有检查通过返回 PATH_OK，buildResponse() 随后返回 RESPONSE_BUILD_CGI 和该脚本路径。
*/
static int validateCgiScript(const std::string &scriptPath)
{
    struct stat info;
    if (stat(scriptPath.c_str(), &info) != 0)
    {
        if (errno == ENOENT || errno == ENOTDIR)
            return 404;
        if (errno == EACCES || errno == EPERM)
            return 403;
        return 500;
    }
    if (!S_ISREG(info.st_mode))
        return 403;
    if (access(scriptPath.c_str(), X_OK) != 0)
        return 403;
    return PATH_OK;
}

/*
函数：buildResponse
用途：保留其他模块已经使用的 Response 返回接口，完成路由、方法、路径和 CGI 分发判断。
参数来源：request 由 RequestParser 完整解析并由 ServerManager 传入。
变量说明：
    - server/location/route：分别表示当前 server 配置、最长匹配 location 和合并后的有效路由。
    - response：当前请求的默认响应，继承 Request 的 Connection 策略。
    - action/pathStatus/cgiPathStatus：分别保存方法枚举、普通路径检查结果和 CGI 脚本检查结果。
实现逻辑：
    1. 检查 ServerConfig 并找到最长匹配 location，合并 EffectiveRoute。
    2. 优先处理 redirect，再检查方法是否实现以及是否被配置允许。
    3. 生成真实磁盘路径；CGI 路径先验证存在、普通文件和执行权限。
    4. CGI 请求不在 Response 内运行，只把脚本路径写入现有 ServerManager 已识别的 X-Internal-CGI-Path 内部 header。
    5. 非 CGI 请求按 GET、POST、DELETE 返回最终 Response。
接口边界：返回类型保持 Response，避免要求 ServerManager 改用新的分发结果类型。
*/
Response buildResponse(const Request &request)
{
    const ServerConfig *server = request.getConfig();
    Response response(request);
    if (server == NULL)
    {
        Response::ErrorPageMap noErrorPages;
        response.createResponse(500, "", noErrorPages);
        return response;
    }

    const LocationConfig *location =
        findMatchingLocation(request.getPath(), server->locations);
    EffectiveRoute route;
    bool routeReady = location != NULL
        ? route.createEffectiveRoute(server, location)
        : route.createEffectiveRoute(server);
    if (!routeReady)
    {
        response.createResponse(500, "", server->error_pages);
        return response;
    }

    if (location != NULL && route.redirect_status >= 300
        && route.redirect_status <= 399 && !route.redirect_url.empty())
    {
        response.setStatus(route.redirect_status);
        response.setHeader("Location", route.redirect_url);
        if (route.redirect_status != 304)
        {
            response.setHeader("Content-Type", "text/html");
            response.setBody("<!DOCTYPE html><html><head><title>Redirect</title></head><body>Redirecting</body></html>");
        }
        return response;
    }

    RequestAction action = requestActionFromMethod(request.getMethod());
    if (action == ACTION_UNSUPPORTED)
    {
        response.createResponse(501, "", route.server->error_pages);
        return response;
    }
    if (!isMethodAllowed(action, route.allow_methods))
    {
        response.createResponse(405, "", route.server->error_pages);
        response.setHeader("Allow", buildAllowHeader(route.allow_methods));
        return response;
    }

    int pathStatus = route.createEffectivePath(request.getPath(), action);
    if (isConfiguredCgiPath(location, request.getPath()))
    {
        int cgiPathStatus = validateCgiScript(route.targetPath);
        if (cgiPathStatus != PATH_OK)
        {
            response.createResponse(cgiPathStatus, "",
                route.server->error_pages);
            return response;
        }
        response.setStatus(200);
        response.setHeader("X-Internal-CGI-Path", route.targetPath);
        return response;
    }

    if (pathStatus != PATH_OK)
    {
        response.createResponse(pathStatus, "", route.server->error_pages);
        return response;
    }
    if (action == ACTION_GET)
        return handleGet(request, route);
    if (action == ACTION_POST)
        return handlePost(request, route);
    return handleDelete(request, route);
}

