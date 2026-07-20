/*
文件：tests/module_tests/session_module_test.cpp
用途：独立验证 Webserv Bonus 的 SessionCookie、SessionStore 和 SessionDemo，不依赖 Request、Response、ServerManager、socket 或 CGI。
测试范围：Cookie 原子解析与生成、随机 ID、时间边界、滑动过期、容量限制、ID 轮换、失败清理，以及 counter/login/logout 示例。
测试原则：除“默认当前时间”场景外都显式传入固定时间；只通过公开接口操作，不访问私有成员。
注释规则：每个测试和辅助函数顶部都说明用途、参数来源、变量含义和实现步骤。
*/

/*
包含：SessionCookie.hpp
用途：调用 Cookie header 解析、单值读取、Set-Cookie 和删除 Cookie 生成接口。
*/
#include "SessionCookie.hpp"

/*
包含：SessionDemo.hpp
用途：调用 counter、login、logout 三个 Subject Bonus 简单示例。
*/
#include "SessionDemo.hpp"

/*
包含：SessionStore.hpp
用途：创建被测 SessionStore，并验证完整生命周期和限制。
*/
#include "SessionStore.hpp"

/*
包含：<iostream>
用途：使用 std::cout 和 std::endl 输出测试组、PASS/FAIL 与汇总。
*/
#include <iostream>

/*
包含：<limits>
用途：取得 time_t 与 unsigned long 最大值，测试时间和计数溢出边界。
*/
#include <limits>

/*
包含：<set>
用途：使用 std::set 检查批量生成的 Session ID 没有重复。
*/
#include <set>

/*
包含：<string>
用途：构造测试输入、保存 Cookie、Session ID、二进制 value 和输出 body。
*/
#include <string>

/*
全局统计：g_total/g_failed
用途：保存当前测试程序执行的断言总数和失败数量。
数据来源：只由 check() 修改，main() 最后读取并决定退出码。
*/
static int g_total = 0;
static int g_failed = 0;

/*
函数：beginGroup
用途：为一组相关断言打印清晰标题。
参数：name 由各 testXxx() 函数直接传入，是输入参数。
变量：无局部变量。
实现逻辑：输出空行、分隔线和组名，不改变断言统计。
*/
static void beginGroup(const std::string &name)
{
    std::cout << "\n========== " << name << " ==========" << std::endl;
}

/*
函数：check
用途：记录并打印一条断言结果。
参数：condition 是测试刚计算出的布尔值；name 是断言说明，二者都是调用方传入。
变量：使用全局 g_total/g_failed，不创建局部统计。
实现逻辑：总数加一；condition 为 false 时打印 FAIL 并增加失败数，否则打印 PASS。
*/
static void check(bool condition, const std::string &name)
{
    ++g_total;
    if (condition)
        std::cout << "[PASS] " << name << std::endl;
    else
    {
        std::cout << "[FAIL] " << name << std::endl;
        ++g_failed;
    }
}

/*
函数：extractCookiePair
用途：从 Set-Cookie value 提取浏览器下一次请求应发送的第一段 name=value。
参数：setCookieValue 来自 SessionDemoResult 或 SessionCookie::buildSetCookie()。
变量：semicolon 是第一个属性分隔符位置。
实现逻辑：不存在分号时返回全部，存在时返回分号前子串。
*/
static std::string extractCookiePair(const std::string &setCookieValue)
{
    size_t semicolon = setCookieValue.find(';');
    if (semicolon == std::string::npos)
        return setCookieValue;
    return setCookieValue.substr(0, semicolon);
}

/*
函数：extractCookieValue
用途：从 name=value Cookie pair 中提取 value，供测试恢复同一 Session。
参数：cookiePair 来自 extractCookiePair()。
变量：equal 是名称与值的分隔符位置。
实现逻辑：没有等号返回空字符串，否则返回等号后的全部内容。
*/
static std::string extractCookieValue(const std::string &cookiePair)
{
    size_t equal = cookiePair.find('=');
    if (equal == std::string::npos)
        return "";
    return cookiePair.substr(equal + 1);
}

