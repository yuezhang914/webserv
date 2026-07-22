/*
文件：srcs/Session/SessionDemo.cpp
用途：实现可在浏览器中展示的 Session 访问计数、登录和退出三个 Bonus 示例。
设计原则：示例层只组合 SessionCookie 与 SessionStore，不直接依赖 Request、Response、socket 或 CGI。
失败处理：新建或轮换后的 Session 如果后续步骤失败会被回收，结果对象恢复为失败安全默认值，避免孤儿记录和旧输出泄漏。
标准限制：实现保持 C++98，不依赖外部库或 Boost。
*/

/*
包含：SessionDemo.hpp
用途：取得 SessionDemo、SessionDemoResult 和 SessionStore 相关声明。
*/
#include "SessionDemo.hpp"

/*
包含：SessionCookie.hpp
用途：解析请求 Cookie，并生成刷新或删除用的 Set-Cookie value。
*/
#include "SessionCookie.hpp"

/*
包含：<limits>
用途：使用 std::numeric_limits<unsigned long>::max() 防止访问计数递增溢出。
*/
#include <limits>

/*
包含：<sstream>
用途：使用 std::ostringstream 把访问次数转换成字符串。
*/
#include <sstream>

/*
常量：SESSION_COOKIE_NAME
用途：统一三个示例使用的浏览器 Cookie 名称，避免不同函数写出不一致名称。
*/
static const char *SESSION_COOKIE_NAME = "WEBSERV_SESSION";

/*
函数：escapeHtml
用途：转义示例 HTML 中的用户可控文本，避免用户名形成标签或实体边界。
参数：text 来自 buildLoginExample() 的 userName。
变量：output 累积安全文本；i 是字符下标。
实现逻辑：替换 &、<、>、双引号和单引号，其余字节原样追加。
*/
static std::string escapeHtml(const std::string &text)
{
    std::string output;
    size_t i = 0;
    while (i < text.size())
    {
        if (text[i] == '&') output += "&amp;";
        else if (text[i] == '<') output += "&lt;";
        else if (text[i] == '>') output += "&gt;";
        else if (text[i] == '"') output += "&quot;";
        else if (text[i] == '\'') output += "&#39;";
        else output += text[i];
        ++i;
    }
    return output;
}

/*
函数：isValidUserName
用途：限制登录示例用户名为空、过长或含控制字符的输入。
参数：userName 由未来 POST 表单解析结果或测试传入。
变量：i 是字节下标；c 是当前无符号字节。
实现逻辑：长度必须为 1..64，拒绝 ASCII 控制字符和 DEL。
*/
static bool isValidUserName(const std::string &userName)
{
    if (userName.empty() || userName.size() > 64)
        return false;
    size_t i = 0;
    while (i < userName.size())
    {
        unsigned char c = static_cast<unsigned char>(userName[i]);
        if (c < 32 || c == 127)
            return false;
        ++i;
    }
    return true;
}

/*
函数：parseUnsignedLong
用途：把 Session 中保存的 visits 文本安全转换成 unsigned long。
参数：value 是 SessionStore 中的字符串；result 是调用方提供的输出变量。
变量：i 是字符下标；digit 是当前数字；maxValue 是 unsigned long 最大值。
实现逻辑：空值或非数字失败；每次乘十加数字前检查是否溢出。
*/
static bool parseUnsignedLong(const std::string &value,
                              unsigned long &result)
{
    result = 0;
    if (value.empty())
        return false;
    const unsigned long maxValue =
        std::numeric_limits<unsigned long>::max();
    size_t i = 0;
    while (i < value.size())
    {
        if (value[i] < '0' || value[i] > '9')
            return false;
        unsigned long digit = static_cast<unsigned long>(value[i] - '0');
        if (result > (maxValue - digit) / 10)
            return false;
        result = result * 10 + digit;
        ++i;
    }
    return true;
}

/*
函数：unsignedLongToString
用途：把访问次数转换成 Session value 和 HTML 文本。
参数：value 是 counter 示例计算出的数字。
变量：output 是格式化流。
实现逻辑：写入数字并返回 output.str()。
*/
static std::string unsignedLongToString(unsigned long value)
{
    std::ostringstream output;
    output << value;
    return output.str();
}

