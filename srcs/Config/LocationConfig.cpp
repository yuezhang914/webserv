#include "LocationConfig.hpp"
#include "Webserv.hpp"

// Parses one location setting and saves its value.
bool Config::parseLocationDirective(const std::string &directive, const std::vector<std::string> &values, LocationConfig *loc)
{
    if (directive == "allow_methods")
    {
        if (values.size() >= 1)
        {
            for (size_t i = 0; i < values.size(); ++i)
            {
                if (values[i].empty())
                {
                    std::cerr << "Error: Empty method token in allow_methods" << std::endl;
                    return ERROR;
                }
                std::string method = values[i];
                for (size_t j = 0; j < method.size(); ++j)
                    method[j] = std::toupper(method[j]);
                if (method != "GET" && method != "POST" && method != "DELETE")
                {
                    std::cerr << "Error: Unsupported HTTP method: " << values[i] << std::endl;
                    return ERROR;
                }
                loc->allow_methods.insert(method);
            }
        }
        else
        {
            std::cerr << "Error: allow_methods requires at least one method" << std::endl;
            return ERROR;
        }
    }
    else if (directive == "root")
    {
        if (values.size() != 1)
        {
            std::cerr << "Error: Invalid root directive" << std::endl;
            return ERROR;
        }
        if (!loc->root.empty())
        {
            std::cerr << "Error: Duplicate root directive in location " << loc->path << std::endl;
            return ERROR;
        }
        if (!loc->alias.empty())
        {
            std::cerr << "Error: Cannot use root and alias together in location " << loc->path << std::endl;
            return ERROR;
        }
        loc->root = values[0];
        loc->has_root = true;
    }
    else if (directive == "autoindex")
    {
        if (values.size() != 1)
        {
            std::cerr << "Invalid " << directive << " directive" << std::endl;
            return ERROR;
        }
        if (loc->has_autoindex)
        {
            std::cerr << "Error: Duplicate " << directive << " directive in location " << loc->path << std::endl;
            return ERROR;
        }
        if (values[0] == "on")
        {
            loc->autoindex = true;
            loc->has_autoindex = true;
        }
        else if (values[0] == "off")
        {
            loc->autoindex = false;
            loc->has_autoindex = true;
        }
        else
        {
            std::cerr << "Invalid " << directive << " directive value: " << values[0] << std::endl;
            return ERROR;
        }
    }
    else if (directive == "max_body_size")
    {
        if (loc->has_body_size == true)
        {
            std::cerr << "Error: \"max_body_size\" directive is duplicate in this location block" << std::endl;
            return ERROR;
        }
        if (values.size() != 1)
        {
            std::cerr << "Error: \"max_body_size\" directive requires exactly one value in location " << loc->path << std::endl;
            return ERROR;
        }
        unsigned long converted_size = this->parseSize(values[0]);
        if (converted_size == static_cast<unsigned long>(ERROR_PARSE_SIZE))
            return ERROR;
        loc->max_body_size = converted_size;
        loc->has_body_size = true;
    }
    else if (directive == "index")
    {
        if (values.size() < 1)
        {
            std::cerr << "Error: Invalid index directive" << std::endl;
            return ERROR;
        }
        if (!loc->index.empty())
        {
            std::cerr << "Error: Duplicate index directive in location " << loc->path << std::endl;
            return ERROR;
        }
        for (size_t i = 0; i < values.size(); ++i)
        {
            if (values[i].empty())
            {
                std::cerr << "Error: Empty index value" << std::endl;
                return ERROR;
            }
            loc->index.push_back(values[i]);
        }
    }
    else if (directive == "cgi_extension")
    {
        if (values.size() != 2)
        {
            std::cerr << "Error: Invalid cgi_extension directive" << std::endl;
            return ERROR;
        }
        if (values[0].empty() || values[0][0] != '.' || values[1].empty())
        {
            std::cerr << "Error: Invalid cgi_extension format" << std::endl;
            return ERROR;
        }
        if (loc->cgi_extensions.find(values[0]) != loc->cgi_extensions.end())
        {
            std::cerr << "Error: Duplicate cgi_extension for " << values[0] << " in location " << loc->path << std::endl;
            return ERROR;
        }
        loc->cgi_extensions[values[0]] = values[1];
    }
    else if (directive == "cgi_require_target")
    {
        if (values.size() != 1)
        {
            std::cerr << "Error: Invalid cgi_require_target directive" << std::endl;
            return ERROR;
        }
        if (loc->has_cgi_require_target)
        {
            std::cerr << "Error: Duplicate cgi_require_target directive in location " << loc->path << std::endl;
            return ERROR;
        }
        if (values[0] == "on")
        {
            loc->cgi_require_target = true;
            loc->has_cgi_require_target = true;
        }
        else if (values[0] == "off")
        {
            loc->cgi_require_target = false;
            loc->has_cgi_require_target = true;
        }
        else
        {
            std::cerr << "Error: Invalid cgi_require_target value: " << values[0] << std::endl;
            return ERROR;
        }
    }
    else if (directive == "upload_path")
    {
        if (values.size() != 1)
        {
            std::cerr << "Error: Invalid upload_path directive" << std::endl;
            return ERROR;
        }
        if (!loc->upload_path.empty())
        {
            std::cerr << "Error: Duplicate upload_path directive in location " << loc->path << std::endl;
            return ERROR;
        }
        loc->upload_path = values[0];
    }
    else if (directive == "return")
    {
        if (values.size() != 2)
        {
            std::cerr << "Error: Invalid return directive" << std::endl;
            return ERROR;
        }
        char *endptr;
        int status = strtol(values[0].c_str(), &endptr, 10);
        if (*endptr != '\0' || status < 300 || status > 399)
        {
            std::cerr << "Error: Invalid return status code: " << values[0] << std::endl;
            return ERROR;
        }
        loc->redirect_status = status;
        loc->redirect_url = values[1];
    }
    else if (directive == "alias")
    {
        if (values.size() != 1)
        {
            std::cerr << "Invalid alias directive" << std::endl;
            return ERROR;
        }
        if (!loc->root.empty())
        {
            std::cerr << "Error: Cannot use root and alias together in location " << loc->path << std::endl;
            return ERROR;
        }
        if (!loc->alias.empty())
        {
            std::cerr << "Error: Duplicate alias directive in location " << loc->path << std::endl;
            return ERROR;
        }
        loc->alias = values[0];
        loc->has_alias = true;
    }
    else
    {
        std::cerr << "Error: Unknown location directive: " << directive << std::endl;
        return ERROR;
    }
    return SUCCESS;
}