/*
函数：testCookieParsing
用途：验证正常 Cookie 解析、单值查询、严格语法、长度限制和失败时输出原子性。
参数：无；所有 header 字符串在函数内部构造。
变量：cookies 是输出 map；value 是单 Cookie 输出；oversized 是超过限制的测试 header。
实现逻辑：覆盖空、多项、OWS、空 value、重复、注入、尾部分隔符、过长输入，并确认失败后 map/value 清空。
*/
static void testCookieParsing()
{
    beginGroup("Cookie request header 解析");
    SessionCookie::CookieMap cookies;
    std::string value;

    check(SessionCookie::parseCookieHeader("", cookies)
        && cookies.empty(), "空 Cookie header 表示没有 Cookie");
    check(SessionCookie::parseCookieHeader(
        "theme=dark; WEBSERV_SESSION=abc123; empty=", cookies),
        "解析多个 Cookie pair");
    check(cookies.size() == 3 && cookies["theme"] == "dark"
        && cookies["WEBSERV_SESSION"] == "abc123"
        && cookies["empty"].empty(), "Cookie map 内容正确");
    check(SessionCookie::getCookie(
        " a=1 ; WEBSERV_SESSION=xyz ", "WEBSERV_SESSION", value)
        && value == "xyz", "查找 Cookie 时处理 pair 两端 OWS");
    check(!SessionCookie::getCookie("a=1", "missing", value)
        && value.empty(), "不存在 Cookie 返回 false 并清空输出");
    check(!SessionCookie::parseCookieHeader("broken", cookies)
        && cookies.empty(), "缺少等号时失败并清空输出 map");
    check(!SessionCookie::parseCookieHeader("=value", cookies)
        && cookies.empty(), "空 Cookie name 被拒绝");
    check(!SessionCookie::parseCookieHeader("a=1; a=2", cookies)
        && cookies.empty(), "重复 Cookie name 被拒绝且无半份结果");
    check(!SessionCookie::parseCookieHeader("a=1; broken", cookies)
        && cookies.empty(), "后续 pair 失败时不保留前面已解析值");
    check(!SessionCookie::parseCookieHeader("a=one,two", cookies),
        "Cookie value 中逗号被拒绝");
    check(!SessionCookie::parseCookieHeader("a=one\\two", cookies),
        "Cookie value 中反斜杠被拒绝");
    check(!SessionCookie::parseCookieHeader("bad name=x", cookies),
        "含空格 Cookie name 被拒绝");
    check(!SessionCookie::parseCookieHeader("a=1;;b=2", cookies),
        "空 Cookie pair 被拒绝");
    check(!SessionCookie::parseCookieHeader("a=1;", cookies),
        "尾部空 Cookie pair 被拒绝");
    std::string oversized(8193, 'a');
    check(!SessionCookie::parseCookieHeader(oversized, cookies)
        && cookies.empty(), "超过 8 KiB 的 Cookie header 被拒绝");
}

