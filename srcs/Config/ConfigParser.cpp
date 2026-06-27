/*
文件：srcs/Config/ConfigParser.cpp
配置文件语法解析实现。本文件负责把 default.conf 这类文本配置拆成 token 流，然后按 server/location/directive/block 的语法建立 Config::servers。
健壮版 parser 不再要求 } 单独占一行，只要求普通指令用 ; 结束，因此可以解析 server { listen 3435; root srv/www; } 这种写法。
*/
#include "ConfigParser.hpp"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

/*
函数：ConfigToken::ConfigToken
用途：创建一个空 token。
变量解释：
    - value：ConfigToken 成员，保存 token 文本；默认设为空。
    - line：ConfigToken 成员，保存 token 所在行号；默认设为 0，表示还没有真实来源行。
实现逻辑：value 为空字符串，line 设为 0；主要用于 vector 临时对象或默认初始化。
*/
ConfigToken::ConfigToken()
    : value(""), line(0)
{
}

/*
函数：ConfigToken::ConfigToken(value, line)
用途：创建一个带文本和行号的配置 token。
参数来源：tokenizeConfig 扫描配置文件时生成；token_value 是 token 文本，token_line 是它所在的行号。
变量解释：
    - token_value：调用者传入的新 token 文本，例如 server、{、listen、3435、;。
    - token_line：调用者传入的行号，来自 tokenizeConfig 扫描 content 时维护的 line。
    - value：对象成员，接收 token_value。
    - line：对象成员，接收 token_line，后续用于错误提示。
实现逻辑：直接保存 value 和 line，后续 parser 用 value 判断语法，用 line 输出错误位置。
*/
ConfigToken::ConfigToken(const std::string &token_value, size_t token_line)
    : value(token_value), line(token_line)
{
}

/*
函数：isBlockSymbol
用途：判断 token 是否是配置语法里的结构符号。
参数来源：parseServerBlock、parseLocationBlock、parseDirectiveTokens 检查当前 token 时传入。
变量解释：
    - token：调用者传入的 token 文本，通常是 tokens[index].value 或 tokens[index + n].value。
    - 返回值：true 表示 token 是语法符号，不应该被当成 directive 名或普通参数。
实现逻辑：如果 token 是 {、} 或 ;，返回 true；否则返回 false。
为什么需要：directive 名、location path、directive 参数不能被这些符号替代。
*/
static bool isBlockSymbol(const std::string &token)
{
    return token == "{" || token == "}" || token == ";";
}

/*
函数：Config::tokenizeConfig
用途：把整个配置文件文本拆成 token 流。
参数来源：parseFile 读完整个配置文件后传入 content。
变量解释：
    - content：parseFile 读出的完整配置文件文本。
    - tokens：函数最终返回的 token 数组，每个元素保存 token 文本和行号。
    - current：正在累积的普通 token，例如 listen 或 srv/www。
    - current_line：current 这个 token 开始出现的行号。
    - line：当前扫描位置所在行号，从 1 开始。
    - index：当前正在扫描 content 的下标。
    - c：content[index] 当前字符，用来判断空白、注释、括号、分号或普通字符。
实现逻辑：
    1. 从左到右扫描每个字符。
    2. 空白字符会结束当前 token；换行会让 line 递增。
    3. # 到当前行末尾都作为注释跳过。
    4. {、}、; 无论是否贴着别的字符，都会单独成为 token。
    5. 其他字符累积到当前 token。
例子：server{listen 3435;} 会变成 server、{、listen、3435、;、}。
*/
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

