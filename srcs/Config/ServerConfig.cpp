#include "ServerConfig.hpp"
#include "Webserv.hpp"

// Checks whether the text is a valid IPv4 address.
static int is_valid_ipv4(const char *ip)
{
    if (ip == NULL)
        return ERROR;
    std::string text(ip);
    if (text.empty() || text[0] == '.' || text[text.length() - 1] == '.')
        return ERROR;
    std::stringstream stream(text);
    std::string part;
    int count = 0;
    while (std::getline(stream, part, '.'))
    {
        if (part.empty() || part.size() > 3)
            return ERROR;
        size_t index = 0;
        while (index < part.size())
        {
            if (!std::isdigit(part[index]))
                return ERROR;
            index++;
        }
        int value = std::atoi(part.c_str());
        if (value < 0 || value > 255)
            return ERROR;
        if (part.size() > 1 && part[0] == '0')
            return ERROR;
        count++;
    }
    if (count != 4)
        return ERROR;
    return SUCCESS;
}

// Parses one server setting and saves its value.
bool Config::parseServerDirective(const std::string &directive, const std::vector<std::string> &values, ServerConfig *srv)
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
                srv->allow_methods.insert(method);
            }
        }
        else
        {
            std::cerr << "Error: allow_methods requires at least one method" << std::endl;
            return ERROR;
        }
    }
    else if (directive == "upload_path")
    {
        if (values.size() != 1)
        {
            std::cerr << "Error: Invalid upload_path directive - exactly one value required" << std::endl;
            return ERROR;
        }
        if (!srv->upload_path.empty())
        {
            std::cerr << "Error: Duplicate upload_path directive in server" << std::endl;
            return ERROR;
        }
        srv->upload_path = values[0];
    }
    else if (directive == "autoindex")
    {
        if (values.size() != 1)
        {
            std::cerr << "Invalid " << directive << " directive" << std::endl;
            return ERROR;
        }
        if (srv->has_autoindex)
        {
            std::cerr << "Error: Duplicate " << directive << " directive in server" << std::endl;
            return ERROR;
        }
        if (values[0] == "on")
            srv->autoindex = true;
        else if (values[0] == "off")
            srv->autoindex = false;
        else
        {
            std::cerr << "Invalid " << directive << " directive value: " << values[0] << std::endl;
            return ERROR;
        }
        srv->has_autoindex = true;
    }
    else if (directive == "listen")
    {
        if (values.size() != 1)
        {
            std::cerr << "Invalid listen directive" << std::endl;
            return ERROR;
        }
        if (srv->countport >= 1)
        {
            std::cerr << "Error: Duplicate listen directive in server" << std::endl;
            return ERROR;
        }
        std::string value = values[0];
        std::string ip;
        std::string port_str;
        size_t colon_pos = value.find(':');
        if (colon_pos == std::string::npos)
        {
            std::vector<std::string> parts = split(value, '.');
            if (parts.size() == 4)
            {
                ip = value;
                port_str = DEFAULT_PORT;
            }
            else
            {
                port_str = value;
                ip = "";
            }
        }
        else
        {
            ip = value.substr(0, colon_pos);
            port_str = value.substr(colon_pos + 1);
        }
        if (!ip.empty())
        {
            if (is_valid_ipv4(ip.c_str()) == ERROR)
            {
                std::cerr << "Invalid IP in listen directive: " << ip << std::endl;
                return ERROR;
            }
            srv->host = ip;
        }
        else
        {
            srv->host = "0.0.0.0";
        }
        if (port_str.empty())
        {
            std::cerr << "Invalid empty port in listen directive" << std::endl;
            return ERROR;
        }
        size_t port_index = 0;
        while (port_index < port_str.size())
        {
            if (!std::isdigit(static_cast<unsigned char>(port_str[port_index])))
            {
                std::cerr << "Invalid port in listen directive: " << port_str << std::endl;
                return ERROR;
            }
            port_index++;
        }
        char *endptr;
        long parsed_port = strtol(port_str.c_str(), &endptr, 10);
        if (*endptr != '\0' || parsed_port <= 0 || parsed_port > 65535)
        {
            std::cerr << "Invalid port in listen directive: " << port_str << std::endl;
            return ERROR;
        }
        srv->port = static_cast<int>(parsed_port);
        srv->countport++;
    }
    else if (directive == "server_name")
    {
        if (values.empty())
        {
            std::cerr << "Error: server_name requires at least one name" << std::endl;
            return ERROR;
        }
        if (!srv->server_names.empty())
        {
            std::cerr << "Error: Duplicate server_name directive in server" << std::endl;
            return ERROR;
        }
        for (size_t i = 0; i < values.size(); ++i)
        {
            if (values[i].empty())
            {
                std::cerr << "Error: Empty server_name value" << std::endl;
                return ERROR;
            }
            srv->server_names.push_back(values[i]);
        }
    }
    else if (directive == "root")
    {
        if (values.size() != 1)
        {
            std::cerr << "Invalid root directive" << std::endl;
            return ERROR;
        }
        if (!srv->root.empty())
        {
            std::cerr << "Error: Duplicate root directive in server" << std::endl;
            return ERROR;
        }
        srv->root = values[0];
        srv->has_root = true;
    }
    else if (directive == "error_page")
    {
        if (values.size() < 2)
        {
            std::cerr << "Invalid error_page directive" << std::endl;
            return ERROR;
        }
        std::string error_path = values[values.size() - 1];
        if (error_path.empty())
        {
            std::cerr << "Error: Empty error page path" << std::endl;
            return ERROR;
        }
        for (size_t i = 0; i < values.size() - 1; ++i)
        {
            char *endptr;
            int code = strtol(values[i].c_str(), &endptr, 10);
            if (*endptr != '\0' || code < 300 || code > 599)
            {
                std::cerr << "Error: Invalid error_page code: " << values[i] << std::endl;
                return ERROR;
            }
            srv->error_pages[code] = error_path;
        }
    }
    else if (directive == "max_body_size")
    {
        if (srv->has_body_size == true)
        {
            std::cerr << "Error: \"max_body_size\" directive is duplicate in this server block" << std::endl;
            return ERROR;
        }
        if (values.size() != 1)
            return ERROR;
        unsigned long converted_size = this->parseSize(values[0]);
        if (converted_size == static_cast<unsigned long>(ERROR_PARSE_SIZE))
            return ERROR;
        srv->max_body_size = converted_size;
        srv->has_body_size = true;
    }
    else if (directive == "index")
    {
        if (values.size() < 1)
        {
            std::cerr << "Error: Invalid index directive" << std::endl;
            return ERROR;
        }
        if (!srv->index.empty())
        {
            std::cerr << "Error: Duplicate index directive in server" << std::endl;
            return ERROR;
        }
        for (size_t i = 0; i < values.size(); ++i)
        {
            if (values[i].empty())
            {
                std::cerr << "Error: Empty index value" << std::endl;
                return ERROR;
            }
            srv->index.push_back(values[i]);
        }
    }
    else
    {
        std::cerr << "Error: Unknown server directive: " << directive << std::endl;
        return ERROR;
    }
    return SUCCESS;
}

