/*
文件：srcs/Response/ResponseBuilder.cpp
用途：实现项目唯一的顶层响应分发，负责 Session 虚拟路由、配置合并、重定向、方法权限、CGI 目标和普通文件请求。
拆分说明：所有请求都必须传入 ServerManager 长期持有的共享 SessionStore，不再保留可能绕过 Session 的旧入口。
*/
/*
包含：Response.hpp
用途：使用 Response 类型以及唯一的带共享 SessionStore 的 buildResponse() 声明。
*/
#include "Webserv.hpp"


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
实现逻辑：没有 CGI 配置时返回 false；逐个比较完整文件后缀；命中后原样保存配置的解释器路径并返回 true。路径存在性和执行权限由 validateCgiScript() 统一检查。
*/

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
            // 直接保留配置中的解释器路径；存在性和执行权限由 validateCgiScript() 统一检查。
            interpreter = it->second;
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
用途：
    根据已经解析完成的 Request 和共享 SessionStore 构造当前请求的 Response。
    普通请求会在这里完成重定向、方法检查、Session 路由、路径解析以及 GET/POST/DELETE 分发；
    CGI 请求不会在这里真正 fork/exec，而是生成后续异步 CGI 层需要的内部 header。

参数来源：
    - request：由 RequestParser 完整解析后交给 ServerManager，再传入本函数；其中包含 method、path、headers、body 和当前绑定的 ServerConfig。
    - sessionStore：由 ServerManager 长期持有的共享 SessionStore，用于不同请求之间保存和恢复 Session；Response 不拥有它。

主要变量：
    - server：当前 Request 绑定的 ServerConfig。
    - response：基于当前 Request 创建的默认 Response。
    - location：根据 request path 找到的最长匹配 LocationConfig；没有匹配时为 NULL。
    - scriptPath：从请求路径中提取出的 CGI 脚本路径；普通请求时通常就是原请求路径。
    - pathInfo：CGI 脚本路径之后额外的 PATH_INFO。
    - route：把 server 配置和匹配到的 location 配置合并后得到的 EffectiveRoute。
    - routeReady：表示 EffectiveRoute 是否成功建立。
    - action：把 HTTP method 转换成项目内部使用的 RequestAction。
    - cgiInterpreter：当前 CGI 扩展名配置对应的解释器路径；为空表示直接执行 CGI 文件。
    - isCgi：表示当前 scriptPath 是否匹配 location 中配置的 CGI extension。
    - effectiveAction：CGI 请求使用 ACTION_CGI，普通请求继续使用原来的 action。
    - pathStatus：createEffectivePath() 生成目标路径后的状态结果。

实现逻辑：
    1. 取得 Request 绑定的 ServerConfig；没有配置时直接返回 500。
    2. 查找最长匹配 location，并把请求路径拆分成 scriptPath 和 pathInfo。
    3. 合并 server 和 location 配置生成 EffectiveRoute；失败时返回 500。
    4. 如果 location 配置了 3xx 重定向，则优先直接返回重定向 Response。
    5. 检查 HTTP method 是否实现；未实现返回 501，不在当前 route 允许列表中则返回 405 和 Allow header。
    6. 如果请求命中 Session 示例路径，直接交给 SessionResponse 层处理。
    7. 判断请求是否属于 CGI，并生成对应的实际目标路径。
    8. CGI 请求分别检查解释器和脚本文件；验证通过后，把 CGI 路径、解释器、SCRIPT_NAME、PATH_INFO 和 document root 写入 X-Internal-CGI-* header，交给后续异步层执行。
    9. 非 CGI 请求先检查目标路径状态，再分别交给 GET/HEAD、POST 或 DELETE handler。
    10. HEAD 复用 GET 的处理结果，但清除实际 response body。

返回值：
    - 普通请求：返回已经构造完成的 HTTP Response。
    - CGI 请求：返回包含 X-Internal-CGI-* 内部信息的 Response，真正的 CGI 执行由后续 ServerManager/CgiHandler 完成。

接口约束：
    本函数要求调用方传入共享 SessionStore；当前不提供绕过 SessionStore 的旧接口。