/*
函数：Config::validateServerNameIsNew
用途：在一个 server block 成功关闭时，登记它的 server_name 并检查跨 server 重复。
参数来源：parseServerBlock 遇到关闭 server 的 } 时传入当前 server 和 all_server_names。
变量解释：
    - server：刚解析完成并准备关闭的 ServerConfig，里面的 server_names 已经收集完整。
    - all_server_names：整个配置文件中之前 server 已登记的名字集合，用来检查跨 server 重复。
    - index：遍历 server.server_names 的下标。
    - name：当前正在检查的一个 server_name。
实现逻辑：
    1. 遍历当前 server.server_names。
    2. 如果名字已经存在于 all_server_names，说明不同 server 使用了重复 server_name，返回 ERROR。
    3. 否则把名字插入 all_server_names。
为什么在 server 关闭时做：只有 server block 全部解析完后，它的 server_name 才是完整的。
*/
bool Config::validateServerNameIsNew(ServerConfig &server, std::set<std::string> &all_server_names) const
{
    size_t index = 0;

    while (index < server.server_names.size())
    {
        const std::string &name = server.server_names[index];
        if (all_server_names.find(name) != all_server_names.end())
        {
            std::cerr << "Duplicate server_name: " << name << " across multiple servers" << std::endl;
            return ERROR;
        }
        all_server_names.insert(name);
        index++;
    }
    return SUCCESS;
}

/*
函数：Config::parseDirectiveTokens
用途：从 token 流中读取一条以 ; 结束的普通配置指令。
参数来源：parseServerBlock 或 parseLocationBlock 发现当前 token 不是 block 关键字时调用。
变量解释：
    - tokens：tokenizeConfig 生成的完整 token 流。
    - index：引用参数，调用时指向 directive 名；函数成功后移动到分号后面的 token。
    - directive_tokens：输出参数，保存一整条 directive，例如 [listen, 127.0.0.1:3435]。
    - tokens[index].value：当前读取到的 token 文本，用来判断是否到达 ; 或遇到非法 { / }。
实现逻辑：
    1. 当前 token 作为 directive 名，例如 listen、root、cgi_extension。
    2. 继续读取后面的 token 作为参数，直到遇到 ;。
    3. 如果在 ; 之前遇到 { 或 }，说明 directive 没正常结束，返回 ERROR。
    4. 如果文件结束还没遇到 ;，说明缺少分号，返回 ERROR。
    5. 成功时 index 移到 ; 后面的 token。
注意：健壮版 parser 强制普通指令以 ; 结束，所以 upload_path 这类指令也必须写分号。
*/
bool Config::parseDirectiveTokens(const std::vector<ConfigToken> &tokens, size_t &index, std::vector<std::string> &directive_tokens) const
{
    if (index >= tokens.size() || isBlockSymbol(tokens[index].value))
    {
        std::cerr << "Error: Expected directive near line " << (index < tokens.size() ? tokens[index].line : 0) << std::endl;
        return ERROR;
    }
    directive_tokens.clear();
    directive_tokens.push_back(tokens[index].value);
    index++;
    while (index < tokens.size())
    {
        if (tokens[index].value == ";")
        {
            index++;
            return SUCCESS;
        }
        if (tokens[index].value == "{" || tokens[index].value == "}")
        {
            std::cerr << "Error: Missing ';' before '" << tokens[index].value << "' near line " << tokens[index].line << std::endl;
            return ERROR;
        }
        directive_tokens.push_back(tokens[index].value);
        index++;
    }
    std::cerr << "Error: Missing ';' after directive: " << directive_tokens[0] << std::endl;
    return ERROR;
}