/*
函数：testCookieGeneration
用途：验证 Set-Cookie 属性、字段限制、输出清空和删除 Cookie 文本格式。
参数：无；所有字段在函数内固定构造。
变量：headerValue 接收生成结果；longName/longValue/longPath 用于长度边界。
实现逻辑：先验证正常顺序，再覆盖非法字符、SameSite 约束、名称/值/Path 上限和过期 Cookie。
*/
static void testCookieGeneration()
{
    beginGroup("Set-Cookie 生成");
    std::string headerValue;

    check(SessionCookie::buildSetCookie("WEBSERV_SESSION", "abc123",
        "/", 1800, true, "Lax", false, headerValue),
        "生成普通 Session Cookie");
    check(headerValue == "WEBSERV_SESSION=abc123; Path=/; Max-Age=1800; "
        "HttpOnly; SameSite=Lax", "普通 Session Cookie 属性顺序稳定");
    check(!SessionCookie::buildSetCookie("Bad Name", "x", "/", 10,
        true, "Lax", false, headerValue) && headerValue.empty(),
        "非法 Cookie name 不生成且清空旧输出");
    check(!SessionCookie::buildSetCookie("SID", "x;injected", "/", 10,
        true, "Lax", false, headerValue),
        "非法 Cookie value 不生成 header");
    check(!SessionCookie::buildSetCookie("SID", "x", "relative", 10,
        true, "Lax", false, headerValue), "相对 Path 被拒绝");
    check(!SessionCookie::buildSetCookie("SID", "x", "/bad;path", 10,
        true, "Lax", false, headerValue), "含分号 Path 被拒绝");
    check(!SessionCookie::buildSetCookie("SID", "x", "/", 10,
        true, "None", false, headerValue),
        "SameSite=None 没有 Secure 时被拒绝");
    check(SessionCookie::buildSetCookie("SID", "x", "/", 10,
        true, "None", true, headerValue)
        && headerValue.find("SameSite=None") != std::string::npos
        && headerValue.find("; Secure") != std::string::npos,
        "SameSite=None 与 Secure 一起允许");
    std::string longName(129, 'a');
    std::string longValue(4097, 'x');
    std::string longPath = "/" + std::string(1024, 'p');
    check(!SessionCookie::buildSetCookie(longName, "x", "/", 10,
        true, "Lax", false, headerValue), "超过 128 字节 Cookie name 被拒绝");
    check(!SessionCookie::buildSetCookie("SID", longValue, "/", 10,
        true, "Lax", false, headerValue), "超过 4096 字节 Cookie value 被拒绝");
    check(!SessionCookie::buildSetCookie("SID", "x", longPath, 10,
        true, "Lax", false, headerValue), "超过 1024 字节 Path 被拒绝");
    check(SessionCookie::buildExpiredCookie("WEBSERV_SESSION", "/",
        true, "Lax", false, headerValue), "生成删除 Session Cookie");
    check(headerValue.find("WEBSERV_SESSION=") == 0
        && headerValue.find("Max-Age=0") != std::string::npos
        && headerValue.find("Expires=Thu, 01 Jan 1970") != std::string::npos,
        "删除 Cookie 同时包含 Max-Age=0 和过去 Expires");
}

/*
函数：testStoreConfiguration
用途：验证构造参数边界、超时上限和默认读取当前时间路径。
参数：无；函数内部创建多个不同配置的 store。
变量：normalized/limited/currentTimeStore 是被测对象；sessionId 保存默认时间创建结果。
实现逻辑：确认 0 参数修正为 1，超长超时限制为一年，并用 now=0 创建合法 Session。
*/
static void testStoreConfiguration()
{
    beginGroup("SessionStore 配置边界");
    SessionStore normalized(0, 0);
    SessionStore limited(static_cast<unsigned long>(-1), 5);
    SessionStore currentTimeStore(30, 5);
    std::string sessionId;

    check(normalized.getTimeoutSeconds() == 1
        && normalized.getMaxSessions() == 1,
        "0 超时和 0 容量被修正为最小安全值");
    check(limited.getTimeoutSeconds() == 31536000UL,
        "异常超长 Session 超时被限制为一年");
    check(currentTimeStore.createSession(sessionId)
        && SessionStore::isValidSessionId(sessionId),
        "调用方不传时间时可以读取当前时间创建 Session");
}

