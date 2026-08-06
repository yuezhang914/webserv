/*
文件：srcs/Response/ResponseBuilder.cpp
用途：实现项目唯一的顶层响应分发，负责 Session 虚拟路由、配置合并、重定向、方法权限、CGI 目标和普通文件请求。
拆分说明：所有请求都必须传入 ServerManager 长期持有的共享 SessionStore，不再保留可能绕过 Session 的旧入口。
*/
/*
包含：Response.hpp
用途：使用 Response 类型以及唯一的带共享 SessionStore 的 buildResponse() 声明。
*/
#include "Response.hpp"

/*
包含：ConfigRouteUtils.hpp
用途：查找最长匹配 location，并复用配置路由辅助函数。
*/
#include "ConfigRouteUtils.hpp"

/*
包含：EffectiveRoute.hpp
用途：合并 server/location 规则，生成真实路径和 RequestAction。
*/
#include "EffectiveRoute.hpp"

/*
包含：Request.hpp
用途：通过只读 getter 获取 method、path、config 和连接策略。
*/
#include "Request.hpp"

/*
包含：RequestHandler.hpp
用途：把普通 GET、POST、DELETE 请求交给对应文件处理函数。
*/
#include "RequestHandler.hpp"

/*
包含：SessionResponse.hpp
用途：识别并构造 counter、login、logout 三个 Session 虚拟路由。
*/
#include "SessionResponse.hpp"

/*
包含：<map>
用途：遍历 LocationConfig 中 CGI 后缀到解释器的映射。
*/
#include <map>

/*
包含：<set>
用途：读取 EffectiveRoute 的 allow_methods 并生成 Allow header。
*/
#include <set>

/*
包含：<string>
用途：处理方法名、脚本路径、重定向正文和内部 CGI 路径 header。
*/
#include <string>

/*
包含：<cerrno>
用途：在 stat() 失败后映射 CGI 脚本路径错误；不在 read/write 后依赖 errno。
*/
#include <cerrno>

/*
包含：<sys/stat.h>
用途：使用 stat() 和 S_ISREG 检查 CGI 目标是否为普通文件。
*/
#include <sys/stat.h>

/*
包含：<unistd.h>
用途：使用 access(X_OK) 检查 CGI 脚本执行权限。
*/
#include <unistd.h>

/*
函数：buildAllowHeader
用途：为 405 Method Not Allowed 生成稳定顺序的 Allow header。
参数来源：allowMethods 来自 EffectiveRoute 合并后的 server/location allow_methods。
变量说明：result 逐步拼出 header value，例如 "GET, POST, DELETE"。
实现逻辑：
    1. 按 GET、POST、DELETE 的稳定顺序列出配置中真正允许的方法。
    2. 不把 HEAD 隐式加入 GET，因为学校 tester 要求未配置的 HEAD 返回 405。
    3. 返回可以直接交给 Response::setHeader("Allow", ...) 的字符串。
*/
static std::string buildAllowHeader(const std::set<std::string> &allowMethods)
{
    std::string result;
    if (allowMethods.find("GET") != allowMethods.end())
        result = "GET";
    if (allowMethods.find("POST") != allowMethods.end())
    {
        if (!result.empty())
            result += ", ";
        result += "POST";
    }
    if (allowMethods.find("DELETE") != allowMethods.end())
    {
        if (!result.empty())
            result += ", ";
        result += "DELETE";
    }
    return result;
}

/*
函数：findConfiguredCgiInterpreter
用途：判断 normalized request path 是否匹配当前 location 的 CGI 后缀，并取出该后缀配置的解释器。
参数来源：
    - location：buildResponse() 通过 findMatchingLocation() 找到的最长匹配 location。
    - path：RequestParser 已解码、规范化并去掉 query 的 request.getPath()。
    - interpreter：输出引用；成功时写入 cgi_extensions value，空值表示脚本自身可直接执行。
变量说明：it 遍历后缀到解释器的映射；extension 是当前后缀。
实现逻辑：没有 CGI 配置时返回 false；逐个比较完整文件后缀；命中后写出解释器并返回 true。
*/

#include <limits.h>
#include <stdlib.h>