// Creates a new ServerConfig object.
ServerConfig::ServerConfig() : port(80), countport(0), host("127.0.0.1"), server_names(), root(""), error_pages(), max_body_size(MAX_BODY_SIZE), has_body_size(false), locations(), index(), upload_path(""), allow_methods(), socketFd(-1), has_root(false), has_autoindex(false), autoindex(false)
{
}

// Creates a new ServerConfig object.
ServerConfig::ServerConfig(const ServerConfig &src) : port(src.port), countport(src.countport), host(src.host), server_names(src.server_names), root(src.root), error_pages(src.error_pages), max_body_size(src.max_body_size), has_body_size(src.has_body_size), locations(src.locations), index(src.index), upload_path(src.upload_path), allow_methods(src.allow_methods), socketFd(-1), has_root(src.has_root), has_autoindex(src.has_autoindex), autoindex(src.autoindex)
{
}

// Cleans up this object and its owned resources.
ServerConfig::~ServerConfig()
{
    if (socketFd > 0)
        close(socketFd);
}

// Copies data from another object.
ServerConfig &ServerConfig::operator=(const ServerConfig &rhs)
{
    if (this != &rhs)
    {
        port = rhs.port;
        countport = rhs.countport;
        host = rhs.host;
        server_names = rhs.server_names;
        root = rhs.root;
        error_pages = rhs.error_pages;
        max_body_size = rhs.max_body_size;
        has_body_size = rhs.has_body_size;
        locations = rhs.locations;
        index = rhs.index;
        upload_path = rhs.upload_path;
        allow_methods = rhs.allow_methods;
        socketFd = -1;
        has_root = rhs.has_root;
        has_autoindex = rhs.has_autoindex;
        autoindex = rhs.autoindex;
    }
    return *this;
}