/*
函数：testSessionLifecycle
用途：验证创建、恢复、键值读写、二进制值、输出清空、删除、清空和销毁的完整生命周期。
参数：无；使用固定时间避免依赖真实时钟。
变量：store 是被测对象；sessionId/value 保存公开接口输出。
实现逻辑：创建后逐项读写，验证非法输入不改变数据，最后销毁并确认旧 ID 失效。
*/
static void testSessionLifecycle()
{
    beginGroup("Session 基本生命周期");
    SessionStore store(30, 100);
    std::string sessionId;
    std::string value;

    check(store.createSession(sessionId, 1000), "创建 Session 成功");
    check(SessionStore::isValidSessionId(sessionId),
        "Session ID 是 64 位小写十六进制");
    check(!SessionStore::isValidSessionId(std::string(64, 'A')),
        "大写十六进制 ID 被拒绝");
    check(store.size() == 1, "创建后 store 数量为 1");
    check(store.resumeSession(sessionId, 1001), "有效 Session 可以恢复");
    check(store.setValue(sessionId, "user", "Alice", 1002),
        "写入 Session value");
    check(store.getValue(sessionId, "user", value, 1003)
        && value == "Alice", "读取 Session value");
    check(store.setValue(sessionId, "user", "Bob", 1004)
        && store.getValue(sessionId, "user", value, 1005)
        && value == "Bob", "同 key 可以覆盖");
    check(store.setValue(sessionId, "binary", std::string("A\0B", 3), 1006)
        && store.getValue(sessionId, "binary", value, 1007)
        && value == std::string("A\0B", 3), "Session value 支持二进制 NUL");
    value = "old";
    check(!store.getValue(sessionId, "missing", value, 1008)
        && value.empty(), "读取不存在 key 时清空输出");
    check(!store.setValue(sessionId, "bad key", "x", 1008),
        "非法 Session key 被拒绝");
    check(!store.setValue(sessionId, "large", std::string(4097, 'x'), 1008),
        "超过 4096 字节的 Session value 被拒绝");
    check(store.removeValue(sessionId, "user", 1009)
        && !store.getValue(sessionId, "user", value, 1010),
        "删除指定 Session key");
    check(store.clearValues(sessionId, 1011)
        && !store.getValue(sessionId, "binary", value, 1012),
        "清空 Session 全部业务值");
    check(store.destroySession(sessionId), "销毁 Session 成功");
    check(store.size() == 0 && !store.resumeSession(sessionId, 1013),
        "销毁后旧 ID 立即失效");
    check(!store.destroySession(sessionId),
        "重复销毁不存在 Session 返回 false");
    check(!store.resumeSession("not-a-session-id", 1014),
        "非法格式 ID 不进入 store 查找");
}

/*
函数：testSlidingExpiration
用途：验证成功操作续期、失败操作不续期、批量清理和 time_t 上限保护。
参数：无；所有时间显式指定。
变量：sessionId/value 保存会话；first/second 是批量清理 ID；nearLimit 是接近 time_t 最大值的时间。
实现逻辑：先验证滑动续期，再证明缺失 key/非法写入不会延寿，最后检查接近最大时间时不发生溢出。
*/
static void testSlidingExpiration()
{
    beginGroup("Session 滑动过期与时间边界");
    SessionStore store(10, 10);
    std::string sessionId;
    std::string value;

    check(store.createSession(sessionId, 100), "创建 10 秒超时 Session");
    check(store.setValue(sessionId, "x", "1", 109),
        "成功写入刷新截止时间");
    check(store.getValue(sessionId, "x", value, 118) && value == "1",
        "成功读取再次刷新截止时间");
    check(!store.resumeSession(sessionId, 128),
        "当前时间等于 expiresAt 时 Session 过期");
    check(store.size() == 0, "访问过期 Session 时自动从 map 删除");

    check(store.createSession(sessionId, 200),
        "为失败读取不续期测试创建 Session");
    check(!store.getValue(sessionId, "missing", value, 209),
        "不存在 key 的读取返回 false");
    check(!store.resumeSession(sessionId, 210),
        "失败读取不会把原过期时间向后刷新");

    check(store.createSession(sessionId, 300),
        "为失败写入不续期测试创建 Session");
    check(!store.setValue(sessionId, "bad key", "x", 309),
        "非法写入返回 false");
    check(!store.resumeSession(sessionId, 310),
        "非法写入不会延长 Session 生命周期");

    std::string first;
    std::string second;
    check(store.createSession(first, 400)
        && store.createSession(second, 405),
        "创建两个不同过期时间的 Session");
    check(store.cleanupExpired(411) == 1 && store.size() == 1,
        "cleanupExpired 只删除已经过期记录");
    check(store.cleanupExpired(415) == 1 && store.size() == 0,
        "cleanupExpired 删除后续到期记录");

    SessionStore overflowSafe(10, 2);
    const std::time_t maxTime = std::numeric_limits<std::time_t>::max();
    const std::time_t nearLimit = maxTime - static_cast<std::time_t>(5);
    check(overflowSafe.createSession(sessionId, nearLimit),
        "接近 time_t 上限时仍能创建 Session");
    check(overflowSafe.resumeSession(sessionId, maxTime - 1),
        "过期时间溢出时被安全限制到 time_t 最大值");
    check(!overflowSafe.resumeSession(sessionId, maxTime),
        "达到限制后的最大过期时间时正确失效");
}