static bool findConfiguredCgiInterpreter(const LocationConfig *location,
                                         const std::string &path,
                                         std::string &interpreter)
{
    interpreter.clear();
    if (location == NULL || location->cgi_extensions.empty())
        return false;

    std::map<std::string, std::string>::const_iterator it =
        location->cgi_extensions.begin();
    while (it != location->cgi_extensions.end())
    {
        const std::string &extension = it->first;
        if (!extension.empty() && 
            path.size() >= extension.size() && 
            path.compare(path.size() - extension.size(), extension.size(), extension) == 0)
        {
            std::string rawInterpreter = it->second;

            // 💡 将配置的解释器路径直接转为系统绝对路径
            char absPath[PATH_MAX];
            if (realpath(rawInterpreter.c_str(), absPath) != NULL)
            {
                interpreter = std::string(absPath);
            }
            else
            {
                // 如果 realpath 转换失败（说明物理文件确实不存在），退回原始路径
                interpreter = rawInterpreter;
            }
            return true;
        }
        ++it;
    }
    return false;
}

/*
函数：extractPathInfo
用途：按 location 配置的 CGI 扩展名，把 normalized path 拆成脚本路径和 PATH_INFO。
参数来源：rawPath 来自 Request::getPath()；location 是最长匹配 location；scriptPath/pathInfo 是输出引用。
变量说明：it 遍历扩展名配置；extPos/extEnd 定位脚本扩展名结束位置。
实现逻辑：默认整条路径都是脚本路径；若扩展名后紧跟 /，则把后半段保存为 PATH_INFO。
*/
static void extractPathInfo(const std::string &rawPath,
                            const LocationConfig *location,
                            std::string &scriptPath,
                            std::string &pathInfo)
{
    scriptPath = rawPath;
    pathInfo = "";

    if (location == NULL || location->cgi_extensions.empty())
        return;

    typedef std::map<std::string, std::string>::const_iterator MapIterator;
    for (MapIterator it = location->cgi_extensions.begin(); it != location->cgi_extensions.end(); ++it)
    {
        const std::string &ext = it->first;
        if (ext.empty())
            continue;

        size_t extPos = rawPath.find(ext);
        if (extPos != std::string::npos)
        {
            size_t extEnd = extPos + ext.length();
            if (extEnd == rawPath.length())
            {
                scriptPath = rawPath;
                pathInfo = "";
                return;
            }
            else if (rawPath[extEnd] == '/')
            {
                scriptPath = rawPath.substr(0, extEnd);
                pathInfo = rawPath.substr(extEnd);
                return;
            }
        }
    }
}

/*
函数：validateCgiScript
用途：在 fork/execve 前确认 EffectiveRoute 生成的 CGI 目标是普通文件，并按启动方式检查读或执行权限。
参数来源：scriptPath 来自 route.targetPath；requiresExecutePermission 在没有配置解释器时为 true。
变量说明：
    - info：stat() 填充的文件类型和权限信息。
实现逻辑：
    1. stat 失败时把不存在/中间目录不存在映射为 404，权限失败映射为 403，其余映射为 500。
    2. 目标不是普通文件时返回 403，避免目录、FIFO、socket 或设备进入 execve。
    3. 直接执行脚本时检查 X_OK；通过解释器执行时只要求 R_OK。
    4. 所有检查通过返回 PATH_OK，buildResponse() 随后通过内部 header 交付脚本路径。
*/
static int validateCgiScript(const std::string &scriptPath,
                             bool requiresExecutePermission)
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
    int requiredMode = requiresExecutePermission ? X_OK : R_OK;
    if (access(scriptPath.c_str(), requiredMode) != 0)
        return 403;
    return PATH_OK;
}

/*
函数：buildResponse
用途：作为项目唯一顶层入口，依次处理配置、重定向、方法权限、Session 路由、CGI 和普通文件请求。
参数来源：
    - request：由 RequestParser 完整解析后由 ServerManager 传入。
    - sessionStore：由 ServerManager 长期持有的服务器级共享对象，不属于 Response 所有。
变量说明：
    - server/location/route：当前 server、最长匹配 location 和合并后的有效路由。
    - response：继承 Request 连接策略的默认响应。
    - action/pathStatus/cgiPathStatus：方法枚举、普通路径检查和 CGI 脚本检查结果。
实现逻辑：
    1. 检查 Request 是否绑定 ServerConfig，并合并 server/location 路由。
    2. 优先执行配置重定向；随后检查方法是否实现，并按配置精确检查权限，HEAD 不隐式继承 GET。
    3. 精确匹配三个 Session 示例路径时，把共享 store 交给 SessionResponse 层。
    4. 其他请求生成真实路径；CGI 后缀先验证可执行脚本并交付内部路径 header。
    5. 普通 GET、POST、DELETE 分别交给 RequestHandler；若未来显式支持 HEAD，则复用 GET 结果并移除实际 body。
接口约束：本项目不再提供不带 SessionStore 的旧重载，避免调用方误绕过 Session 功能。
*/

