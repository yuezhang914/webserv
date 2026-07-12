/*
文件：srcs/Config/ConfigParser.cpp
配置文件语法解析实现。本文件负责把 default.conf 这类文本配置拆成 token 流，然后按 server/location/directive/block 的语法建立 Config::servers。
健壮版 parser 不再要求 } 单独占一行，只要求普通指令用 ; 结束，因此可以解析 server { listen 3435; root srv/www; } 这种写法。
*/
#include "Webserv.hpp"
#include <climits>

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
函数：findUnsupportedQuoteLine
用途：检查配置正文中是否出现当前语法不支持的单引号或双引号。
参数来源：parseFile 读取完整配置文本后、调用 tokenizeConfig 之前传入。
变量解释：
    - content：完整配置文件文本。
    - quote_line：输出参数；发现引号时记录所在行号。
    - line：当前扫描行号，从 1 开始。
    - in_comment：当前是否位于 # 开始的行注释中。
实现逻辑：
    1. 从左到右扫描配置正文。
    2. # 之后直到换行都属于注释，注释里的引号不会参与配置语法。
    3. 在注释外遇到单引号或双引号时立即记录行号并返回 true。
    4. 全文没有不支持的引号时返回 false。
说明：当前配置格式不支持 quoted string，因此不会建立引号状态，也不会让引号改变空白、#、{、}、; 的含义。
*/
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