/*
函数：resolveOrCreateSession
用途：从 Cookie 恢复有效 Session；缺失、畸形、未知或过期时创建新 Session。
参数：cookieHeader 来自 Request；store 是共享存储；now 是请求时间；sessionId/created 是输出。
变量：candidate 保存 Cookie 中解析出的候选 ID。
实现逻辑：Cookie 存在且 resumeSession 成功时复用；否则调用 createSession 并标记 created=true。
*/
static bool resolveOrCreateSession(const std::string &cookieHeader,
                                   SessionStore &store,
                                   std::time_t now,
                                   std::string &sessionId,
                                   bool &created)
{
    sessionId.clear();
    created = false;
    std::string candidate;
    if (SessionCookie::getCookie(cookieHeader,
        SESSION_COOKIE_NAME, candidate)
        && store.resumeSession(candidate, now))
    {
        sessionId = candidate;
        return true;
    }
    if (!store.createSession(sessionId, now))
        return false;
    created = true;
    return true;
}

/*
函数：setRefreshCookie
用途：为有效 Session 生成带 Path、Max-Age、HttpOnly 和 SameSite=Lax 的 Cookie。
参数：sessionId 来自 SessionStore；store 提供超时秒数；result 是待写入输出对象。
变量：无额外局部变量。
实现逻辑：调用 SessionCookie::buildSetCookie；成功后设置 hasSetCookie=true。
*/
static bool setRefreshCookie(const std::string &sessionId,
                             const SessionStore &store,
                             SessionDemoResult &result)
{
    if (!SessionCookie::buildSetCookie(SESSION_COOKIE_NAME,
        sessionId, "/", store.getTimeoutSeconds(), true,
        "Lax", false, result.setCookieValue))
        return false;
    result.hasSetCookie = true;
    return true;
}

/*
函数：cleanupFailedExample
用途：在示例中途失败时按需销毁新建或轮换后的 Session，并清空返回结果。
参数：store 是共享存储；sessionId 是可能需要销毁的 ID；destroySession 表示该 ID 是否由本次调用新建；result 是输出对象。
变量：无局部变量。
实现逻辑：需要回滚且 ID 非空时尝试 destroySession，随后调用 result.reset() 并返回 false。
*/
static bool cleanupFailedExample(SessionStore &store,
                                 const std::string &sessionId,
                                 bool destroySession,
                                 SessionDemoResult &result)
{
    if (destroySession && !sessionId.empty())
        store.destroySession(sessionId);
    result.reset();
    return false;
}

/*
函数：SessionDemoResult::SessionDemoResult
用途：创建状态明确的空示例结果。
参数：无；由测试或未来接入代码创建。
变量：成员在初始化列表中设置。
实现逻辑：使用 500、无 Cookie、空 body 和空 ID 作为失败安全默认值。
*/
SessionDemoResult::SessionDemoResult()
    : statusCode(500), hasSetCookie(false), setCookieValue(), body(),
      sessionId()
{
}

/*
函数：SessionDemoResult::reset
用途：清除上一次调用留下的状态，允许同一对象安全复用。
参数：无。
变量：无局部变量。
实现逻辑：恢复构造函数相同的失败安全默认值。
*/
void SessionDemoResult::reset()
{
    statusCode = 500;
    hasSetCookie = false;
    setCookieValue.clear();
    body.clear();
    sessionId.clear();
}

/*
函数：SessionDemo::sessionCookieName
用途：向接入层公开本示例使用的 Cookie 名称。
参数：无。
变量：无局部变量。
实现逻辑：返回固定字符串 WEBSERV_SESSION。
*/
const char *SessionDemo::sessionCookieName()
{
    return SESSION_COOKIE_NAME;
}

/*
函数：SessionDemo::buildCounterExample
用途：实现最小 Session 演示：同一 Cookie 每次访问使服务端 visits 加一。
参数：cookieHeader 来自 Request；store 是服务器共享 SessionStore；now 是请求时间；result 是输出。
变量：created 表示是否新建；storedVisits/visits 保存计数；maxValue 防止递增溢出。
实现逻辑：恢复或创建会话、读取并递增 visits、先生成 Cookie 再写值、生成带登录/退出表单的 200 HTML；失败时回收新会话并重置结果。
*/
bool SessionDemo::buildCounterExample(const std::string &cookieHeader,
                                      SessionStore &store,
                                      std::time_t now,
                                      SessionDemoResult &result)
{
    result.reset();
    bool created = false;
    try
    {
        if (!resolveOrCreateSession(cookieHeader, store, now,
            result.sessionId, created))
            return false;
        std::string storedVisits;
        unsigned long visits = 0;
        if (store.getValue(result.sessionId, "visits", storedVisits, now)
            && !parseUnsignedLong(storedVisits, visits))
            visits = 0;
        const unsigned long maxValue =
            std::numeric_limits<unsigned long>::max();
        if (visits == maxValue)
            visits = 0;
        ++visits;
        if (!setRefreshCookie(result.sessionId, store, result)
            || !store.setValue(result.sessionId, "visits",
                unsignedLongToString(visits), now))
            return cleanupFailedExample(store, result.sessionId,
                created, result);
        result.statusCode = 200;
        result.body = "<!DOCTYPE html><html><head><title>Session Counter"
            "</title></head><body><h1>Cookie and Session Demo</h1>"
            "<p>Visits: ";
        result.body += unsignedLongToString(visits);
        result.body += "</p><form method=\"post\" action=\"/session/login\">"
            "<label>User <input name=\"user\" maxlength=\"64\"></label>"
            "<button type=\"submit\">Login</button></form>"
            "<form method=\"post\" action=\"/session/logout\">"
            "<button type=\"submit\">Logout</button></form>"
            "</body></html>";
        return true;
    }
    catch (...)
    {
        return cleanupFailedExample(store, result.sessionId,
            created, result);
    }
}

