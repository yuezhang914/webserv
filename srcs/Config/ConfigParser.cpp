#include "Webserv.hpp"
#include <climits>

// Creates a new ConfigToken object.
ConfigToken::ConfigToken() : value(""), line(0)
{
}

// Creates a new ConfigToken object.
ConfigToken::ConfigToken(const std::string &token_value, size_t token_line) : value(token_value), line(token_line)
{
}

// Checks whether the token is a block symbol.
static bool isBlockSymbol(const std::string &token)
{
    return token == "{" || token == "}" || token == ";";
}

// Finds unsupported quote line.
static bool findUnsupportedQuoteLine(const std::string &content, size_t &quote_line)
{
    size_t index = 0;
    size_t line = 1;
    bool in_comment = false;
    while (index < content.size())
    {
        char c = content[index];
        if (c == '\n')
        {
            line++;
            in_comment = false;
        }
        else if (!in_comment && c == '#')
            in_comment = true;
        else if (!in_comment && (c == '"' || c == '\''))
        {
            quote_line = line;
            return true;
        }
        index++;
    }
    return false;
}

// Splits the configuration text into parser tokens.
std::vector<ConfigToken> Config::tokenizeConfig(const std::string &content) const
{
    std::vector<ConfigToken> tokens;
    std::string current;
    size_t current_line = 1;
    size_t line = 1;
    size_t index = 0;
    while (index < content.size())
    {
        char c = content[index];
        if (c == '#')
        {
            if (!current.empty())
            {
                tokens.push_back(ConfigToken(current, current_line));
                current.clear();
            }
            while (index < content.size() && content[index] != '\n')
                index++;
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(c)))
        {
            if (!current.empty())
            {
                tokens.push_back(ConfigToken(current, current_line));
                current.clear();
            }
            if (c == '\n')
                line++;
            index++;
            continue;
        }
        if (c == '{' || c == '}' || c == ';')
        {
            if (!current.empty())
            {
                tokens.push_back(ConfigToken(current, current_line));
                current.clear();
            }
            tokens.push_back(ConfigToken(std::string(1, c), line));
            index++;
            continue;
        }
        if (current.empty())
            current_line = line;
        current += c;
        index++;
    }
    if (!current.empty())
        tokens.push_back(ConfigToken(current, current_line));
    return tokens;
}

// Checks server name is new.
bool Config::validateServerNameIsNew(ServerConfig &server, std::map<int, std::set<std::string> > &all_server_names) const
{
    size_t index = 0;
    int current_port = server.port;
    std::set<std::string> &port_scoped_names = all_server_names[current_port];
    while (index < server.server_names.size())
    {
        const std::string &name = server.server_names[index];
        if (port_scoped_names.find(name) != port_scoped_names.end())
        {
            std::cerr << "Error: Duplicate server_name \"" << name << "\" on port " << current_port << std::endl;
            return ERROR;
        }
        port_scoped_names.insert(name);
        index++;
    }
    return SUCCESS;
}

// Parses directive tokens.
bool Config::parseDirectiveTokens(const std::vector<ConfigToken> &tokens, size_t &index, std::vector<std::string> &directive_tokens) const
{
    directive_tokens.clear();
    while (index < tokens.size())
    {
        const std::string &val = tokens[index].value;
        if (val == ";")
        {
            index++;
            if (directive_tokens.empty() || directive_tokens.size() == 1)
            {
                std::cerr << "Error: Invalid empty directive or missing arguments near line " << tokens[index - 1].line << std::endl;
                return ERROR;
            }
            return SUCCESS;
        }
        if (val == "{" || val == "}")
        {
            if (directive_tokens.empty())
                std::cerr << "Error: Expected directive name, but found '" << val << "' near line " << tokens[index].line << std::endl;
            else std::cerr << "Error: Missing ';' before '" << val << "' near line " << tokens[index].line << std::endl;
            return ERROR;
        }
        directive_tokens.push_back(val);
        index++;
    }
    std::cerr << "Error: Missing ';' after directive" << std::endl;
    return ERROR;
}

