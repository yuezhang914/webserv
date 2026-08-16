#include "Webserv.hpp"

// Checks whether wildcard host.
static bool isWildcardHost(const std::string &host)
{
    return host == "0.0.0.0";
}

// Creates a new Config object.
Config::Config(const std::string &path) : all_server_names(), error(0)
{
    if (parseFile(path) == ERROR)
    {
        std::cerr << "Error: Failed to parse config file" << std::endl;
        this->error = 1;
        return;
    }
    if (serversHaveUniqueListenPairs() == ERROR)
        this->error = 1;
}

// Cleans up this object and its owned resources.
Config::~Config()
{
}

// Returns servers.
std::vector<ServerConfig> &Config::getServers()
{
    return servers;
}

// Returns servers.
const std::vector<ServerConfig> &Config::getServers() const
{
    return servers;
}

// Checks that server listen address and port pairs do not conflict.
bool Config::serversHaveUniqueListenPairs() const
{
    size_t i = 0;
    while (i < servers.size())
    {
        size_t j = i + 1;
        while (j < servers.size())
        {
            const ServerConfig &left = servers[i];
            const ServerConfig &right = servers[j];
            if (left.port == right.port && (left.host == right.host || isWildcardHost(left.host) || isWildcardHost(right.host)))
            {
                std::cerr << "Error: Conflicting listen endpoints: " << left.host << ":" << left.port << " and " << right.host << ":" << right.port << std::endl;
                return ERROR;
            }
            ++j;
        }
        ++i;
    }
    return SUCCESS;
}

// Removes spaces from both ends of the text.
std::string Config::trim(const std::string &str) const
{
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, last - first + 1);
}

// Splits text into parts with the given delimiter.
std::vector<std::string> Config::split(const std::string &str, char delimiter) const
{
    std::vector<std::string> tokens;
    std::stringstream ss(str);
    std::string token;
    while (std::getline(ss, token, delimiter))
    {
        token = trim(token);
        if (!token.empty())
        {
            tokens.push_back(token);
        }
    }
    return (tokens);
}