/*
函数：SessionDemo::buildLoginExample
用途：实现登录示例：保存用户名，并在匿名会话提升为登录会话时轮换 Session ID。
参数：cookieHeader 来自 Request；userName 来自 POST 数据；store 是共享存储；now 是请求时间；result 是输出。
变量：created 表示是否新建；ownsCurrentSession 表示失败时是否应销毁当前 ID；rotatedId 保存轮换结果。
实现逻辑：验证用户名、恢复或创建会话、已有会话轮换 ID、生成 Cookie、保存 user、生成转义 HTML；后续失败时销毁本次新建 ID。
*/
bool SessionDemo::buildLoginExample(const std::string &cookieHeader,
                                    const std::string &userName,
                                    SessionStore &store,
                                    std::time_t now,
                                    SessionDemoResult &result)
{
    result.reset();
    if (!isValidUserName(userName))
    {
        try
        {
            result.statusCode = 400;
            result.body = "Invalid user name";
            return true;
        }
        catch (...)
        {
            result.reset();
            return false;
        }
    }

    bool created = false;
    bool ownsCurrentSession = false;
    try
    {
        if (!resolveOrCreateSession(cookieHeader, store, now,
            result.sessionId, created))
            return false;
        ownsCurrentSession = created;
        if (!created)
        {
            std::string rotatedId;
            if (!store.regenerateSession(result.sessionId, rotatedId, now))
            {
                result.reset();
                return false;
            }
            result.sessionId = rotatedId;
            ownsCurrentSession = true;
        }
        if (!setRefreshCookie(result.sessionId, store, result)
            || !store.setValue(result.sessionId, "user", userName, now))
            return cleanupFailedExample(store, result.sessionId,
                ownsCurrentSession, result);
        result.statusCode = 200;
        result.body = "<!DOCTYPE html><html><head><title>Session Login"
            "</title></head><body><h1>Logged in</h1><p>User: ";
        result.body += escapeHtml(userName);
        result.body += "</p><p><a href=\"/session/counter\">"
            "Back to counter</a></p><form method=\"post\" "
            "action=\"/session/logout\"><button type=\"submit\">"
            "Logout</button></form></body></html>";
        return true;
    }
    catch (...)
    {
        return cleanupFailedExample(store, result.sessionId,
            ownsCurrentSession, result);
    }
}

/*
函数：SessionDemo::buildLogoutExample
用途：实现退出示例：删除服务端 Session，并返回浏览器删除 Cookie。
参数：cookieHeader 来自 Request；store 是共享 SessionStore；result 是输出。
变量：sessionId 保存 Cookie 中的 ID；expiredCookie/body 在删除服务端状态前先完整构造。
实现逻辑：先准备删除 Cookie 和 200 HTML；准备成功后解析并删除服务端记录，再整体写入 result，重复退出保持幂等。
*/
bool SessionDemo::buildLogoutExample(const std::string &cookieHeader,
                                     SessionStore &store,
                                     SessionDemoResult &result)
{
    result.reset();
    try
    {
        std::string expiredCookie;
        if (!SessionCookie::buildExpiredCookie(SESSION_COOKIE_NAME,
            "/", true, "Lax", false, expiredCookie))
            return false;
        std::string body = "<!DOCTYPE html><html><head><title>Session Logout"
            "</title></head><body><h1>Logged out</h1><p><a "
            "href=\"/session/counter\">Start a new session</a></p>"
            "</body></html>";
        result.statusCode = 200;
        result.hasSetCookie = true;
        result.setCookieValue = expiredCookie;
        result.body = body;
        std::string sessionId;
        if (SessionCookie::getCookie(cookieHeader,
            SESSION_COOKIE_NAME, sessionId))
            store.destroySession(sessionId);
        return true;
    }
    catch (...)
    {
        result.reset();
        return false;
    }
}
