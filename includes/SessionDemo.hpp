#ifndef SESSIONDEMO_HPP
#define SESSIONDEMO_HPP

/*
文件：includes/SessionDemo.hpp
用途：声明可用于 Webserv Bonus 展示的访问计数、登录和退出三个 Cookie/Session 示例。
模块边界：示例不直接修改 Request 或 Response；它接收 Cookie 文本并返回状态、HTML body 和 Set-Cookie value。
评分意义：Subject 要求支持 cookies、session management 并提供 simple examples，本类提供可接入路由的最小示例。
标准限制：接口保持 C++98，不依赖外部库或 Boost。
*/

/*
包含：SessionStore.hpp
用途：使用 SessionStore 保存和恢复示例所需的访问次数与用户名。
*/
#include "SessionStore.hpp"

/*
包含：<ctime>
用途：使用 std::time_t 接收请求处理层提供的当前时间。
*/
#include <ctime>

/*
包含：<string>
用途：使用 std::string 保存 Cookie、用户名、HTML body 和 Session ID。
*/
#include <string>

/*
结构体：SessionDemoResult
用途：保存一个示例处理完成后交给 Response 的状态、Cookie、body 和当前 Session ID。
成员来源：所有字段由 SessionDemo 的 buildXxxExample() 写入，调用方只读取。
失败默认：构造和 reset() 都使用 500、无 Cookie、空 body、空 ID，避免失败时返回旧数据。
*/
struct SessionDemoResult
{
    int statusCode;
    bool hasSetCookie;
    std::string setCookieValue;
    std::string body;
    std::string sessionId;

    /*
    函数：SessionDemoResult
    用途：创建失败安全的空结果。
    参数：无。
    逻辑：初始化为 500、无 Cookie、空 body 和空 Session ID。
    */
    SessionDemoResult();

    /*
    函数：reset
    用途：清除上一次调用留下的全部结果，允许对象复用。
    参数：无。
    逻辑：恢复构造函数相同的失败安全默认值。
    */
    void reset();
};

/*
类：SessionDemo
用途：组合 SessionCookie 与 SessionStore，生成浏览器可展示的简单 Bonus 行为。
成员来源：类没有成员变量，所有状态来自函数参数中的共享 SessionStore。
使用方式：未来路由处理层调用对应函数，再把 SessionDemoResult 写进 Response。
*/
class SessionDemo
{
public:
    /*
    函数：buildCounterExample
    用途：恢复或创建 Session，并把服务端 visits 计数加一。
    参数：cookieHeader 来自 Request；store 是共享存储；now 是请求时间；result 是输出。
    逻辑：解析 Cookie、恢复或新建会话、读写 visits、生成刷新 Cookie 和 200 HTML。
    */
    static bool buildCounterExample(const std::string &cookieHeader,
                                    SessionStore &store,
                                    std::time_t now,
                                    SessionDemoResult &result);

    /*
    函数：buildLoginExample
    用途：验证用户名、建立登录 Session，并在已有匿名会话时轮换 ID。
    参数：cookieHeader 来自 Request；userName 来自 POST 数据；store/now/result 与计数示例相同。
    逻辑：恢复或创建会话、必要时更换 ID、保存 user、生成 Cookie 和 HTML。
    */
    static bool buildLoginExample(const std::string &cookieHeader,
                                  const std::string &userName,
                                  SessionStore &store,
                                  std::time_t now,
                                  SessionDemoResult &result);

    /*
    函数：buildLogoutExample
    用途：删除服务端 Session，并生成浏览器删除 Cookie。
    参数：cookieHeader 来自 Request；store 是共享存储；result 是输出。
    逻辑：能解析到 ID 时尝试删除；无论旧记录是否存在，都返回幂等的删除 Cookie 和 200 页面。
    */
    static bool buildLogoutExample(const std::string &cookieHeader,
                                   SessionStore &store,
                                   SessionDemoResult &result);

    /*
    函数：sessionCookieName
    用途：向接入层公开示例使用的固定 Cookie 名称。
    参数：无。
    逻辑：返回 WEBSERV_SESSION 常量指针。
    */
    static const char *sessionCookieName();

private:
    /*
    函数：SessionDemo
    用途：禁止创建无状态示例工具对象。
    参数：无。
    逻辑：只声明不实现，调用方只能使用 static 函数。
    */
    SessionDemo();
};

#endif