/*
函数：Config::parseLocationBlock
用途：解析一个完整 location /path { ... } block。
参数来源：parseServerBlock 在 server 内部遇到 location token 时调用；index 指向 location。
变量解释：
    - tokens：完整 token 流，提供 location path、{、directive、} 等语法元素。
    - index：引用参数，调用时指向 location；成功返回时移动到 location 关闭 } 后面。
    - server：当前 location 所属的 ServerConfig，新的 LocationConfig 会 push 到 server.locations。
    - current_location_paths：当前 server 内已经出现过的 location path 集合，用来拒绝重复 location。
    - location：server.locations.back() 的引用，表示正在填充的当前 location。
    - directive_tokens：临时数组，保存 parseDirectiveTokens 读出的一条 location directive。
实现逻辑：
    1. 检查 location 后必须有路径和 {。
    2. 检查同一个 server 内 location path 不能重复。
    3. 在 server.locations 里创建 LocationConfig，并记录 path。
    4. 继续解析 location 内部 directive，全部交给 parseLocationDirective。
    5. 遇到 } 时关闭当前 location 并返回。
    6. location 内不允许嵌套 location 或 server。
*/
bool Config::parseLocationBlock(const std::vector<ConfigToken> &tokens, size_t &index, ServerConfig &server, std::set<std::string> &current_location_paths)
{
    // 【防呆检查 1】此时 index 指向 "location"。检查后面是不是至少还有两个词（比如 "/api" 和 "{"）
    if (index + 2 >= tokens.size())
    {
        std::cerr << "Error: Invalid location block opening near line " << tokens[index].line << std::endl;
        return ERROR; // 词不够了，图纸残缺，报错
    }
    
    // 【防呆检查 2】检查 location 后面的词是不是符号（比如直接写了 location { ）
    if (isBlockSymbol(tokens[index + 1].value))
    {
        std::cerr << "Error: Expected location path near line " << tokens[index].line << std::endl;
        return ERROR; // 连个房间名都没有，直接给大括号，报错
    }
    
    // 【防呆检查 3】检查房间名后面，是不是跟着左大括号 "{"
    if (tokens[index + 2].value != "{")
    {
        std::cerr << "Error: Expected '{' after location path near line " << tokens[index].line << std::endl;
        return ERROR; // 没按规矩写大括号，报错
    }
    
    // 【核心查重】翻登记簿，看看这个房间名是不是已经存在了（这就是宿主刚才问的那个问题！）
    if (current_location_paths.find(tokens[index + 1].value) != current_location_paths.end())
    {
        std::cerr << "Duplicate location path: " << tokens[index + 1].value << " in server" << std::endl;
        return ERROR; // 名字重复，报错
    }

    // 前期检查全部通过！正式开始建房间
    server.locations.push_back(LocationConfig()); // 在别墅的图纸里，真正画上一个新房间
    LocationConfig &location = server.locations.back(); // 拿到这个新房间的控制权
    location.path = tokens[index + 1].value;      // 给房间挂上门牌号（比如 "/api"）
    current_location_paths.insert(location.path); // 【关键！】把新挂上的门牌号，写进“房间登记簿”里，防别人重名
    
    index += 3; // 手指一口气往后移三步，跳过 "location"、"/api"、"{" 这三个词，准备读里面的家具配置

    // 开始循环阅读房间里面的要求
    while (index < tokens.size())
    {
        // 【成功收工】遇到属于这个房间的 "}"
        if (tokens[index].value == "}")
        {
            index++; // 手指移到下一个词
            return SUCCESS; // 房间建好了，成功返回，把控制权还给大别墅！
        }
        
        // 【防套娃 1】房间里不能再建房间
        if (tokens[index].value == "location")
        {
            std::cerr << "Error: Nested location block is not allowed near line " << tokens[index].line << std::endl;
            return ERROR;
        }
        
        // 【防套娃 2】房间里更不能建大别墅
        if (tokens[index].value == "server")
        {
            std::cerr << "Error: Nested server block is not allowed near line " << tokens[index].line << std::endl;
            return ERROR;
        }
        
        // 【防乱码】房间里不允许出现孤零零的奇怪标点
        if (tokens[index].value == "{" || tokens[index].value == ";")
        {
            std::cerr << "Error: Unexpected token '" << tokens[index].value << "' in location block near line " << tokens[index].line << std::endl;
            return ERROR;
        }

        // 处理房间里的普通指令（比如 root /var/www; 或者 index index.html;）
        std::vector<std::string> directive_tokens;
        // 组装指令词语
        if (parseDirectiveTokens(tokens, index, directive_tokens) == ERROR)
            return ERROR;
            
        // 将指令应用到这个房间（注意这里的参数，传了 &server 也传了 &location，说明指令会配置在这个小房间上）
        if (parseDirective(directive_tokens, &server, &location) == ERROR)
            return ERROR;
    }
    
    // 如果一直读到了文件末尾，都没发现房间的右大括号 "}"
    std::cerr << "Error: Unclosed location block: " << location.path << std::endl;
    return ERROR; // 烂尾房间，报错！
}

