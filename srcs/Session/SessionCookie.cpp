/*
文件：srcs/Session/SessionCookie.cpp
用途：实现客户端 Cookie header 解析，以及安全的 Set-Cookie 和删除 Cookie value 生成。
设计原则：解析先写临时 map，全部成功后再替换输出；任何失败都不留下半份结果或旧结果。
输出边界：生成函数只返回 header value，不添加 "Set-Cookie:" 前缀，由 Response 统一写响应头。
标准限制：实现保持 C++98，不依赖外部库或 Boost。
*/

/*
包含：SessionCookie.hpp
用途：取得 SessionCookie 类、CookieMap 和成员函数声明。
*/
#include "SessionCookie.hpp"

/*
包含：<sstream>
用途：使用 std::ostringstream 把 unsigned long 的 Max-Age 转换成字符串。
*/
#include <sstream>

/*
常量组：Cookie 文本限制
用途：限制请求 header、名称、值、Path 和最终 Set-Cookie 长度，防止异常输入造成无界分配。
*/
static const size_t MAX_COOKIE_HEADER_LENGTH = 8192;
static const size_t MAX_COOKIE_NAME_LENGTH = 128;
static const size_t MAX_COOKIE_VALUE_LENGTH = 4096;
static const size_t MAX_COOKIE_PATH_LENGTH = 1024;
static const size_t MAX_SET_COOKIE_LENGTH = 8192;

/*
函数：SessionCookie::trimOws
用途：删除 Cookie pair 两端的普通空格和 tab。
参数：value 是 parseCookieHeader() 按分号切出的片段。
变量：begin/end 表示有效内容的首尾边界。
实现逻辑：从左和右分别跳过 OWS，再返回中间子串。
*/
std::string SessionCookie::trimOws(const std::string &value)
{
    size_t begin = 0;
    while (begin < value.size()
        && (value[begin] == ' ' || value[begin] == '\t'))
        ++begin;
    size_t end = value.size();
    while (end > begin
        && (value[end - 1] == ' ' || value[end - 1] == '\t'))
        --end;
    return value.substr(begin, end - begin);
}

/*
函数：SessionCookie::isTokenChar
用途：判断一个字符是否能出现在 Cookie 名称中。
参数：c 来自 isValidCookieName() 的逐字符遍历。
变量：symbols 保存 HTTP token 允许的特殊字符；value 是无符号字节值。
实现逻辑：字母、数字或 symbols 中字符返回 true。
*/
bool SessionCookie::isTokenChar(char c)
{
    const std::string symbols("!#$%&'*+-.^_`|~");
    unsigned char value = static_cast<unsigned char>(c);
    return (value >= '0' && value <= '9')
        || (value >= 'A' && value <= 'Z')
        || (value >= 'a' && value <= 'z')
        || symbols.find(c) != std::string::npos;
}

/*
函数：SessionCookie::isValidCookieName
用途：验证 Cookie 名称非空、长度受限且全部是 token 字符。
参数：name 来自请求 header 或 buildSetCookie() 调用方。
变量：i 是名称字符下标。
实现逻辑：长度必须在 1..128；任一非法字符都返回 false。
*/
bool SessionCookie::isValidCookieName(const std::string &name)
{
    if (name.empty() || name.size() > MAX_COOKIE_NAME_LENGTH)
        return false;
    size_t i = 0;
    while (i < name.size())
    {
        if (!isTokenChar(name[i]))
            return false;
        ++i;
    }
    return true;
}