/*
函数：Config::tokenizeConfig
用途：把整个配置文件文本拆成 token 流。
参数来源：parseFile 读完整个配置文件并确认没有不支持的引号后传入 content。
变量解释：
    - content：parseFile 读出的完整配置文件文本。
    - tokens：函数最终返回的 token 数组，每个元素保存 token 文本和行号。
    - current：正在累积的普通 token，例如 listen 或 srv/www。
    - current_line：current 这个 token 开始出现的行号。
    - line：当前扫描位置所在行号，从 1 开始。
    - index：当前正在扫描 content 的下标。
    - c：content[index] 当前字符。
实现逻辑：
    1. 从左到右扫描每个字符。
    2. 空白字符结束当前普通 token；换行同时增加行号。
    3. # 到当前行末尾作为注释跳过。
    4. {、}、; 无论是否贴着其他字符，都单独成为 token。
    5. 其他字符累积到 current。
说明：当前配置格式不支持单引号或双引号；parseFile 会在进入本函数前直接拒绝它们。
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

// 🚀 注意：我们将传入的账本升级为 map<端口, set<域名>> 复合物理索引网关
bool Config::validateServerNameIsNew(ServerConfig &server, std::map<int, std::set<std::string> > &all_server_names) const
{
    size_t index = 0;
    int current_port = server.port; // 💡 动态提取当前服务器实例所监听的特定物理端口

    // 提取或创建属于当前特定端口的独立域名防伪子沙盒（局部 set 引用）
    std::set<std::string> &port_scoped_names = all_server_names[current_port];

    while (index < server.server_names.size())
    {
        const std::string &name = server.server_names[index];

        // 🔒 像素级互锁：只有在【同一个物理端口内】发生了域名重叠，才是工业级违规！
        if (port_scoped_names.find(name) != port_scoped_names.end())
        {
            std::cerr << "Error: Duplicate server_name \"" << name
                      << "\" detected specifically on port " << current_port << std::endl;
            return ERROR;
        }

        // 安全通关后，仅将其织入属于当前端口的局部防伪结界中
        port_scoped_names.insert(name);
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
    directive_tokens.clear();
    while (index < tokens.size())
    {
        const std::string &val = tokens[index].value;

        if (val == ";")
        {
            index++;
            // 🔒 防空防御：如果进来直接撞见分号（如 `;`），或者只有指令名就撞见分号（如 `listen;`）
            if (directive_tokens.empty() || directive_tokens.size() == 1)
            {
                std::cerr << "Error: Invalid empty directive or missing arguments near line " << tokens[index - 1].line << std::endl;
                return ERROR;
            }
            return SUCCESS;
        }

        if (val == "{" || val == "}")
        {
            // 如果容器是空的，说明一开局就撞见大括号（非法断句）
            if (directive_tokens.empty())
                std::cerr << "Error: Expected directive name, but found '" << val << "' near line " << tokens[index].line << std::endl;
            else
                std::cerr << "Error: Missing ';' before '" << val << "' near line " << tokens[index].line << std::endl;
            return ERROR;
        }

        // 🟢 无论是第一个进来的指令名，还是后面的参数，统统平铺推入
        directive_tokens.push_back(val);
        index++;
    }

    // 触底断言：全读完了还没分号
    std::cerr << "Error: Missing ';' after directive" << std::endl;
    return ERROR;
}

// 函数：Config::parseLocationBlock
// 用途：
//     解析一个完整的 location { ... } 配置块，并把其中的配置保存到当前
//     ServerConfig 的 locations 数组中。
// 参数来源：
//     - tokens：
//       tokenizeConfig() 生成的完整配置 token 流。
//       调用本函数时，tokens[index] 应当正好是 "location"。
//     - index：
//       当前解析位置的引用参数。
//       调用时指向 "location"；
//       解析成功后移动到该 location 右大括号 "}" 后面的 token。
//     - server：
//       parseServerBlock() 当前正在填充的 ServerConfig。
//       解析出的 LocationConfig 最终会加入 server.locations。
//     - current_location_paths：
//       当前 server 内已经出现过的 location path 集合。
//       用于拒绝同一个 server 中重复的 location 路径。
// 变量解释：
//     - target_path：
//       location 后面的 URI 路径前缀，例如 "/"、"/upload/"、"/cgi/"。
//     - current_loc_idx：
//       新建 LocationConfig 在 server.locations 中的下标。
//       后续始终通过下标重新取得对象，避免 vector 扩容后旧引用失效。
//     - current_val：
//       当前正在检查的 token 内容，用来识别右大括号、嵌套 block
//       或普通 directive。
//     - directive_tokens：
//       保存一条普通 location 指令及其参数。
//       例如 index index.html; 会被保存为 ["index", "index.html"]。
// 实现逻辑：
//     1. 检查 location 后面至少还有 path 和左大括号。
//     2. 检查 path 不是 {、}、; 这类结构符号。
//     3. 检查 path 后面必须紧跟左大括号 "{"。
//     4. 检查 location path 必须以 "/" 开头。
//     5. 使用 current_location_paths 检查当前 server 内是否存在重复 path。
//     6. 在 server.locations 中创建新的 LocationConfig，并保存 path。
//     7. 记录新 location 的下标，避免长期持有可能因 vector 扩容而失效的引用。
//     8. 跳过 location、path、"{"，开始解析 block 内部内容。
//     9. 遇到 "}" 时结束当前 location，并把 index 移到右大括号之后。
//     10. location 内禁止继续嵌套 location 或 server block。
//     11. 普通指令先由 parseDirectiveTokens() 读取到分号，
//         再由 parseDirective() 分发给 parseLocationDirective()。
//     12. 如果读到文件末尾仍没有遇到右大括号，返回未闭合 block 错误。
// 返回值：
//     - SUCCESS：
//       location block 语法正确，所有指令成功写入 LocationConfig。
//     - ERROR：
//       location 开头格式错误、path 非法、path 重复、出现嵌套 block、
//       directive 非法、缺少分号或缺少右大括号。
bool Config::parseLocationBlock(const std::vector<ConfigToken> &tokens, size_t &index, ServerConfig &server, std::set<std::string> &current_location_paths)
{
    // 【防呆检查 1】此时 index 指向 "location"。检查后面是不是至少还有两个词（比如 "/api" 和 "{"）
    if (index + 2 >= tokens.size())
    {
        std::cerr << "Error: Invalid location block opening near line " << tokens[index].line << std::endl;
        return ERROR;
    }

    // 【防呆检查 2】检查 location 后面的词是不是符号（比如直接写了 location { ）
    if (isBlockSymbol(tokens[index + 1].value))
    {
        std::cerr << "Error: Expected location path near line " << tokens[index].line << std::endl;
        return ERROR;
    }

    // 【防呆检查 3】检查房间名后面，是不是跟着左大括号 "{"
    if (tokens[index + 2].value != "{")
    {
        std::cerr << "Error: Expected '{' after location path near line " << tokens[index].line << std::endl;
        return ERROR;
    }

    std::string target_path = tokens[index + 1].value;

    // location path 必须是 URI 路径前缀，因此必须从 / 开始。
    if (target_path.empty() || target_path[0] != '/')
    {
        std::cerr << "Error: Location path must start with '/' near line "
                  << tokens[index + 1].line << std::endl;
        return ERROR;
    }

    // 【核心查重】翻登记簿，看看这个房间名是不是已经存在了
    if (current_location_paths.find(target_path) != current_location_paths.end())
    {
        std::cerr << "Error: Duplicate location path: " << target_path << " in server" << std::endl;
        return ERROR;
    }

    // 前期检查全部通过！正式开始建房间
    server.locations.push_back(LocationConfig());

    /* 🛠️ 【修改点 1：绝杀 vector 扩容内存悬空陷阱，采用物理下标寻址】 */
    // 解释：不持有 locations.back() 的物理引用，改为记录当前新房间在 vector 中的安全下标位置
    size_t current_loc_idx = server.locations.size() - 1;
    server.locations[current_loc_idx].path = target_path;
    current_location_paths.insert(target_path);

    index += 3; // 手指一口气往后移三步，跳过 "location"、"path"、"{"

    // 开始循环阅读房间里面的要求
    while (index < tokens.size())
    {
        // 【成功收工】遇到属于这个房间的 "}"
        if (tokens[index].value == "}")
        {
            index++;
            return SUCCESS;
        }

        // location 内不能继续嵌套 location 或 server block。
        std::string current_val = tokens[index].value;
        if (current_val == "location" || current_val == "server")
        {
            std::cerr << "Error: Nested block keyword '" << current_val
                      << "' is not allowed inside location near line " << tokens[index].line << std::endl;
            return ERROR;
        }

        // 【防乱码】房间里不允许出现孤零零的奇怪标点
        if (current_val == "{")
        {
            std::cerr << "Error: Unexpected token '" << current_val << "' in location block near line " << tokens[index].line << std::endl;
            return ERROR;
        }

        // 处理房间里的普通指令
        std::vector<std::string> directive_tokens;
        if (parseDirectiveTokens(tokens, index, directive_tokens) == ERROR)
            return ERROR;

        /* 🚀 【动态安全交付】：通过之前锁死的物理下标，动态提取 100% 绝对正确的当前房间物理内存地址 */
        if (parseDirective(directive_tokens, &server, &(server.locations[current_loc_idx])) == ERROR)
            return ERROR;
    }

    // 如果一直读到了文件末尾，都没发现房间的右大括号 "}"
    std::cerr << "Error: Unclosed location block: " << server.locations[current_loc_idx].path << std::endl;
    return ERROR;
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
bool Config::parseServerBlock(const std::vector<ConfigToken> &tokens, size_t &index, std::map<int, std::set<std::string> > &all_server_names)
{
    // 【条件1】检查 "server" 后面是不是紧跟着一个左大括号 "{"
    if (index + 1 >= tokens.size() || tokens[index + 1].value != "{")
    {
        std::cerr << "Error: Expected '{' after server near line " << tokens[index].line << std::endl;
        return ERROR;
    }

    // 成功确认是服务器块，准备开始记录它的配置
    servers.push_back(ServerConfig());

    /* 🛠️ 【修改点 1：绝杀 vector 扩容搬家内存陷阱，升级为安全下标动态寻址】 */
    // 解释：不长时间持有 servers.back() 的物理引用，改用 current_srv_idx 锁死内存坑位
    size_t current_srv_idx = servers.size() - 1;
    std::set<std::string> current_location_paths;

    index += 2; // 跳过 "server" 和 "{"

    // 开始循环阅读大括号里面的每一行要求
    while (index < tokens.size())
    {
        // 【条件2】如果读到了右大括号 "}"，说明这栋“别墅”的设计图读完了
        if (tokens[index].value == "}")
        {
            index++;
            // 🚀 动态捞取绝对安全的物理地址进行最终的域名防伪校验
            return validateServerNameIsNew(servers[current_srv_idx], all_server_names);
        }

        // server block 内不能再嵌套另一个 server block。
        std::string current_val = tokens[index].value;
        if (current_val == "server")
        {
            std::cerr << "Error: Nested server block is not allowed near line "
                      << tokens[index].line << std::endl;
            return ERROR;
        }

        // 【条件4】如果读到了 "location"，说明要开始建“房间”了
        if (current_val == "location")
        {
            // 🚀 安全动态交付当前别墅地址
            if (parseLocationBlock(tokens, index, servers[current_srv_idx], current_location_paths) == ERROR)
                return ERROR;
            continue;
        }

        // 【条件5】如果读到了孤零零的 "{"
        if (current_val == "{")
        {
            std::cerr << "Error: Unexpected token '" << current_val << "' in server block near line " << tokens[index].line << std::endl;
            return ERROR;
        }

        // 如果上面都不是，那说明这是一条普通的配置指令（比如 listen 80;）
        std::vector<std::string> directive_tokens;
        if (parseDirectiveTokens(tokens, index, directive_tokens) == ERROR)
            return ERROR;

        // 🚀 【动态安全分发】：将指令真正应用到 100% 物理绝对正确的这栋“别墅”上
        if (parseDirective(directive_tokens, &(servers[current_srv_idx]), NULL) == ERROR)
            return ERROR;
    }

    // 【条件8】如果把所有的词都读完了，还没碰到右大括号 "}"
    std::cerr << "Error: Unclosed server block" << std::endl;
    return ERROR;
}