/*
函数：Config::parseServerBlock
用途：解析一个完整 server { ... } block。
参数来源：parseTokenStream 在顶层遇到 server token 时调用；index 指向 server。
变量解释：
    - tokens：完整 token 流，提供 server 内所有 directive、location block 和关闭 }。
    - index：引用参数，调用时指向 server；成功返回时移动到 server 关闭 } 后面。
    - all_server_names：整个配置文件里已关闭 server 的 server_name 集合，用来检查重复。
    - server：servers.back() 的引用，表示本次新建并正在填充的 ServerConfig。
    - current_location_paths：只属于当前 server 的 location path 集合，用来检查同一 server 内 location 不重复。
    - directive_tokens：临时数组，保存 parseDirectiveTokens 读出的一条 server directive。
实现逻辑：
    1. 检查 server 后必须直接跟 {。
    2. 创建新的 ServerConfig，并用 current_location_paths 记录本 server 内已出现的 location path。
    3. server 内遇到 location 时调用 parseLocationBlock。
    4. server 内遇到普通指令时读取到 ;，再交给 parseServerDirective。
    5. 遇到 } 时关闭 server，检查 server_name 是否重复，然后返回。
    6. server 内不允许嵌套 server。
*/
bool Config::parseServerBlock(const std::vector<ConfigToken> &tokens, size_t &index, std::set<std::string> &all_server_names)
{
    // 【条件1】检查 "server" 后面是不是紧跟着一个左大括号 "{"
    if (index + 1 >= tokens.size() || tokens[index + 1].value != "{")
    {
        std::cerr << "Error: Expected '{' after server near line " << tokens[index].line << std::endl;
        return ERROR; // 格式不对，直接报错返回
    }

    // 成功确认是服务器块，准备开始记录它的配置
    servers.push_back(ServerConfig());        // 在我们的“别墅群”列表里，新建一栋“别墅”
    ServerConfig &server = servers.back();    // 拿到这栋新“别墅”的控制权
    std::set<std::string> current_location_paths; // 用来记录这栋别墅里有哪些“房间”（location）
    
    index += 2; // 手指往后移动两步，跳过 "server" 和 "{" 这两个词，准备读里面的内容

    // 开始循环阅读大括号里面的每一行要求
    while (index < tokens.size())
    {
        // 【条件2】如果读到了右大括号 "}"，说明这栋“别墅”的设计图读完了
        // （Server）根本就看不见 location 里的 }，因为那个 } 已经被（Location）自己吃掉并消化了！
        if (tokens[index].value == "}")
        {
            index++; // 手指移到下一个词，为读别的配置做准备
            // 检查这栋别墅的名字是不是和别人撞名了，没撞名就算成功
            return validateServerNameIsNew(server, all_server_names);
        }
        
        // 【条件3】如果在大括号里面又看到了 "server" 这个词
        if (tokens[index].value == "server")
        {
            std::cerr << "Error: Nested server block is not allowed near line " << tokens[index].line << std::endl;
            return ERROR; // 别墅里面不能再套一栋别墅，报错！
        }
        
        // 【条件4】如果读到了 "location"，说明要开始建“房间”了
        if (tokens[index].value == "location")
        {
            // 去专门负责建房间的函数里处理，如果建房间失败了，整个别墅也算失败
            if (parseLocationBlock(tokens, index, server, current_location_paths) == ERROR)
                return ERROR;
            continue; // 房间建好后，直接进入下一轮循环，接着往下读
        }
        
        // 【条件5】如果读到了孤零零的 "{" 或者 ";" 
        if (tokens[index].value == "{" || tokens[index].value == ";")
        {
            std::cerr << "Error: Unexpected token '" << tokens[index].value << "' in server block near line " << tokens[index].line << std::endl;
            return ERROR; // 这是语法错误，就像句子里多出了奇怪的标点，报错！
        }

        // 如果上面都不是，那说明这是一条普通的配置指令（比如 listen 80;）
        std::vector<std::string> directive_tokens;
        // 【条件6】尝试把这条指令的词组装起来
        if (parseDirectiveTokens(tokens, index, directive_tokens) == ERROR)
            return ERROR; // 组装失败，报错！
            
        // 【条件7】尝试把这条指令真正应用到我们的“别墅”配置上
        if (parseDirective(directive_tokens, &server, NULL) == ERROR)
            return ERROR; // 应用失败，报错！
    }
    
    // 【条件8】如果把所有的词都读完了，还没碰到右大括号 "}"
    std::cerr << "Error: Unclosed server block" << std::endl;
    return ERROR; // 括号没闭合，烂尾楼，报错！
}

