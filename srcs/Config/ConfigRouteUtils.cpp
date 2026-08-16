#include "ConfigRouteUtils.hpp"

// Checks whether a request path matches a location path.
static bool locationPathMatches(const std::string &path, const std::string &location_path)
{
    if (location_path.empty())
        return false;
    if (location_path == "/")
        return !path.empty() && path[0] == '/';
    if (path.compare(0, location_path.size(), location_path) != 0)
        return false;
    if (path.size() == location_path.size())
        return true;
    if (location_path[location_path.size() - 1] == '/')
        return true;
    return path[location_path.size()] == '/';
}

// Finds the longest location that matches the request path.
const LocationConfig *findMatchingLocation(const std::string &path, const std::vector<LocationConfig> &locations)
{
    const LocationConfig *best_match = NULL;
    size_t longest_match = 0;
    size_t i = 0;
    while (i < locations.size())
    {
        if (locationPathMatches(path, locations[i].path) && locations[i].path.size() > longest_match)
        {
            best_match = &locations[i];
            longest_match = locations[i].path.size();
        }
        ++i;
    }
    return best_match;
}

// Returns effective body limit.
unsigned long getEffectiveBodyLimit(const ServerConfig *server, const std::string &path)
{
    if (server == NULL)
        return MAX_BODY_SIZE;
    const LocationConfig *location = findMatchingLocation(path, server->locations);
    if (location != NULL && location->has_body_size)
        return location->max_body_size;
    return server->max_body_size;
}