/*
函数：Config::parseTokenStream
用途：解析完整配置 token 流，并填充 Config::servers。
参数来源：parseFile 调用 tokenizeConfig 后传入 tokens。
变量解释：
    - tokens：tokenizeConfig 的输出，表示整个配置文件的 token 流。
    - index：当前顶层扫描位置；parseServerBlock 会把它推进到当前 server block 之后。
实现逻辑：
    1. 空 token 流属于无效配置。
    2. 顶层只允许连续出现一个或多个 server block。
    3. 每遇到 server，就调用 parseServerBlock 解析完整 block。
    4. 顶层出现其他 token，立即报告“server block 外存在非法内容”。
    5. 解析结束后确认至少生成了一个 ServerConfig。
说明：当前配置格式没有额外的顶层包装 block。
*/
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
            std::cerr << "Error: Only server blocks are allowed at top level near line "
                      << tokens[index].line << ": " << tokens[index].value << std::endl;
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
用途：把 max_body_size 的严格文本值转换成字节数。
参数来源：parseServerDirective 或 parseLocationDirective 解析 max_body_size 1M; 时传入 "1M"。
变量解释：
    - size_str：配置里的原始大小字符串，例如 1M、512K、1024。
    - num_str：去掉可选单位后的纯数字部分。
    - unit：K/M/G 单位的大写形式；没有单位时为 \0。
    - multiplier：单位对应的乘数；无单位为 1。
    - num：逐位累积得到的数字部分。