/*
函数：testRegeneration
用途：验证 ID 轮换保留数据、旧 ID 失效、输出清空和同一变量输入输出兼容。
参数：无；使用固定时间和 user 键。
变量：oldId/newId/value 保存轮换状态；aliasId 用于同一变量轮换场景。
实现逻辑：先普通轮换并检查数据，再验证失败清空输出，最后用同一 string 同时作为旧 ID 与新 ID。
*/
static void testRegeneration()
{
    beginGroup("Session ID 轮换");
    SessionStore store(60, 10);
    std::string oldId;
    std::string newId;
    std::string value;

    check(store.createSession(oldId, 1000)
        && store.setValue(oldId, "user", "Alice", 1001),
        "准备含数据的旧 Session");
    check(store.regenerateSession(oldId, newId, 1002),
        "重新生成 Session ID 成功");
    check(newId != oldId && SessionStore::isValidSessionId(newId),
        "新 ID 与旧 ID 不同且格式正确");
    check(!store.resumeSession(oldId, 1003), "轮换后旧 ID 立即失效");
    check(store.getValue(newId, "user", value, 1003)
        && value == "Alice", "轮换后业务数据被保留");
    check(store.size() == 1, "轮换不会增加 store 中会话数量");
    value = "old-output";
    check(!store.regenerateSession(oldId, value, 1004)
        && value.empty(), "不存在旧 ID 时失败并清空新 ID 输出");

    std::string aliasId;
    check(store.createSession(aliasId, 1100), "创建同变量轮换测试 Session");
    std::string beforeAlias = aliasId;
    check(store.regenerateSession(aliasId, aliasId, 1101)
        && aliasId != beforeAlias, "同一个 string 可同时作为旧 ID 输入和新 ID 输出");
}

/*
函数：testLimitsAndUniqueness
用途：验证最大 Session 数、过期回收、单 Session key 数量和批量随机 ID 唯一性。
参数：无；内部使用小容量和普通容量 store。
变量：first/second/third 是容量 ID；ids 是唯一集合；allUnique/firstSixtyFour 汇总循环结果。
实现逻辑：填满容量并验证拒绝，过期后恢复容量；生成 128 个 ID；填满 64 个 key 后只允许覆盖旧 key。
*/
static void testLimitsAndUniqueness()
{
    beginGroup("容量限制与随机唯一性");
    SessionStore limited(10, 2);
    std::string first;
    std::string second;
    std::string third;

    check(limited.createSession(first, 100)
        && limited.createSession(second, 100),
        "创建达到 maxSessions 的两条记录");
    third = "old-output";
    check(!limited.createSession(third, 100) && third.empty(),
        "达到 maxSessions 后拒绝并清空输出");
    check(limited.createSession(third, 111)
        && limited.size() == 1,
        "创建前自动回收过期记录并恢复容量");

    SessionStore store(60, 200);
    std::set<std::string> ids;
    bool allUnique = true;
    size_t i = 0;
    while (i < 128)
    {
        std::string id;
        if (!store.createSession(id, 1000)
            || !SessionStore::isValidSessionId(id)
            || !ids.insert(id).second)
            allUnique = false;
        ++i;
    }
    check(allUnique && ids.size() == 128,
        "连续生成 128 个 Session ID 均合法且不重复");

    std::string target = *ids.begin();
    bool firstSixtyFour = true;
    i = 0;
    while (i < 64)
    {
        std::string key = "key";
        key += static_cast<char>('A' + (i / 26));
        key += static_cast<char>('a' + (i % 26));
        if (!store.setValue(target, key, "x", 1001))
            firstSixtyFour = false;
        ++i;
    }
    check(firstSixtyFour, "单个 Session 可以保存上限内 64 个 key");
    check(!store.setValue(target, "overflow", "x", 1002),
        "第 65 个新 key 被容量限制拒绝");
    check(store.setValue(target, "keyAa", "updated", 1003),
        "达到 key 上限后仍可覆盖已有 key");
}