/*
函数：SessionCookie::isValidCookieValue
用途：验证未加引号 Cookie value 的长度和 cookie-octet 字符范围。
参数：value 来自请求 Cookie 或待生成的 Session ID。
变量：i 是字节下标；c 是当前无符号字节；allowed 表示是否属于安全范围。
实现逻辑：允许空值和安全可见 ASCII；拒绝空格、控制字符、双引号、逗号、分号和反斜杠。
*/
bool SessionCookie::isValidCookieValue(const std::string &value)
{
    if (value.size() > MAX_COOKIE_VALUE_LENGTH)
        return false;
    size_t i = 0;
    while (i < value.size())
    {
        unsigned char c = static_cast<unsigned char>(value[i]);
        bool allowed = c == 0x21
            || (c >= 0x23 && c <= 0x2B)
            || (c >= 0x2D && c <= 0x3A)
            || (c >= 0x3C && c <= 0x5B)
            || (c >= 0x5D && c <= 0x7E);
        if (!allowed)
            return false;
        ++i;
    }
    return true;
}

/*
函数：SessionCookie::parseCookieHeader
用途：把客户端 Cookie header value 解析成 name 到 value 的唯一映射。
参数：headerValue 来自 Request；cookies 是调用方传入的输出 map。
变量：parsed 是临时结果；begin/semicolon/end 切分片段；pair/equal/name/value 解析单项；insertResult 检查重复。
实现逻辑：先清空输出；在临时 map 中完成全部验证；只有整体成功时 swap 到 cookies，任何失败输出保持为空。
*/
bool SessionCookie::parseCookieHeader(const std::string &headerValue,
                                      CookieMap &cookies)
{
    cookies.clear();
    if (headerValue.empty())
        return true;
    if (headerValue.size() > MAX_COOKIE_HEADER_LENGTH)
        return false;
    try
    {
        CookieMap parsed;
        size_t begin = 0;
        while (begin < headerValue.size())
        {
            size_t semicolon = headerValue.find(';', begin);
            size_t end = semicolon == std::string::npos
                ? headerValue.size() : semicolon;
            std::string pair = trimOws(headerValue.substr(begin,
                end - begin));
            if (pair.empty())
                return false;
            size_t equal = pair.find('=');
            if (equal == std::string::npos || equal == 0)
                return false;
            std::string name = pair.substr(0, equal);
            std::string value = pair.substr(equal + 1);
            if (!isValidCookieName(name) || !isValidCookieValue(value))
                return false;
            std::pair<CookieMap::iterator, bool> insertResult =
                parsed.insert(std::make_pair(name, value));
            if (!insertResult.second)
                return false;
            if (semicolon == std::string::npos)
                break;
            begin = semicolon + 1;
            if (begin == headerValue.size())
                return false;
        }
        cookies.swap(parsed);
        return true;
    }
    catch (...)
    {
        cookies.clear();
        return false;
    }
}

/*
函数：SessionCookie::getCookie
用途：从完整 Cookie header 中读取指定名称的值。
参数：headerValue/name 是输入；value 是调用方提供的输出变量。
变量：cookies 保存完整解析结果；it 定位目标名称。
实现逻辑：先清空输出并验证名称；解析失败或不存在返回 false；复制成功后返回 true，异常时重新清空。
*/
bool SessionCookie::getCookie(const std::string &headerValue,
                              const std::string &name,
                              std::string &value)
{
    value.clear();
    if (!isValidCookieName(name))
        return false;
    CookieMap cookies;
    if (!parseCookieHeader(headerValue, cookies))
        return false;
    CookieMap::const_iterator it = cookies.find(name);
    if (it == cookies.end())
        return false;
    try
    {
        value = it->second;
        return true;
    }
    catch (...)
    {
        value.clear();
        return false;
    }
}

/*
函数：SessionCookie::isValidPath
用途：验证 Set-Cookie Path 是受限长度的绝对路径，并且不会破坏 header。
参数：path 由 SessionDemo 或未来配置传入。
变量：i 是路径下标；c 是当前字节。
实现逻辑：要求非空、以 / 开头、长度不超过 1024，并拒绝分号、控制字符和 DEL。
*/
bool SessionCookie::isValidPath(const std::string &path)
{
    if (path.empty() || path[0] != '/'
        || path.size() > MAX_COOKIE_PATH_LENGTH)
        return false;
    size_t i = 0;
    while (i < path.size())
    {
        unsigned char c = static_cast<unsigned char>(path[i]);
        if (c < 32 || c == 127 || c == ';')
            return false;
        ++i;
    }
    return true;
}

