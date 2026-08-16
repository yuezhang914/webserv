#ifndef LOCATIONCONFIG_HPP
#define LOCATIONCONFIG_HPP
#include <set>
#include <string>
#include <vector>
#include <map>
class LocationConfig
{
public:
    std::set<std::string> allow_methods;
    std::string root;
    bool autoindex;
    bool has_autoindex;
    std::vector<std::string> index;
    std::map<std::string, std::string> cgi_extensions;
    std::string upload_path;
    std::string path;
    int redirect_status;
    std::string redirect_url;
    std::string alias;
    bool has_root;
    bool has_alias;
    unsigned long max_body_size;
    bool has_body_size;
    bool cgi_require_target;
    bool has_cgi_require_target;
    // Creates a new LocationConfig object.
    LocationConfig();
    // Creates a new LocationConfig object.
    LocationConfig(const LocationConfig &src);
    // Copies data from another object.
    LocationConfig &operator=(const LocationConfig &rhs);
    // Cleans up this object and its owned resources.
    virtual ~LocationConfig();
};
#endif