/*
函数：testCounterExample
用途：验证访问计数示例创建 Cookie、复用 Session、处理畸形 Cookie 和处理 store 满容量失败。
参数：无；第一次无 Cookie，第二次回传 Set-Cookie 第一段。
变量：store/result 保存示例状态；cookiePair/sessionId 提取浏览器回传内容；fullStore 验证失败清理。
实现逻辑：运行两次 counter 检查 visits，再传畸形 Cookie 创建新会话，最后验证容量不足时结果保持失败默认且无孤儿记录。
*/
static void testCounterExample()
{
    beginGroup("Bonus 示例：访问计数");
    SessionStore store(1800, 100);
    SessionDemoResult firstResult;
    SessionDemoResult secondResult;

    check(SessionDemo::buildCounterExample("", store, 1000, firstResult),
        "首次访问 counter 示例成功");
    check(firstResult.statusCode == 200
        && firstResult.body.find("Visits: 1") != std::string::npos,
        "首次访问显示 Visits: 1");
    check(firstResult.hasSetCookie
        && firstResult.setCookieValue.find("WEBSERV_SESSION=") == 0
        && firstResult.setCookieValue.find("HttpOnly") != std::string::npos
        && firstResult.setCookieValue.find("SameSite=Lax")
            != std::string::npos,
        "首次访问返回安全 Session Cookie");

    std::string cookiePair = extractCookiePair(firstResult.setCookieValue);
    std::string sessionId = extractCookieValue(cookiePair);
    check(sessionId == firstResult.sessionId
        && SessionStore::isValidSessionId(sessionId),
        "Set-Cookie 中 ID 与服务端 Session 一致");
    check(SessionDemo::buildCounterExample(
        "theme=dark; " + cookiePair, store, 1001, secondResult),
        "浏览器回传 Cookie 后再次访问成功");
    check(secondResult.sessionId == sessionId
        && secondResult.body.find("Visits: 2") != std::string::npos,
        "第二次访问复用同一 Session 并显示 Visits: 2");
    check(secondResult.hasSetCookie
        && extractCookieValue(extractCookiePair(secondResult.setCookieValue))
            == sessionId, "每次有效访问刷新 Cookie Max-Age");

    SessionDemoResult malformedResult;
    check(SessionDemo::buildCounterExample(
        "WEBSERV_SESSION=bad; broken", store, 1002, malformedResult),
        "畸形 Cookie 不使示例崩溃");
    check(malformedResult.sessionId != sessionId
        && malformedResult.body.find("Visits: 1") != std::string::npos,
        "畸形 Cookie 被当作新会话而非复用未知 ID");

    SessionStore fullStore(60, 1);
    std::string occupied;
    check(fullStore.createSession(occupied, 2000),
        "准备满容量 SessionStore");
    SessionDemoResult failedResult;
    failedResult.statusCode = 200;
    failedResult.hasSetCookie = true;
    failedResult.body = "old";
    check(!SessionDemo::buildCounterExample("", fullStore, 2001,
        failedResult), "满容量且无可回收记录时 counter 可控失败");
    check(failedResult.statusCode == 500 && !failedResult.hasSetCookie
        && failedResult.body.empty() && failedResult.sessionId.empty()
        && fullStore.size() == 1,
        "counter 失败时清空旧结果且不留下孤儿 Session");
}

