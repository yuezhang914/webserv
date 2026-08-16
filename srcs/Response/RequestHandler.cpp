#include "RequestHandler.hpp"
#include "RequestHandlerInternal.hpp"
#include <fstream>
#include <sstream>

// Changes ASCII letters in the text to lower case.
std::string requestHandlerToLowerAscii(const std::string &value)
{
    std::string result = value;
    size_t i = 0;
    while (i < result.size())
    {
        if (result[i] >= 'A' && result[i] <= 'Z')
            result[i] = static_cast<char>(result[i] - 'A' + 'a');
        ++i;
    }
    return result;
}

// Returns mime type.
std::string getMimeType(const std::string &path)
{
    size_t dot = path.find_last_of('.');
    if (dot == std::string::npos)
        return "application/octet-stream";
    std::string extension = requestHandlerToLowerAscii(path.substr(dot));
    if (extension == ".html" || extension == ".htm")
        return "text/html";
    if (extension == ".css")
        return "text/css";
    if (extension == ".js")
        return "application/javascript";
    if (extension == ".json")
        return "application/json";
    if (extension == ".txt")
        return "text/plain";
    if (extension == ".png")
        return "image/png";
    if (extension == ".jpg" || extension == ".jpeg")
        return "image/jpeg";
    if (extension == ".gif")
        return "image/gif";
    if (extension == ".svg")
        return "image/svg+xml";
    if (extension == ".pdf")
        return "application/pdf";
    return "text/plain";
}

// Handles get.
Response handleGet(const Request &request, EffectiveRoute &route)
{
    Response response(request);
    if (route.isDir)
        return handleIndex(route, request.getPath(), response.shouldCloseConnection());
    std::ifstream input(route.targetPath.c_str(), std::ios::binary);
    if (!input)
    {
        response.createResponse(500, "Failed to open file", route.server->error_pages);
        return response;
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    std::string content = buffer.str();
    input.close();
    if (!input)
    {
        response.createResponse(500, "Error closing file", route.server->error_pages);
        return response;
    }
    response.setStatus(200);
    response.setHeader("Content-Type", getMimeType(route.targetPath));
    response.setBody(content);
    return response;
}

// Changes an HTTP method into an internal request action.
RequestAction requestActionFromMethod(const std::string &method)
{
    if (method == "GET")
        return ACTION_GET;
    if (method == "HEAD")
        return ACTION_HEAD;
    if (method == "POST")
        return ACTION_POST;
    if (method == "DELETE")
        return ACTION_DELETE;
    return ACTION_UNSUPPORTED;
}

// Checks whether the current route allows this HTTP method.
bool isMethodAllowed(RequestAction action, const std::set<std::string> &allowMethods)
{
    if (action == ACTION_GET)
        return allowMethods.find("GET") != allowMethods.end();
    if (action == ACTION_POST)
        return allowMethods.find("POST") != allowMethods.end();
    if (action == ACTION_DELETE)
        return allowMethods.find("DELETE") != allowMethods.end();
    if (action == ACTION_HEAD)
        return allowMethods.find("HEAD") != allowMethods.end();
    return false;
}