实现逻辑：
    1. 字符串不能为空；末尾只有一个 K/M/G 单位时才接受单位。
    2. 数字部分必须全部由 0-9 组成，因此 +1、-1、1.5 都会被拒绝。
    3. 逐位计算 num，并在乘 10、加 digit 前检查 unsigned long 溢出。
    4. 根据单位得到 multiplier，并在最终乘法前再次检查溢出。
    5. 成功返回字节数；任何格式或溢出错误返回 ERROR_PARSE_SIZE。
例子：1M -> 1048576；512K -> 524288；+1 和超大 G 值都会被拒绝。
*/
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
        std::cerr << "Error: Size unit multiplication overflows unsigned long: "
                  << size_str << std::endl;
        return static_cast<unsigned long>(ERROR_PARSE_SIZE);
    }
    return num * multiplier;
}

/*
函数：Config::parseFile
用途：读取整个配置文件，把文本规则变成 servers 列表。
参数来源：Config 构造函数传入的配置文件路径。
变量解释：
    - path：main/Config 构造函数传入的配置文件路径。
    - file：ifstream 对象，用来打开并读取 path。
    - buffer：ostringstream，用来一次性接收 file.rdbuf() 的全部内容。
    - content：配置文件的完整文本。
    - quote_line：发现不支持的引号时记录其行号。
    - tokens：tokenizeConfig(content) 的结果，传给 parseTokenStream。
实现逻辑：
    1. 用 ifstream 打开 path，打不开就返回 ERROR。
    2. 把整个文件读入 content。
    3. 检查注释外是否出现单引号或双引号；当前语法不支持 quoted string，出现即报错。
    4. 调用 tokenizeConfig，把正文拆成带行号的 token 流。
    5. 调用 parseTokenStream，按 server/location/directive 语法建立 servers。
产出：servers 成员变量被填好，后续 ServerManager、Request 和 Response 都读取这些配置对象。
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

    std::string content = buffer.str();
    size_t quote_line = 0;
    if (findUnsupportedQuoteLine(content, quote_line))
    {
        std::cerr << "Error: Quoted strings are not supported near line "
                  << quote_line << std::endl;
        return ERROR;
    }

    std::vector<ConfigToken> tokens = tokenizeConfig(content);
    return parseTokenStream(tokens);
}