/*
函数：Config::parseTokenStream
用途：解析完整配置 token 流，并填充 Config::servers。
参数来源：parseFile 调用 tokenizeConfig 后传入 tokens。
变量解释：
    - tokens：tokenizeConfig 的输出，表示整个配置文件的 token 流。
    - index：当前顶层扫描位置；遇到 server block 时会被 parseServerBlock 推进。
    - all_server_names：跨 server 的名字登记表，传给 parseServerBlock 做重复检查。
实现逻辑：
    1. 顶层只允许出现 server block。
    2. 每遇到 server，就调用 parseServerBlock 解析整个 server 区域。
    3. 顶层出现 }、{、;、普通 directive 或 location 都是语法错误。
    4. 所有 token 解析结束后，至少必须有一个 server。
*/
bool Config::parseTokenStream(const std::vector<ConfigToken> &tokens)
{
    size_t index = 0;
    std::set<std::string> all_server_names;

    while (index < tokens.size())
    {
        if (tokens[index].value == "server")
        {
            if (parseServerBlock(tokens, index, all_server_names) == ERROR)
                return ERROR;
            continue;
        }
        if (tokens[index].value == "location")
        {
            std::cerr << "Error: location block must be inside server block near line " << tokens[index].line << std::endl;
            return ERROR;
        }
        if (tokens[index].value == "}")
        {
            std::cerr << "Error: Unexpected closing brace near line " << tokens[index].line << std::endl;
            return ERROR;
        }
        if (tokens[index].value == "{" || tokens[index].value == ";")
        {
            std::cerr << "Error: Unexpected token '" << tokens[index].value << "' near line " << tokens[index].line << std::endl;
            return ERROR;
        }
        std::cerr << "Error: Directive outside server block near line " << tokens[index].line << ": " << tokens[index].value << std::endl;
        return ERROR;
    }
    if (servers.empty())
    {
        std::cerr << "Error: Config must contain at least one server block" << std::endl;
        return ERROR;
    }
    return SUCCESS;
}

/*
函数：Config::parseDirective
用途：解析一条普通配置指令，并决定它应该写入 ServerConfig 还是 LocationConfig。
参数来源：parseDirectiveTokens 从 token 流中读取 directive 到 ; 之间的内容后传入；current_server/current_location 表示当前解析位置。
变量解释：
    - tokens：一条 directive 的普通字符串数组，tokens[0] 是指令名，后面是参数。
    - current_server：当前正在解析的 server；在 server block 内不为空。
    - current_location：当前正在解析的 location；在 location block 内不为空，并且优先于 current_server。
    - directive：从 tokens[0] 复制出来的指令名，例如 root、listen、cgi_extension。
    - values：tokens 去掉第一个元素后的参数数组，例如 [srv/www] 或 [GET, POST]。
实现逻辑：
    1. tokens[0] 是指令名，例如 root、listen、allow_methods。
    2. tokens[1..end] 是指令参数，例如 srv/www、GET POST。
    3. 如果 current_location 不为空，说明当前在 location block 里，交给 parseLocationDirective。
    4. 否则如果 current_server 不为空，说明当前在 server block 里，交给 parseServerDirective。
    5. 如果两者都为空，说明普通指令写在 server 外面，这是配置语法错误。
说明：server/location 的开头、{、} 和 ; 不在这里处理，而是由 token-stream parser 处理。
*/
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