// Parses location block.
bool Config::parseLocationBlock(const std::vector<ConfigToken> &tokens, size_t &index, ServerConfig &server, std::set<std::string> &current_location_paths)
{
    if (index + 2 >= tokens.size())
    {
        std::cerr << "Error: Invalid location block opening near line " << tokens[index].line << std::endl;
        return ERROR;
    }
    if (isBlockSymbol(tokens[index + 1].value))
    {
        std::cerr << "Error: Expected location path near line " << tokens[index].line << std::endl;
        return ERROR;
    }
    if (tokens[index + 2].value != "{")
    {
        std::cerr << "Error: Expected '{' after location path near line " << tokens[index].line << std::endl;
        return ERROR;
    }
    std::string target_path = tokens[index + 1].value;
    if (target_path.empty() || target_path[0] != '/')
    {
        std::cerr << "Error: Location path must start with '/' near line " << tokens[index + 1].line << std::endl;
        return ERROR;
    }
    if (current_location_paths.find(target_path) != current_location_paths.end())
    {
        std::cerr << "Error: Duplicate location path: " << target_path << " in server" << std::endl;
        return ERROR;
    }
    server.locations.push_back(LocationConfig());
    size_t current_loc_idx = server.locations.size() - 1;
    server.locations[current_loc_idx].path = target_path;
    current_location_paths.insert(target_path);
    index += 3;
    while (index < tokens.size())
    {
        if (tokens[index].value == "}")
        {
            index++;
            return SUCCESS;
        }
        std::string current_val = tokens[index].value;
        if (current_val == "location" || current_val == "server")
        {
            std::cerr << "Error: Nested block keyword '" << current_val << "' is not allowed inside location near line " << tokens[index].line << std::endl;
            return ERROR;
        }
        if (current_val == "{")
        {
            std::cerr << "Error: Unexpected token '" << current_val << "' in location block near line " << tokens[index].line << std::endl;
            return ERROR;
        }
        std::vector<std::string> directive_tokens;
        if (parseDirectiveTokens(tokens, index, directive_tokens) == ERROR)
            return ERROR;
        if (parseDirective(directive_tokens, &server, &(server.locations[current_loc_idx])) == ERROR)
            return ERROR;
    }
    std::cerr << "Error: Unclosed location block: " << server.locations[current_loc_idx].path << std::endl;
    return ERROR;
}

// Parses server block.
bool Config::parseServerBlock(const std::vector<ConfigToken> &tokens, size_t &index, std::map<int, std::set<std::string> > &all_server_names)
{
    if (index + 1 >= tokens.size() || tokens[index + 1].value != "{")
    {
        std::cerr << "Error: Expected '{' after server near line " << tokens[index].line << std::endl;
        return ERROR;
    }
    servers.push_back(ServerConfig());
    size_t current_srv_idx = servers.size() - 1;
    std::set<std::string> current_location_paths;
    index += 2;
    while (index < tokens.size())
    {
        if (tokens[index].value == "}")
        {
            index++;
            return validateServerNameIsNew(servers[current_srv_idx], all_server_names);
        }
        std::string current_val = tokens[index].value;
        if (current_val == "server")
        {
            std::cerr << "Error: Nested server block is not allowed near line " << tokens[index].line << std::endl;
            return ERROR;
        }
        if (current_val == "location")
        {
            if (parseLocationBlock(tokens, index, servers[current_srv_idx], current_location_paths) == ERROR)
                return ERROR;
            continue;
        }
        if (current_val == "{")
        {
            std::cerr << "Error: Unexpected token '" << current_val << "' in server block near line " << tokens[index].line << std::endl;
            return ERROR;
        }
        std::vector<std::string> directive_tokens;
        if (parseDirectiveTokens(tokens, index, directive_tokens) == ERROR)
            return ERROR;
        if (parseDirective(directive_tokens, &(servers[current_srv_idx]), NULL) == ERROR)
            return ERROR;
    }
    std::cerr << "Error: Unclosed server block" << std::endl;
    return ERROR;
}