*/
Response buildResponse(const Request &request, SessionStore &sessionStore)
{
    // 第一步：取得当前请求绑定的 server 配置，并先创建默认 Response。
    const ServerConfig *server = request.getConfig();
    Response response(request);

    // 没有 ServerConfig 时无法继续建立路由，直接返回 500。
    if (server == NULL)
    {
        Response::ErrorPageMap noErrorPages;
        response.createResponse(500, "", noErrorPages);
        return response;
    }

    // 第二步：根据请求路径找到最长匹配的 location；没有匹配时返回 NULL。
    const LocationConfig *location = findMatchingLocation(request.getPath(), server->locations);

    // 提前拆分 CGI 脚本路径和 PATH_INFO，后续普通请求和 CGI 请求共用 scriptPath。
    std::string scriptPath;
    std::string pathInfo;
    extractPathInfo(request.getPath(), location, scriptPath, pathInfo);

    // 第三步：把 server 配置和匹配 location 的覆盖配置合并成当前请求真正使用的 route。
    EffectiveRoute route;
    bool routeReady = (location != NULL) ? route.createEffectiveRoute(server, location) : route.createEffectiveRoute(server);

    // EffectiveRoute 创建失败时无法继续处理请求，返回 500。
    if (!routeReady)
    {
        response.createResponse(500, "", server->error_pages);
        return response;
    }

    // 第四步：location 配置的重定向优先处理，命中后不再进入文件或 CGI 流程。
    if (location != NULL && route.redirect_status >= 300 && route.redirect_status <= 399 && !route.redirect_url.empty())
    {
        response.setStatus(route.redirect_status);
        response.setHeader("Location", route.redirect_url);

        // 304 不生成普通重定向 body，其他 3xx 返回简单 HTML 内容。304 Not Modified：不是让你跳去新网址，而是告诉你“内容没变，直接用你电脑里的本地缓存！”
        if (route.redirect_status != 304)
        {
            response.setHeader("Content-Type", "text/html");
            response.setBody("<!DOCTYPE html><html><body>Redirecting</body></html>");
        }

        return response;
    }

    // 第五步：把 HTTP method 转成项目内部 action，用于统一检查方法支持和权限。
    RequestAction action = requestActionFromMethod(request.getMethod());

    // 项目未实现的方法返回 501 Not Implemented。
    if (action == ACTION_UNSUPPORTED)
    {
        response.createResponse(501, "", route.server->error_pages);
        return response;
    }

    // 当前 route 没有允许该方法时返回 405，并生成 Allow header。
    if (!isMethodAllowed(action, route.allow_methods))
    {
        response.createResponse(405, "", route.server->error_pages);
        response.setHeader("Allow", buildAllowHeader(route.allow_methods));

        // HEAD 错误响应同样不发送实际 body。
        if (request.getMethod() == "HEAD")
            response.clearBody();

        return response;
    }

    // 第六步：Session 示例路径直接交给 SessionResponse 层，不进入普通文件和 CGI 处理。
    if (isSessionDemoPath(request.getPath()))
        return buildSessionDemoResponse(request, sessionStore);

    // 第七步：根据 location 中配置的 CGI extension 判断当前脚本是否需要作为 CGI 执行。
    std::string cgiInterpreter;
    bool isCgi = findConfiguredCgiInterpreter(location, scriptPath, cgiInterpreter);

    // CGI 请求使用 ACTION_CGI 生成路径；普通请求继续使用原始 action。
    RequestAction effectiveAction = isCgi ? ACTION_CGI : action;
    int pathStatus = route.createEffectivePath(scriptPath, effectiveAction);

    // 第八步：CGI 请求在这里做路径和执行条件检查，但真正执行交给后续异步层。
    if (isCgi)
    {
        std::cout << "DEBUG cgi_require_target=" << route.cgi_require_target << std::endl;

        // 配置了解释器时，先检查解释器本身是否存在并且可执行。
        if (!cgiInterpreter.empty())
        {
            int interpreterStatus = validateCgiScript(cgiInterpreter, true);

            if (interpreterStatus != PATH_OK)
            {
                response.createResponse(interpreterStatus, "", route.server->error_pages);
                return response;
            }

            // 默认要求 CGI target 存在；只有 POST 且配置允许目标不存在时才跳过检查。
            bool checkTarget = true;
            if (request.getMethod() == "POST" && route.cgi_require_target == false)
                checkTarget = false;

            // 使用解释器执行时，脚本需要存在，但脚本文件本身不要求 executable 权限。
            if (checkTarget)
            {
                int cgiPathStatus = validateCgiScript(route.targetPath, false);

                if (cgiPathStatus != PATH_OK)
                {
                    response.createResponse(cgiPathStatus, "", route.server->error_pages);
                    return response;
                }
            }
        }
        else
        {
            // 没有解释器时 CGI 文件会被直接执行，因此目标文件必须存在并且可执行。
            int cgiPathStatus = validateCgiScript(route.targetPath, true);

            if (cgiPathStatus != PATH_OK)
            {
                response.createResponse(cgiPathStatus, "", route.server->error_pages);
                return response;
            }
        }

        // CGI 不在本函数直接执行；先设置内部状态和真实 CGI target，供 ServerManager/CgiHandler 使用。
        response.setStatus(200);
        response.setHeader("X-Internal-CGI-Path", route.targetPath);

        // 配置了解释器时，准备 execve() 最终使用的解释器路径。
        if (!cgiInterpreter.empty())
        {
            std::string executableInterpreter = cgiInterpreter;

            // CgiHandler 后续会先 chdir() 到脚本目录，所以相对解释器路径需要补出返回 webserv 启动目录的 ../ 层级。
            if (cgiInterpreter[0] != '/' && !route.targetPath.empty() && route.targetPath[0] != '/')
            {
                size_t slashPos = route.targetPath.find_last_of('/');
                std::string scriptDirectory = slashPos == std::string::npos ? "." : route.targetPath.substr(0, slashPos);

                // 统计脚本目录相对于启动目录实际向下进入了多少层。
                size_t depth = 0;
                size_t pos = 0;
                while (pos < scriptDirectory.size())
                {
                    // 跳过连续斜杠，找到当前目录 segment 的起点。
                    while (pos < scriptDirectory.size() && scriptDirectory[pos] == '/')
                        ++pos;

                    // 取得当前目录 segment。
                    size_t end = scriptDirectory.find('/', pos);
                    if (end == std::string::npos)
                        end = scriptDirectory.size();
                    std::string part = scriptDirectory.substr(pos, end - pos);

                    // 普通目录增加深度，".." 抵消一层，"." 和空 segment 不改变深度。
                    if (!part.empty() && part != ".")
                    {
                        if (part == "..")
                        {
                            if (depth > 0)
                                --depth;
                        }
                        else
                            ++depth;
                    }

                    pos = end;
                }

                // 根据目录深度生成 ../ 前缀，使 chdir() 后仍然能够找到原来配置的相对 interpreter。
                executableInterpreter.clear();
                size_t i = 0;
                while (i < depth)
                {
                    executableInterpreter += "../";
                    ++i;
                }

                // 去掉开头多余的 "./"，再拼接真实解释器路径。
                if (cgiInterpreter.compare(0, 2, "./") == 0)
                    executableInterpreter += cgiInterpreter.substr(2);
                else
                    executableInterpreter += cgiInterpreter;
            }

            response.setHeader("X-Internal-CGI-Interpreter", executableInterpreter);
        }

        // 把 CGI 环境变量构造需要的信息交给后续异步 CGI 层。
        response.setHeader("X-Internal-CGI-Script-Name", scriptPath);
        response.setHeader("X-Internal-CGI-Path-Info", pathInfo);
        response.setHeader("X-Internal-CGI-Document-Root", route.use_alias ? route.alias : route.root);

        return response;
    }

    // 第九步：非 CGI 请求必须先确认生成出的实际文件路径状态正常。
    if (pathStatus != PATH_OK)
    {
        response.createResponse(pathStatus, "", route.server->error_pages);
        return response;
    }

    // GET 和 HEAD 共用读取逻辑；HEAD 保留 GET 的状态码和 headers，但清除实际 body。
    if (action == ACTION_GET || action == ACTION_HEAD)
    {
        Response res = handleGet(request, route);

        if (action == ACTION_HEAD)
            res.clearBodyOnly();

        return res;
    }

    // POST 交给上传和写文件逻辑处理。
    if (action == ACTION_POST)
        return handlePost(request, route);

    // 前面的分支已经处理其他 action，到这里剩余的已支持方法是 DELETE。
    return handleDelete(request, route);
}
