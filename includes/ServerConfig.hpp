#ifndef SERVERCONFIG_HPP
#define SERVERCONFIG_HPP
#include "LocationConfig.hpp"
#include "Defines.hpp"
class ServerConfig
{
public:
    int port;
    int countport;
    std::string host;
    std::vector<std::string> server_names;
    std::string root;
    std::map<int, std::string> error_pages;
    unsigned long max_body_size;
    bool has_body_size;
    std::vector<LocationConfig> locations;
    std::vector<std::string> index;
    std::string upload_path;
    std::set<std::string> allow_methods;
    int socketFd;
    bool has_root;
    bool has_autoindex;
    bool autoindex;
    // Creates a new ServerConfig object.
    ServerConfig();
    // Creates a new ServerConfig object.
    ServerConfig(const ServerConfig &src);
    // Cleans up this object and its owned resources.
    virtual ~ServerConfig();
    // Copies data from another object.
    ServerConfig &operator=(const ServerConfig &rhs);
};
#endif