// Creates a new LocationConfig object.
LocationConfig::LocationConfig() : allow_methods(), root(""), autoindex(false), has_autoindex(false), index(), cgi_extensions(), upload_path(""), path(""), redirect_status(0), redirect_url(""), alias(""), has_root(false), has_alias(false), max_body_size(MAX_BODY_SIZE), has_body_size(false), cgi_require_target(true), has_cgi_require_target(false)
{
}

// Creates a new LocationConfig object.
LocationConfig::LocationConfig(const LocationConfig &src) : allow_methods(src.allow_methods), root(src.root), autoindex(src.autoindex), has_autoindex(src.has_autoindex), index(src.index), cgi_extensions(src.cgi_extensions), upload_path(src.upload_path), path(src.path), redirect_status(src.redirect_status), redirect_url(src.redirect_url), alias(src.alias), has_root(src.has_root), has_alias(src.has_alias), max_body_size(src.max_body_size), has_body_size(src.has_body_size), cgi_require_target(src.cgi_require_target), has_cgi_require_target(src.has_cgi_require_target)
{
}

// Copies data from another object.
LocationConfig &LocationConfig::operator=(const LocationConfig &rhs)
{
    if (this != &rhs)
    {
        allow_methods = rhs.allow_methods;
        root = rhs.root;
        autoindex = rhs.autoindex;
        has_autoindex = rhs.has_autoindex;
        index = rhs.index;
        cgi_extensions = rhs.cgi_extensions;
        upload_path = rhs.upload_path;
        path = rhs.path;
        redirect_status = rhs.redirect_status;
        redirect_url = rhs.redirect_url;
        alias = rhs.alias;
        has_root = rhs.has_root;
        has_alias = rhs.has_alias;
        max_body_size = rhs.max_body_size;
        has_body_size = rhs.has_body_size;
        cgi_require_target = rhs.cgi_require_target;
        has_cgi_require_target = rhs.has_cgi_require_target;
    }
    return *this;
}

// Cleans up this object and its owned resources.
LocationConfig::~LocationConfig()
{
}
