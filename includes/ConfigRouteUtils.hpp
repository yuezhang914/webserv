#ifndef CONFIGROUTEUTILS_HPP
#define CONFIGROUTEUTILS_HPP
#include "ServerConfig.hpp"
#include "LocationConfig.hpp"

// Finds the longest location that matches the request path.
const LocationConfig *findMatchingLocation(const std::string &path, const std::vector<LocationConfig> &locations);

// Returns effective body limit.
unsigned long getEffectiveBodyLimit(const ServerConfig *server, const std::string &path);
#endif
