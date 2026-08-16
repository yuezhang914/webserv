#include "SessionResponse.hpp"
#include "SessionResponseInternal.hpp"
#include "Request.hpp"
#include "SessionDemo.hpp"
#include "SessionStore.hpp"
#include "ServerConfig.hpp"
#include <string>
static const char *SESSION_COUNTER_PATH = "/session/counter";
static const char *SESSION_LOGIN_PATH = "/session/login";
static const char *SESSION_LOGOUT_PATH = "/session/logout";

// Adds headers that stop session pages from being cached.
static void applySessionCachePolicy(Response &response)
{
    response.setHeader("Cache-Control", "no-store");
    response.setHeader("Pragma", "no-cache");
}

// Creates session failure response.
static Response createSessionFailureResponse(const Request &request, int statusCode, const std::string &bodyText)
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

// Creates session method not allowed.
static Response createSessionMethodNotAllowed(const Request &request, const std::string &allowedMethod)
{
    Response response = createSessionFailureResponse(request, 405, "");
    std::string allowValue = allowedMethod;
    if (allowedMethod == "GET")
        allowValue = "GET, HEAD";
    response.setHeader("Allow", allowValue);
    return response;
}

// Changes a session demo result into an HTTP response.
static Response applySessionDemoResult(const Request &request, const SessionDemoResult &result)
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

// Checks whether the request uses a session demo path.
bool isSessionDemoPath(const std::string &path)
{
    return path == SESSION_COUNTER_PATH || path == SESSION_LOGIN_PATH || path == SESSION_LOGOUT_PATH;
}

// Builds session demo response.
Response buildSessionDemoResponse(const Request &request, SessionStore &sessionStore)
{
    std::string cookieHeader;
    request.getHeader("cookie", cookieHeader);
    SessionDemoResult result;
    if (request.getPath() == SESSION_COUNTER_PATH)
    {
        if (request.getMethod() != "GET" && request.getMethod() != "HEAD")
            return createSessionMethodNotAllowed(request, "GET");
        if (!SessionDemo::buildCounterExample(cookieHeader, sessionStore, 0, result))
            return createSessionFailureResponse(request, 500, "");
        Response response = applySessionDemoResult(request, result);
        if (request.getMethod() == "HEAD")
            response.clearBodyOnly();
        return response;
    }
    if (request.getPath() == SESSION_LOGIN_PATH)
    {
        if (request.getMethod() != "POST")
            return createSessionMethodNotAllowed(request, "POST");
        std::string userName;
        int loginStatus = extractSessionLoginUserName(request, userName);
        if (loginStatus != 200)
            return createSessionFailureResponse(request, loginStatus, "");
        if (!SessionDemo::buildLoginExample(cookieHeader, userName, sessionStore, 0, result))
            return createSessionFailureResponse(request, 500, "");
        return applySessionDemoResult(request, result);
    }
    if (request.getMethod() != "POST")
        return createSessionMethodNotAllowed(request, "POST");
    if (!SessionDemo::buildLogoutExample(cookieHeader, sessionStore, result))
        return createSessionFailureResponse(request, 500, "");
    return applySessionDemoResult(request, result);
}