Response buildResponse(const Request &request,
                       SessionStore &sessionStore)
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

    /* CGI 请求可能在脚本扩展名后携带 PATH_INFO。 */
    std::string scriptPath, pathInfo;
    extractPathInfo(request.getPath(), location, scriptPath, pathInfo);

    EffectiveRoute route;
    bool routeReady = location != NULL
                          ? route.createEffectiveRoute(server, location)
                          : route.createEffectiveRoute(server);
    if (!routeReady)
    {
        response.createResponse(500, "", server->error_pages);
        return response;
    }

    if (location != NULL && route.redirect_status >= 300 && route.redirect_status <= 399 && !route.redirect_url.empty())
    {
        response.setStatus(route.redirect_status);
        response.setHeader("Location", route.redirect_url);
        if (route.redirect_status != 304)
        {
            response.setHeader("Content-Type", "text/html");
            response.setBody("<!DOCTYPE html><html><head><title>Redirect"
                             "</title></head><body>Redirecting</body></html>");
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
        if (request.getMethod() == "HEAD")
            response.clearBody();
        return response;
    }

    if (isSessionDemoPath(request.getPath()))
        return buildSessionDemoResponse(request, sessionStore);

    /* 先识别 CGI 后缀。ACTION_CGI 只负责建立 targetPath，不在通用
       GET 路径检查中提前 stat；脚本和解释器随后分别按 CGI 规则验证。 */
    std::string cgiInterpreter;
    bool isCgi = findConfiguredCgiInterpreter(location, scriptPath,
                                               cgiInterpreter);
    RequestAction effectiveAction = isCgi ? ACTION_CGI : action;
    int pathStatus = route.createEffectivePath(scriptPath, effectiveAction);

    if (isCgi)
    {
        /* CGI 脚本本身必须存在且是普通文件。通过解释器启动时脚本只需
           可读；直接 execve 脚本时必须拥有执行权限。 */
        int cgiPathStatus = validateCgiScript(route.targetPath,
                                              cgiInterpreter.empty());
        if (cgiPathStatus != PATH_OK)
        {
            response.createResponse(cgiPathStatus, "",
                                    route.server->error_pages);
            return response;
        }

        /* 配置了解释器时，还要独立确认解释器是可执行普通文件。
           不能只验证解释器而跳过脚本，否则缺失脚本会延迟到异步层变成 502。 */
        if (!cgiInterpreter.empty())
        {
            int interpreterStatus = validateCgiScript(cgiInterpreter, true);
            if (interpreterStatus != PATH_OK)
            {
                response.createResponse(interpreterStatus, "",
                                        route.server->error_pages);
                return response;
            }
        }

        response.setStatus(200);
        response.setHeader("X-Internal-CGI-Path", route.targetPath);
        if (!cgiInterpreter.empty())
            response.setHeader("X-Internal-CGI-Interpreter", cgiInterpreter);

        /* 通过内部 headers 把 CGI 路径信息交给异步执行层。 */
        response.setHeader("X-Internal-CGI-Script-Name", scriptPath);
        response.setHeader("X-Internal-CGI-Path-Info", pathInfo);
        response.setHeader("X-Internal-CGI-Document-Root",
                           route.use_alias ? route.alias : route.root);

        return response;
    }

    /* 非 CGI 请求继续使用通用路径状态；CGI 已在上方完成专用验证。 */
    if (pathStatus != PATH_OK)
    {
        response.createResponse(pathStatus, "", route.server->error_pages);
        return response;
    }

    /* HEAD 使用 GET 的状态和 headers，但最终不保留实际消息体。 */
    if (action == ACTION_GET || action == ACTION_HEAD)
    {
        Response res = handleGet(request, route);

        if (action == ACTION_HEAD)
            res.clearBodyOnly();
        return res;
    }
    if (action == ACTION_POST)
        return handlePost(request, route);
    return handleDelete(request, route);
}