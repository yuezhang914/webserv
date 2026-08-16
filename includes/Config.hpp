#ifndef CONFIG_HPP
#define CONFIG_HPP
#include "ServerConfig.hpp"
#include "LocationConfig.hpp"
#include "Defines.hpp"
struct ConfigToken
{
    std::string value;
    size_t line;
    // Creates a new ConfigToken object.
    ConfigToken();
    // Creates a new ConfigToken object.
    ConfigToken(const std::string &token_value, size_t token_line);
};
class Config
{
private:
    std::map<int, std::set<std::string> > all_server_names;
    std::vector<ServerConfig> servers;
    // Removes spaces from both ends of the text.
    std::string trim(const std::string &str) const;
    // Splits text into parts with the given delimiter.
    std::vector<std::string> split(const std::string &str, char delimiter) const;
    // Parses size.
    unsigned long parseSize(const std::string &size_str) const;
    // Parses directive.
    bool parseDirective(const std::vector<std::string> &tokens, ServerConfig *current_server, LocationConfig *current_location);
    // Parses file.
    bool parseFile(const std::string &path);
    // Splits the configuration text into parser tokens.
    std::vector<ConfigToken> tokenizeConfig(const std::string &content) const;
    // Parses token stream.
    bool parseTokenStream(const std::vector<ConfigToken> &tokens);
    // Parses server block.
    bool parseServerBlock(const std::vector<ConfigToken> &tokens, size_t &index, std::map<int, std::set<std::string> > &all_server_names);
    // Parses location block.
    bool parseLocationBlock(const std::vector<ConfigToken> &tokens, size_t &index, ServerConfig &server, std::set<std::string> &current_location_paths);
    // Parses directive tokens.
    bool parseDirectiveTokens(const std::vector<ConfigToken> &tokens, size_t &index, std::vector<std::string> &directive_tokens) const;
    // Checks server name is new.
    bool validateServerNameIsNew(ServerConfig &server, std::map<int, std::set<std::string> > &all_server_names) const;
    // Parses one server setting and saves its value.
    bool parseServerDirective(const std::string &directive, const std::vector<std::string> &values, ServerConfig *srv);
    // Parses one location setting and saves its value.
    bool parseLocationDirective(const std::string &directive, const std::vector<std::string> &values, LocationConfig *srv);
    // Checks that server listen address and port pairs do not conflict.
    bool serversHaveUniqueListenPairs() const;
public:
    // Creates a new Config object.
    Config(const std::string &path);
    // Cleans up this object and its owned resources.
    virtual ~Config();
    // Returns servers.
    std::vector<ServerConfig> &getServers();
    // Returns servers.
    const std::vector<ServerConfig> &getServers() const;
    bool error;
    // Prints config.
    void printConfig() const;
};
#endif
