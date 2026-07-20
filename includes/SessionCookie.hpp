#ifndef SESSIONCOOKIE_HPP
#define SESSIONCOOKIE_HPP

/*
文件：includes/SessionCookie.hpp
用途：声明 Cookie request header 解析和 Set-Cookie value 生成工具，为 Session ID 在浏览器与服务器之间传递提供格式支持。
模块边界：本类只处理文本，不保存 Session 数据，也不直接依赖 Request 或 Response。
安全边界：名称、值、Path、SameSite 和最终 header 长度都会验证，避免控制字符、分隔符和 header 注入。
标准限制：接口保持 C++98，不依赖外部库或 Boost。
*/

/*
包含：<map>
用途：使用 std::map 保存解析后的唯一 Cookie 名和值。
*/
#include <map>

/*
包含：<string>
用途：使用 std::string 接收 Cookie header、属性和输出结果。
*/
#include <string>

/*
类：SessionCookie
用途：提供无状态的 Cookie 文本解析和生成函数。
成员来源：类没有成员变量，所有输入都通过函数参数传入。
使用方式：Request 提供 Cookie header，Response 把 buildSetCookie() 输出写入 Set-Cookie。
*/
class SessionCookie
{
public:
    /*
    类型：CookieMap
    用途：表示客户端 Cookie header 中的 name 到 value 映射。
    数据来源：parseCookieHeader() 解析成功后整体写入。
    */
    typedef std::map<std::string, std::string> CookieMap;

    /*
    函数：parseCookieHeader
    用途：把完整 Cookie header value 解析成唯一名称映射。
    参数：headerValue 来自 Request；cookies 是调用方提供的输出 map。
    逻辑：在临时 map 中完成全部解析，只有整体成功时才替换输出；失败时输出为空。
    */
    static bool parseCookieHeader(const std::string &headerValue,
                                  CookieMap &cookies);

    /*
    函数：getCookie
    用途：从完整 Cookie header 中读取指定名称的值。
    参数：headerValue/name 是输入；value 是输出变量。
    逻辑：先验证目标名称并解析全部 header，找到后复制值；失败清空 value。
    */
    static bool getCookie(const std::string &headerValue,
                          const std::string &name,
                          std::string &value);

    /*
    函数：buildSetCookie
    用途：生成可直接交给 Response::setHeader("Set-Cookie", value) 的 header value。
    参数：name/value 是 Cookie pair；path/maxAge/httpOnly/sameSite/secure 是属性；headerValue 是输出。
    逻辑：验证所有字段，按稳定顺序拼接属性，并检查最终长度后输出。
    */
    static bool buildSetCookie(const std::string &name,
                               const std::string &value,
                               const std::string &path,
                               unsigned long maxAge,
                               bool httpOnly,
                               const std::string &sameSite,
                               bool secure,
                               std::string &headerValue);

    /*
    函数：buildExpiredCookie
    用途：生成让浏览器立即删除指定 Cookie 的 Set-Cookie value。
    参数：name/path/属性与 buildSetCookie 相同；headerValue 是输出。
    逻辑：生成空值、Max-Age=0，再追加过去的 Expires 日期。
    */
    static bool buildExpiredCookie(const std::string &name,
                                   const std::string &path,
                                   bool httpOnly,
                                   const std::string &sameSite,
                                   bool secure,
                                   std::string &headerValue);

    /*
    函数：isValidCookieName
    用途：验证 Cookie 名称是否是受限长度的 HTTP token。
    参数：name 是调用方传入的候选名称。
    逻辑：要求非空、不过长，并且每个字符都是 token 字符。
    */
    static bool isValidCookieName(const std::string &name);

    /*
    函数：isValidCookieValue
    用途：验证未加引号 Cookie value 的 cookie-octet 字符范围。
    参数：value 是请求中解析出的值或待生成值。
    逻辑：允许空值和安全可见 ASCII，拒绝空格、控制字符、逗号、分号、双引号和反斜杠。
    */
    static bool isValidCookieValue(const std::string &value);

private:
    /*
    函数：SessionCookie
    用途：禁止创建无状态工具对象。
    参数：无。
    逻辑：只声明不实现，调用方只能使用 static 函数。
    */
    SessionCookie();

    /*
    函数：trimOws
    用途：删除 Cookie pair 两端的空格和 tab。
    参数：value 是按分号切出的片段。
    逻辑：计算首尾有效位置并返回中间子串。
    */
    static std::string trimOws(const std::string &value);

    /*
    函数：isTokenChar
    用途：判断单个字符能否出现在 Cookie 名称中。
    参数：c 来自名称遍历。
    逻辑：字母、数字和 HTTP token 特殊字符返回 true。
    */
    static bool isTokenChar(char c);

    /*
    函数：isValidPath
    用途：验证 Set-Cookie Path 是绝对路径且不会破坏 header。
    参数：path 由示例或未来配置传入。
    逻辑：要求以 / 开头、长度受限，并拒绝分号、控制字符和 DEL。
    */
    static bool isValidPath(const std::string &path);

    /*
    函数：isValidSameSite
    用途：验证 SameSite 属性及 None 与 Secure 的组合。
    参数：sameSite/secure 来自 buildSetCookie() 调用方。
    逻辑：允许空、Lax、Strict；None 只有在 secure=true 时允许。
    */
    static bool isValidSameSite(const std::string &sameSite, bool secure);

    /*
    函数：unsignedLongToString
    用途：把 Max-Age 秒数转为十进制文本。
    参数：value 是调用方传入的 unsigned long。
    逻辑：通过 ostringstream 格式化并返回字符串。
    */
    static std::string unsignedLongToString(unsigned long value);
};

#endif