// Parses token stream.
bool Config::parseTokenStream(const std::vector<ConfigToken> &tokens)
{
    if (tokens.empty())
    {
        std::cerr << "Error: Empty config file" << std::endl;
        return ERROR;
    }
    size_t index = 0;
    while (index < tokens.size())
    {
        if (tokens[index].value != "server")
        {
            std::cerr << "Error: Only server blocks are allowed at top level near line " << tokens[index].line << ": " << tokens[index].value << std::endl;
            return ERROR;
        }
        if (parseServerBlock(tokens, index, all_server_names) == ERROR)
            return ERROR;
    }
    if (servers.empty())
    {
        std::cerr << "Error: Config must contain at least one server block" << std::endl;
        return ERROR;
    }
    return SUCCESS;
}

// Parses directive.
bool Config::parseDirective(const std::vector<std::string> &tokens, ServerConfig *current_server, LocationConfig *current_location)
{
    if (tokens.empty())
    {
        std::cerr << "Error: No directive in config" << std::endl;
        return ERROR;
    }
    std::string directive = tokens[0];
    std::vector<std::string> values(tokens.begin() + 1, tokens.end());
    if (current_location)
        return parseLocationDirective(directive, values, current_location);
    if (current_server)
        return parseServerDirective(directive, values, current_server);
    std::cerr << "Error: Directive outside server block: " << directive << std::endl;
    return ERROR;
}

// Parses size.
unsigned long Config::parseSize(const std::string &size_str) const
{
    if (size_str.empty())
    {
        std::cerr << "Error: Invalid empty size" << std::endl;
        return static_cast<unsigned long>(ERROR_PARSE_SIZE);
    }
    std::string num_str = size_str;
    char unit = '\0';
    char last = num_str[num_str.length() - 1];
    if (std::isalpha(static_cast<unsigned char>(last)))
    {
        unit = static_cast<char>(std::toupper(static_cast<unsigned char>(last)));
        if (unit != 'K' && unit != 'M' && unit != 'G')
        {
            std::cerr << "Error: Invalid size unit: " << size_str << std::endl;
            return static_cast<unsigned long>(ERROR_PARSE_SIZE);
        }
        num_str.erase(num_str.length() - 1);
    }
    if (num_str.empty())
    {
        std::cerr << "Error: Invalid size format: " << size_str << std::endl;
        return static_cast<unsigned long>(ERROR_PARSE_SIZE);
    }
    unsigned long num = 0;
    size_t index = 0;
    while (index < num_str.size())
    {
        unsigned char c = static_cast<unsigned char>(num_str[index]);
        if (!std::isdigit(c))
        {
            std::cerr << "Error: Invalid size format: " << size_str << std::endl;
            return static_cast<unsigned long>(ERROR_PARSE_SIZE);
        }
        unsigned long digit = static_cast<unsigned long>(num_str[index] - '0');
        if (num > (ULONG_MAX - digit) / 10UL)
        {
            std::cerr << "Error: Size value overflows unsigned long: " << size_str << std::endl;
            return static_cast<unsigned long>(ERROR_PARSE_SIZE);
        }
        num = num * 10UL + digit;
        index++;
    }
    unsigned long multiplier = 1UL;
    if (unit == 'K')
        multiplier = 1024UL;
    else if (unit == 'M')
        multiplier = 1024UL * 1024UL;
    else if (unit == 'G')
        multiplier = 1024UL * 1024UL * 1024UL;
    if (num > ULONG_MAX / multiplier)
    {
        std::cerr << "Error: Size unit multiplication overflows unsigned long: " << size_str << std::endl;
        return static_cast<unsigned long>(ERROR_PARSE_SIZE);
    }
    return num * multiplier;
}

// Parses file.
bool Config::parseFile(const std::string &path)
{
    std::ifstream file(path.c_str());
    if (!file.is_open())
    {
        std::cerr << "Unable to open config file: " << path << std::endl;
        return ERROR;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    file.close();
    std::string content = buffer.str();
    size_t quote_line = 0;
    if (findUnsupportedQuoteLine(content, quote_line))
    {
        std::cerr << "Error: Quoted strings are not supported near line " << quote_line << std::endl;
        return ERROR;
    }
    std::vector<ConfigToken> tokens = tokenizeConfig(content);
    return parseTokenStream(tokens);
}