/*
函数：SessionCookie::isValidSameSite
用途：验证 SameSite 属性，并保证 SameSite=None 只与 Secure 一起使用。
参数：sameSite/secure 来自 buildSetCookie() 调用方。
变量：无局部变量。
实现逻辑：允许空、Lax、Strict；None 仅在 secure=true 时允许。
*/
bool SessionCookie::isValidSameSite(const std::string &sameSite,
                                    bool secure)
{
    if (sameSite.empty() || sameSite == "Lax" || sameSite == "Strict")
        return true;
    if (sameSite == "None")
        return secure;
    return false;
}

/*
函数：SessionCookie::unsignedLongToString
用途：把 Max-Age 秒数转换成十进制文本。
参数：value 是 buildSetCookie() 接收的 unsigned long。
变量：output 是格式化流。
实现逻辑：把 value 写入流并返回字符串。
*/
std::string SessionCookie::unsignedLongToString(unsigned long value)
{
    std::ostringstream output;
    output << value;
    return output.str();
}

/*
函数：SessionCookie::buildSetCookie
用途：生成可直接作为 Set-Cookie header value 的安全字符串。
参数：name/value 是 Cookie pair；path/maxAge/httpOnly/sameSite/secure 是属性；headerValue 是输出。
变量：result 累积最终文本。
实现逻辑：先清空输出并验证全部字段；按稳定顺序拼接属性；最终长度不超过 8 KiB 时输出，异常返回 false。
*/
bool SessionCookie::buildSetCookie(const std::string &name,
                                   const std::string &value,
                                   const std::string &path,
                                   unsigned long maxAge,
                                   bool httpOnly,
                                   const std::string &sameSite,
                                   bool secure,
                                   std::string &headerValue)
{
    headerValue.clear();
    if (!isValidCookieName(name) || !isValidCookieValue(value)
        || !isValidPath(path) || !isValidSameSite(sameSite, secure))
        return false;
    try
    {
        std::string result = name + "=" + value;
        result += "; Path=" + path;
        result += "; Max-Age=" + unsignedLongToString(maxAge);
        if (httpOnly)
            result += "; HttpOnly";
        if (!sameSite.empty())
            result += "; SameSite=" + sameSite;
        if (secure)
            result += "; Secure";
        if (result.size() > MAX_SET_COOKIE_LENGTH)
            return false;
        headerValue = result;
        return true;
    }
    catch (...)
    {
        headerValue.clear();
        return false;
    }
}

/*
函数：SessionCookie::buildExpiredCookie
用途：生成让浏览器立即删除指定 Cookie 的 Set-Cookie value。
参数：name/path/属性与 buildSetCookie 相同；headerValue 是输出变量。
变量：base 保存带空值和 Max-Age=0 的基础文本。
实现逻辑：先生成基础 Cookie，再追加过去 Expires；检查最终长度后输出，异常或验证失败返回 false。
*/
bool SessionCookie::buildExpiredCookie(const std::string &name,
                                       const std::string &path,
                                       bool httpOnly,
                                       const std::string &sameSite,
                                       bool secure,
                                       std::string &headerValue)
{
    headerValue.clear();
    try
    {
        std::string base;
        if (!buildSetCookie(name, "", path, 0, httpOnly,
            sameSite, secure, base))
            return false;
        base += "; Expires=Thu, 01 Jan 1970 00:00:00 GMT";
        if (base.size() > MAX_SET_COOKIE_LENGTH)
            return false;
        headerValue = base;
        return true;
    }
    catch (...)
    {
        headerValue.clear();
        return false;
    }
}
