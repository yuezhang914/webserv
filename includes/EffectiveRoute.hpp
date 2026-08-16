#ifndef EFFECTIVE_ROUTE_HPP
#define EFFECTIVE_ROUTE_HPP
#include "LocationConfig.hpp"
#include "ServerConfig.hpp"
enum RequestAction
{
    ACTION_UNSUPPORTED = 0, ACTION_GET, ACTION_POST, ACTION_DELETE, ACTION_HEAD, ACTION_CGI
};
enum EffectivePathStatus
{
    PATH_OK = 0
};
struct EffectiveRoute
{
    const ServerConfig *server;
    const LocationConfig *location;
    std::string root;
    std::string alias;
    bool use_alias;
    bool autoindex;
    std::set<std::string> allow_methods;
    std::vector<std::string> index;
    std::string upload_path;
    std::string location_prefix;
    int redirect_status;
    std::string redirect_url;
    std::string targetPath;
    bool isDir;
    bool cgi_require_target;
    // Creates a new EffectiveRoute object.
    EffectiveRoute();
    // Builds the final route settings for one request.
    bool createEffectiveRoute(const ServerConfig *srv, const LocationConfig *loc);
    // Builds the final route settings for one request.
    bool createEffectiveRoute(const ServerConfig *srv);
    // Builds the real file path for one request.
    int createEffectivePath(const std::string &requestPath, RequestAction action);
    // Checks whether valid path.
    int isValidPath(RequestAction action);
};

// Joins paths.
std::string joinPaths(const std::string &base, const std::string &suffix);
#endif
