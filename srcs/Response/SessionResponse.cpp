/*
文件：srcs/Response/SessionResponse.cpp
用途：把 SessionStore 和 SessionDemo 接入 Response 层，并实现三个不依赖磁盘文件的虚拟演示路由。
演示路由：GET /session/counter、POST /session/login、POST /session/logout。
模块边界：登录正文格式由 SessionForm.cpp 解析；本文件不创建全局 Store、不读取 socket，也不修改 ServerManager。
*/

/*
包含：SessionResponse.hpp
用途：取得本文件对外的路径识别和响应构造函数声明。
*/
#include "SessionResponse.hpp"

/*
包含：SessionResponseInternal.hpp
用途：调用内部登录正文解析函数，区分 400 与 415。
*/
#include "SessionResponseInternal.hpp"

/*
包含：Request.hpp
用途：读取 normalized path、method、Cookie header、body、config 和连接策略来源。
*/
#include "Request.hpp"

/*
包含：SessionDemo.hpp
用途：调用访问计数、登录和退出三个 Bonus 示例，并读取 SessionDemoResult。
*/
#include "SessionDemo.hpp"

/*
包含：SessionStore.hpp
用途：接收并操作 ServerManager 长期持有的共享 Session 存储。
*/
#include "SessionStore.hpp"

/*
包含：ServerConfig.hpp
用途：内部失败或方法错误时读取当前 server 的自定义 error_pages。
*/
#include "ServerConfig.hpp"

/*
包含：<string>
用途：处理固定路径、Cookie、用户名和错误响应正文。
*/
#include <string>

static const char *SESSION_COUNTER_PATH = "/session/counter";
static const char *SESSION_LOGIN_PATH = "/session/login";
static const char *SESSION_LOGOUT_PATH = "/session/logout";


/*
函数：applySessionCachePolicy
用途：为包含会话状态或 Session 错误信息的响应禁止浏览器和中间缓存保存副本。
参数：response 是当前正在构造的 Session Response，由调用方以引用传入。
变量：无局部变量。
实现逻辑：写入 HTTP/1.1 的 Cache-Control: no-store，并补充兼容旧缓存的 Pragma: no-cache。
*/
static void applySessionCachePolicy(Response &response)
{
    response.setHeader("Cache-Control", "no-store");
    response.setHeader("Pragma", "no-cache");
}

/*
函数：createSessionFailureResponse
用途：为 Session 接入层的 4xx/5xx 分支创建与当前 Request 连接策略一致的错误响应。
参数：request 提供连接策略和 ServerConfig；statusCode/bodyText 由当前失败分支传入。
变量：response 是结果；emptyPages 只在 request 没有 config 时使用。
实现逻辑：优先使用 server 自定义 error_pages；没有 config 时生成默认页面；最后统一添加 no-store 缓存策略。
*/
static Response createSessionFailureResponse(const Request &request,
                                             int statusCode,
                                             const std::string &bodyText)
{
    Response response(request);
    const ServerConfig *server = request.getConfig();
    if (server != NULL)
        response.createResponse(statusCode, bodyText, server->error_pages);
    else
    {
        Response::ErrorPageMap emptyPages;
        response.createResponse(statusCode, bodyText, emptyPages);
    }
    applySessionCachePolicy(response);
    return response;
}

/*
函数：createSessionMethodNotAllowed
用途：为虚拟 Session 路由返回带精确 Allow header 的 405。
参数：request 是当前请求；allowedMethod 是 counter/login/logout 需要的唯一方法。
变量：response 由 createSessionFailureResponse() 创建。
实现逻辑：生成 405 后写入普通 Allow header，并返回结果。
*/
static Response createSessionMethodNotAllowed(
    const Request &request,
    const std::string &allowedMethod)
{
    Response response = createSessionFailureResponse(request, 405, "");
    response.setHeader("Allow", allowedMethod);
    return response;
}

/*
函数：applySessionDemoResult
用途：把 SessionDemoResult 转成 Response，避免三个路由重复设置状态、Cookie、类型和 body。
参数：request 提供连接策略；result 由 SessionDemo 成功写入。
变量：response 是最终结果。
实现逻辑：设置状态和 no-store 缓存策略；有 Cookie 时写 Set-Cookie；有 body 时设置 UTF-8 HTML 类型，Response 自身维护 Content-Length。
*/
static Response applySessionDemoResult(const Request &request,
                                       const SessionDemoResult &result)
{
    Response response(request);
    response.setStatus(result.statusCode);
    applySessionCachePolicy(response);
    if (result.hasSetCookie)
        response.setHeader("Set-Cookie", result.setCookieValue);
    if (!result.body.empty())
    {
        response.setHeader("Content-Type", "text/html; charset=utf-8");
        response.setBody(result.body);
    }
    return response;
}

/*
函数：isSessionDemoPath
用途：判断 normalized request path 是否属于三个内建 Session 示例。
参数：path 来自 Request::getPath()。
变量：无局部变量。
实现逻辑：与三个固定路径精确比较，避免 /session/counter-extra 等普通资源被误截获。
*/
bool isSessionDemoPath(const std::string &path)
{
    return path == SESSION_COUNTER_PATH
        || path == SESSION_LOGIN_PATH
        || path == SESSION_LOGOUT_PATH;
}

/*
函数：buildSessionDemoResponse
用途：执行 Session 虚拟路由，并把 Cookie/Session 示例结果转换为普通 Response。
参数：request 由 RequestParser 完整解析；sessionStore 是服务器级共享对象。
变量：cookieHeader 来自请求；result 保存示例输出；userName/loginStatus 用于 login body 解析。
实现逻辑：
    1. counter 只接受 GET，调用 buildCounterExample()。
    2. login 只接受 POST，解析 text/plain 或 urlencoded 用户名，再调用 buildLoginExample()。
    3. logout 只接受 POST，调用 buildLogoutExample()。
    4. SessionDemo 返回 false 时生成 500；成功时统一复制到 Response。
*/
Response buildSessionDemoResponse(const Request &request,
                                  SessionStore &sessionStore)
{
    std::string cookieHeader;
    request.getHeader("cookie", cookieHeader);
    SessionDemoResult result;

    if (request.getPath() == SESSION_COUNTER_PATH)
    {
        if (request.getMethod() != "GET")
            return createSessionMethodNotAllowed(request, "GET");
        if (!SessionDemo::buildCounterExample(cookieHeader,
            sessionStore, 0, result))
            return createSessionFailureResponse(request, 500, "");
        return applySessionDemoResult(request, result);
    }

    if (request.getPath() == SESSION_LOGIN_PATH)
    {
        if (request.getMethod() != "POST")
            return createSessionMethodNotAllowed(request, "POST");
        std::string userName;
        int loginStatus = extractSessionLoginUserName(request, userName);
        if (loginStatus != 200)
            return createSessionFailureResponse(request, loginStatus, "");
        if (!SessionDemo::buildLoginExample(cookieHeader, userName,
            sessionStore, 0, result))
            return createSessionFailureResponse(request, 500, "");
        return applySessionDemoResult(request, result);
    }

    if (request.getMethod() != "POST")
        return createSessionMethodNotAllowed(request, "POST");
    if (!SessionDemo::buildLogoutExample(cookieHeader,
        sessionStore, result))
        return createSessionFailureResponse(request, 500, "");
    return applySessionDemoResult(request, result);
}