/*
函数：Config::parseSize
用途：把 max_body_size 的文本值转成字节数。
参数来源：parseServerDirective 解析 max_body_size 1M; 时传入 "1M"。
变量解释：
    - size_str：配置里原始大小字符串，例如 1M、512K、1024。
    - num_str：去掉单位后的数字部分字符串，交给 strtoul 转换。
    - unit：保存 K/M/G 这类单位；没有单位时为 \0。
    - endptr：strtoul 输出参数，指向数字解析停止的位置，用于判断是否有非法字符。
    - num：数字部分转成的 unsigned long，最后根据 unit 换算成字节数。
实现逻辑：
    1. 先复制输入到 num_str，并准备 unit 保存单位。
    2. 如果最后一个字符是字母，就把它当单位 K/M/G，并从数字字符串中删除。
    3. 用 strtoul 把数字部分转成 unsigned long。
    4. 如果数字格式非法，返回 ERROR_PARSE_SIZE。
    5. 根据单位把数值乘以 1024、1024*1024 或 1024*1024*1024。
    6. 单位不是 K/M/G 且不为空时，返回 ERROR_PARSE_SIZE。
例子：1M -> 1048576；512K -> 524288。
*/
unsigned long Config::parseSize(const std::string &size_str) const
{
    std::string num_str = size_str;
    char unit = '\0';

    if (!num_str.empty() && isalpha(num_str[num_str.length() - 1]))
    {
        unit = toupper(num_str[num_str.length() - 1]);
        num_str.erase(num_str.length() - 1);
    }
    char *endptr;
    unsigned long num = strtoul(num_str.c_str(), &endptr, 10);
    if (*endptr != '\0' || num_str.empty())
    {
        std::cout << "Error: Invalid size format: " << size_str << std::endl;
        return ERROR_PARSE_SIZE;
    }
    if (unit == 'K')
        num *= 1024;
    else if (unit == 'M')
        num *= 1024 * 1024;
    else if (unit == 'G')
        num *= 1024 * 1024 * 1024;
    else if (unit != '\0')
    {
        std::cout << "Error: Invalid size unit: " << size_str << std::endl;
        return ERROR_PARSE_SIZE;
    }
    return num;
}

/*
函数：Config::parseFile
用途：读取整个配置文件，把文本规则变成 servers 列表。
参数来源：Config 构造函数传入的配置文件路径。
变量解释：
    - path：main/Config 构造函数传入的配置文件路径。
    - file：ifstream 对象，用来打开并读取 path。
    - buffer：ostringstream，用来一次性接收 file.rdbuf() 的全部内容。
    - tokens：tokenizeConfig(buffer.str()) 的结果，传给 parseTokenStream。
实现逻辑：
    1. 用 ifstream 打开 path，打不开就返回 ERROR。
    2. 把整个文件读入字符串。
    3. 调用 tokenizeConfig，把文件拆成带行号的 token 流。
    4. 调用 parseTokenStream 按 server/location/directive 语法解析所有 token。
    5. parseTokenStream 会负责检查多余 }、缺少 }、缺少 ;、location 写在 server 外、嵌套 block 等错误。
产出：servers 成员变量被填好，后续 setupSockets/serverLoop 都依赖它。
*/
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

    std::vector<ConfigToken> tokens = tokenizeConfig(buffer.str());
    return parseTokenStream(tokens);
}

/*
函数：Config::serversHaveRoot
用途：检查每个 server 是否有 root 指令。
变量解释：
    - servers：Config 的成员变量，保存所有已经解析出的 ServerConfig。
    - it：遍历 servers 的 const_iterator。
    - it->has_root：每个 server 是否显式配置过 root 的标志。
实现逻辑：
    1. 遍历 servers。
    2. 如果某个 ServerConfig.has_root 是 false，说明没有配置 root，返回 ERROR。
    3. 所有 server 都有 root，返回 SUCCESS。
为什么重要：没有 root 时，Webserv 无法把 /ping.html 映射成 srv/www/ping.html 这种真实文件路径。
*/
bool Config::serversHaveRoot() const
{
    for (std::vector<ServerConfig>::const_iterator it = servers.begin(); it != servers.end(); ++it)
    {
        if (it->has_root == false)
            return ERROR;
    }
    return SUCCESS;
}