/*
函数：testLoginAndLogoutExamples
用途：验证登录轮换 ID、用户名保存与 HTML 转义、非法用户名、容量失败、退出删除和重复退出幂等。
参数：无；先用 counter 创建匿名 Session，再把 Cookie 交给 login 和 logout。
变量：anonymous/login/logout 是阶段结果；oldId/user/cookiePair 用于验证；fullStore 验证创建失败。
实现逻辑：登录后检查旧 ID 失效和新 Cookie；测试非法输入与满容量；最后退出并确认服务端和浏览器状态都清除。
*/
static void testLoginAndLogoutExamples()
{
    beginGroup("Bonus 示例：登录与退出");
    SessionStore store(1800, 100);
    SessionDemoResult anonymous;
    SessionDemoResult login;
    SessionDemoResult logout;
    std::string user;

    check(SessionDemo::buildCounterExample("", store, 2000, anonymous),
        "创建登录前匿名 Session");
    std::string anonymousCookie = extractCookiePair(anonymous.setCookieValue);
    std::string oldId = anonymous.sessionId;
    check(SessionDemo::buildLoginExample(anonymousCookie,
        "<Alice & Bob>", store, 2001, login), "已有 Session 登录成功");
    check(login.statusCode == 200 && login.sessionId != oldId,
        "登录后轮换 Session ID 防止 fixation");
    check(!store.resumeSession(oldId, 2002), "登录后匿名旧 ID 已失效");
    check(store.getValue(login.sessionId, "user", user, 2002)
        && user == "<Alice & Bob>", "登录用户名保存在服务端 Session");
    check(login.body.find("&lt;Alice &amp; Bob&gt;")
        != std::string::npos, "登录页面对用户名执行 HTML 转义");
    check(extractCookieValue(extractCookiePair(login.setCookieValue))
        == login.sessionId, "登录响应把轮换后的新 ID 写入 Cookie");

    SessionDemoResult invalidLogin;
    check(SessionDemo::buildLoginExample("", "", store, 2003,
        invalidLogin) && invalidLogin.statusCode == 400
        && !invalidLogin.hasSetCookie,
        "空用户名返回可控 400 且不创建 Cookie");
    check(SessionDemo::buildLoginExample("", std::string(65, 'a'),
        store, 2003, invalidLogin) && invalidLogin.statusCode == 400,
        "超过 64 字节用户名返回 400");

    SessionStore fullStore(60, 1);
    std::string occupied;
    check(fullStore.createSession(occupied, 3000),
        "准备登录满容量 store");
    SessionDemoResult capacityLogin;
    check(!SessionDemo::buildLoginExample("", "Alice", fullStore,
        3001, capacityLogin), "无 Cookie 登录在满容量时可控失败");
    check(capacityLogin.statusCode == 500
        && capacityLogin.sessionId.empty() && fullStore.size() == 1,
        "登录创建失败不留下半成品结果或额外记录");

    std::string loginCookie = extractCookiePair(login.setCookieValue);
    check(SessionDemo::buildLogoutExample(loginCookie, store, logout),
        "退出示例成功");
    check(logout.statusCode == 200 && logout.hasSetCookie
        && logout.setCookieValue.find("Max-Age=0") != std::string::npos,
        "退出响应返回删除 Cookie");
    check(!store.resumeSession(login.sessionId, 2004),
        "退出后服务端 Session 已删除");

    SessionDemoResult repeatedLogout;
    check(SessionDemo::buildLogoutExample(loginCookie, store,
        repeatedLogout) && repeatedLogout.statusCode == 200,
        "重复退出保持幂等并继续清除浏览器 Cookie");
    check(SessionDemo::buildLogoutExample("broken", store,
        repeatedLogout) && repeatedLogout.hasSetCookie,
        "畸形 Cookie 退出仍返回浏览器删除 Cookie");
}

/*
函数：main
用途：依次运行全部 Session Bonus 模块测试并返回 shell 可识别状态。
参数：无命令行参数。
变量：使用全局 g_total/g_failed 输出汇总。
实现逻辑：按 Cookie、配置、Session 核心和三个示例顺序运行；失败数为 0 返回 0，否则返回 1。
*/
int main()
{
    testCookieParsing();
    testCookieGeneration();
    testStoreConfiguration();
    testSessionLifecycle();
    testSlidingExpiration();
    testRegeneration();
    testLimitsAndUniqueness();
    testCounterExample();
    testLoginAndLogoutExamples();

    std::cout << "\n========== Session Bonus 测试汇总 =========="
              << std::endl;
    std::cout << "总断言：" << g_total << std::endl;
    std::cout << "通过：" << (g_total - g_failed) << std::endl;
    std::cout << "失败：" << g_failed << std::endl;
    return g_failed == 0 ? 0 : 1;
}
