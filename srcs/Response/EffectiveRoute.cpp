#include "EffectiveRoute.hpp"

// Creates a new EffectiveRoute object.
EffectiveRoute::EffectiveRoute() : server(NULL), location(NULL), root(), alias(), use_alias(false), autoindex(false), allow_methods(), index(), upload_path(), location_prefix("/"), redirect_status(0), redirect_url(), targetPath(), isDir(false)
{
}

// Builds the final route settings for one request.
bool EffectiveRoute::createEffectiveRoute(const ServerConfig *srv, const LocationConfig *loc)
{
    if (srv == NULL || loc == NULL)
        return false;
    server = srv;
    location = loc;
    root.clear();
    alias.clear();
    allow_methods.clear();
    index.clear();
    targetPath.clear();
    isDir = false;
    if (loc->has_alias)
    {
        use_alias = true;
        alias = loc->alias;
        root = loc->alias;
    }
    else
    {
        use_alias = false;
        if (loc->has_root)
            root = loc->root;
        else if (srv->has_root)
            root = srv->root;
        else return false;
    }
    if (!loc->allow_methods.empty())
        allow_methods = loc->allow_methods;
    else if (!srv->allow_methods.empty())
        allow_methods = srv->allow_methods;
    else
    {
        allow_methods.insert("GET");
        allow_methods.insert("POST");
        allow_methods.insert("DELETE");
    }
    if (!loc->index.empty())
        index = loc->index;
    else if (!srv->index.empty())
        index = srv->index;
    else index.push_back("index.html");
    autoindex = loc->has_autoindex ? loc->autoindex : srv->autoindex;
    if (!loc->upload_path.empty())
        upload_path = loc->upload_path;
    else if (!srv->upload_path.empty())
        upload_path = srv->upload_path;
    else upload_path = "/upload/";
    location_prefix = loc->path;
    redirect_status = loc->redirect_status;
    redirect_url = loc->redirect_url;
    cgi_require_target = loc->cgi_require_target;
    return true;
}

// Builds the final route settings for one request.
bool EffectiveRoute::createEffectiveRoute(const ServerConfig *srv)
{
    if (srv == NULL)
        return false;
    server = srv;
    location = NULL;
    root.clear();
    alias.clear();
    allow_methods.clear();
    index.clear();
    targetPath.clear();
    isDir = false;
    use_alias = false;
    if (!srv->has_root)
        return false;
    root = srv->root;
    if (!srv->allow_methods.empty())
        allow_methods = srv->allow_methods;
    else
    {
        allow_methods.insert("GET");
        allow_methods.insert("POST");
        allow_methods.insert("DELETE");
    }
    if (!srv->index.empty())
        index = srv->index;
    else index.push_back("index.html");
    upload_path = srv->upload_path.empty() ? "/upload/" : srv->upload_path;
    autoindex = srv->autoindex;
    location_prefix = "/";
    redirect_status = 0;
    redirect_url.clear();
    return true;
}
