#include "EffectiveRoute.hpp"
#include <cerrno>
#include <sys/stat.h>

// Builds the real file path for one request.
int EffectiveRoute::createEffectivePath(const std::string &requestPath, RequestAction action)
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
        if (location != NULL && location->has_root)
        {
            if (requestPath.compare(0, location_prefix.size(), location_prefix) != 0)
                return 404;
            suffix = requestPath.substr(location_prefix.size());
        }
        else suffix = requestPath;
    }
    targetPath = joinPaths(base, suffix);
    if (targetPath.empty())
        return 500;
    return isValidPath(action);
}

// Joins paths.
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

// Checks whether valid path.
int EffectiveRoute::isValidPath(RequestAction action)
{
    isDir = false;
    if (action != ACTION_GET && action != ACTION_HEAD)
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
