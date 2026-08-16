#include "Webserv.hpp"
#include "ConfigRouteUtils.hpp"
#include "EffectiveRoute.hpp"
#include "Request.hpp"
#include "RequestHandler.hpp"
#include "SessionResponse.hpp"
#include <map>
#include <set>
#include <string>
#include <cerrno>
#include <sys/stat.h>
#include <unistd.h>

// Builds the value of the HTTP Allow header.
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

// Finds the CGI interpreter for the script extension.
static bool findConfiguredCgiInterpreter(const LocationConfig *location, const std::string &path, std::string &interpreter)
{
    interpreter.clear();
    if (location == NULL || location->cgi_extensions.empty())
        return false;
    std::map<std::string, std::string>::const_iterator it = location->cgi_extensions.begin();
    while (it != location->cgi_extensions.end())
    {
        const std::string &extension = it->first;
        if (!extension.empty() && path.size() >= extension.size() && path.compare(path.size() - extension.size(), extension.size(), extension) == 0)
        {
            interpreter = it->second;
            return true;
        }
        ++it;
    }
    return false;
}

// Splits a CGI request path into script path and PATH_INFO.
static void extractPathInfo(const std::string &rawPath, const LocationConfig *location, std::string &scriptPath, std::string &pathInfo)
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

// Checks whether a CGI file can be used.
static int validateCgiScript(const std::string &scriptPath, bool requiresExecutePermission)
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

// Builds the response for one HTTP request.
Response buildResponse(const Request &request, SessionStore &sessionStore)
{
    const ServerConfig *server = request.getConfig();
    Response response(request);
    if (server == NULL)
    {
        Response::ErrorPageMap noErrorPages;
        response.createResponse(500, "", noErrorPages);
        return response;
    }
    const LocationConfig *location = findMatchingLocation(request.getPath(), server->locations);
    std::string scriptPath;
    std::string pathInfo;
    extractPathInfo(request.getPath(), location, scriptPath, pathInfo);
    EffectiveRoute route;
    bool routeReady = (location != NULL) ? route.createEffectiveRoute(server, location) : route.createEffectiveRoute(server);
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
            response.setBody("<!DOCTYPE html><html><body>" "Redirecting" "</body></html>");
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
    {
        return buildSessionDemoResponse(request, sessionStore);
    }
    std::string cgiInterpreter;
    bool isCgi = findConfiguredCgiInterpreter(location, scriptPath, cgiInterpreter);
    RequestAction effectiveAction = isCgi ? ACTION_CGI : action;
    int pathStatus = route.createEffectivePath(scriptPath, effectiveAction);
    if (isCgi)
    {
        DEBUG_LOG("DEBUG cgi_require_target=" << route.cgi_require_target);
        if (!cgiInterpreter.empty())
        {
            int interpreterStatus = validateCgiScript(cgiInterpreter, true);
            if (interpreterStatus != PATH_OK)
            {
                response.createResponse(interpreterStatus, "", route.server->error_pages);
                return response;
            }
            bool checkTarget = true;
            if (request.getMethod() == "POST" && route.cgi_require_target == false)
            {
                checkTarget = false;
            }
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
            int cgiPathStatus = validateCgiScript(route.targetPath, true);
            if (cgiPathStatus != PATH_OK)
            {
                response.createResponse(cgiPathStatus, "", route.server->error_pages);
                return response;
            }
        }
        response.setStatus(200);
        response.setHeader("X-Internal-CGI-Path", route.targetPath);
        if (!cgiInterpreter.empty())
        {
            std::string executableInterpreter = cgiInterpreter;
            if (cgiInterpreter[0] != '/' && !route.targetPath.empty() && route.targetPath[0] != '/')
            {
                size_t slashPos = route.targetPath.find_last_of('/');
                std::string scriptDirectory = slashPos == std::string::npos ? "." : route.targetPath.substr(0, slashPos);
                size_t depth = 0;
                size_t pos = 0;
                while (pos < scriptDirectory.size())
                {
                    while (pos < scriptDirectory.size() && scriptDirectory[pos] == '/')
                        ++pos;
                    size_t end = scriptDirectory.find('/', pos);
                    if (end == std::string::npos)
                        end = scriptDirectory.size();
                    std::string part = scriptDirectory.substr(pos, end - pos);
                    if (!part.empty() && part != ".")
                    {
                        if (part == "..")
                        {
                            if (depth > 0)
                                --depth;
                        }
                        else ++depth;
                    }
                    pos = end;
                }
                executableInterpreter.clear();
                size_t i = 0;
                while (i < depth)
                {
                    executableInterpreter += "../";
                    ++i;
                }
                if (cgiInterpreter.compare(0, 2, "./") == 0)
                    executableInterpreter += cgiInterpreter.substr(2);
                else executableInterpreter += cgiInterpreter;
            }
            response.setHeader("X-Internal-CGI-Interpreter", executableInterpreter);
        }
        response.setHeader("X-Internal-CGI-Script-Name", scriptPath);
        response.setHeader("X-Internal-CGI-Path-Info", pathInfo);
        response.setHeader("X-Internal-CGI-Document-Root", route.use_alias ? route.alias : route.root);
        return response;
    }
    if (pathStatus != PATH_OK)
    {
        response.createResponse(pathStatus, "", route.server->error_pages);
        return response;
    }
    if (action == ACTION_GET || action == ACTION_HEAD)
    {
        Response res = handleGet(request, route);
        if (action == ACTION_HEAD)
        {
            res.clearBodyOnly();
        }
        return res;
    }
    if (action == ACTION_POST)
    {
        return handlePost(request, route);
    }
    return handleDelete(request, route);
}
