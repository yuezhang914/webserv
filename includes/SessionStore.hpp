#ifndef SESSIONSTORE_HPP
#define SESSIONSTORE_HPP

/*
文件：includes/SessionStore.hpp
用途：声明 Webserv Bonus 的内存 Session 存储类，负责创建、恢复、轮换、销毁、过期回收和键值读写。
模块边界：本文件不依赖 Request、Response、ServerManager 或 CGI；接入层只需要传入 Cookie 中的 Session ID。
所有权：SessionStore 独占内部记录，外部不能取得内部指针，避免绕过过期检查或产生悬空引用。
标准限制：接口保持 C++98，不依赖外部库或 Boost。
*/

/*
包含：<cstddef>
用途：使用 size_t 表示 Session 数量、字符串长度和容器容量。
*/
#include <cstddef>

/*
包含：<ctime>
用途：使用 std::time_t 表示访问时间和过期时间，并在调用方未传时间时读取当前时间。
*/
#include <ctime>

/*
包含：<map>
用途：使用 std::map 保存 Session ID 到记录，以及记录内部的键值数据。
*/
#include <map>

/*
包含：<string>
用途：使用 std::string 保存 Session ID、键和值，并支持二进制安全的 value。
*/
#include <string>

/*
类：SessionStore
用途：管理服务器进程内共享的 Session 数据，并保证每次访问都经过 ID 格式、过期时间和容量限制检查。
成员来源：_sessions 由本类创建和销毁；_timeoutSeconds 与 _maxSessions 来自构造函数参数。
使用方式：ServerManager 未来应持有一个长期存在的 SessionStore，而不是为每个客户端单独创建。
*/
class SessionStore
{
public:
    /*
    类型：ValueMap
    用途：表示单个 Session 中的业务键值集合。
    数据来源：key/value 由 SessionDemo 或未来业务处理函数通过 setValue() 写入。
    */
    typedef std::map<std::string, std::string> ValueMap;

    /*
    函数：SessionStore
    用途：创建空 Session 存储并设置滑动过期秒数和最大 Session 数量。
    参数：timeoutSeconds 和 maxSessions 都是调用方传入；0 会被修正为最小安全值，过大的超时会被限制。
    逻辑：初始化空 map，并保存经过边界保护的配置值。
    */
    explicit SessionStore(unsigned long timeoutSeconds = 1800,
                          size_t maxSessions = 10000);

    /*
    函数：createSession
    用途：创建新的空 Session，并通过 sessionId 输出随机 ID。
    参数：sessionId 是调用方提供的输出变量；now 是调用方传入的时间，0 表示读取当前时间。
    逻辑：清理过期记录、检查容量、生成唯一 ID、建立时间字段并插入 map；失败时清空输出。
    */
    bool createSession(std::string &sessionId, std::time_t now = 0);

    /*
    函数：resumeSession
    用途：确认客户端提供的 ID 是否仍有效，并刷新滑动过期时间。
    参数：sessionId 通常来自 Cookie；now 由请求处理层传入，0 表示读取当前时间。
    逻辑：验证 ID、查找记录、删除已过期记录，有效时刷新访问时间并返回 true。
    */
    bool resumeSession(const std::string &sessionId, std::time_t now = 0);

    /*
    函数：regenerateSession
    用途：保留原 Session 数据但更换 ID，用于登录后的 Session fixation 防护。
    参数：oldSessionId 来自旧 Cookie；newSessionId 是输出；now 是轮换时间。
    逻辑：找到有效旧记录，生成新 ID，复制数据并插入新记录，成功后删除旧记录。
    */
    bool regenerateSession(const std::string &oldSessionId,
                           std::string &newSessionId,
                           std::time_t now = 0);

    /*
    函数：destroySession
    用途：立即删除指定 Session，供退出登录或主动失效会话使用。
    参数：sessionId 通常来自 Cookie 或业务层保存的当前 ID。
    逻辑：先验证格式，再按 ID 从 map 删除；存在并删除一条时返回 true。
    */
    bool destroySession(const std::string &sessionId);

    /*
    函数：setValue
    用途：在有效 Session 中新增或覆盖一个键值，并刷新滑动过期时间。
    参数：sessionId 来自 Cookie；key/value 由业务层传入；now 是本次请求时间。
    逻辑：检查 key、value 和每会话键数量，再写入 map；失败不会写入新值。
    */
    bool setValue(const std::string &sessionId,
                  const std::string &key,
                  const std::string &value,
                  std::time_t now = 0);

    /*
    函数：getValue
    用途：从有效 Session 读取一个键值，并刷新滑动过期时间。
    参数：sessionId/key 是输入；value 是调用方提供的输出变量；now 是访问时间。
    逻辑：任何失败都会清空 value；找到会话和 key 后复制完整字符串并返回 true。
    */
    bool getValue(const std::string &sessionId,
                  const std::string &key,
                  std::string &value,
                  std::time_t now = 0);

    /*
    函数：removeValue
    用途：删除有效 Session 中的一个指定键。
    参数：sessionId/key 由业务层传入；now 是访问时间。
    逻辑：验证 key、找到有效 Session、刷新过期时间，再根据 erase 数量返回结果。
    */
    bool removeValue(const std::string &sessionId,
                     const std::string &key,
                     std::time_t now = 0);

    /*
    函数：clearValues
    用途：清空一个 Session 的全部业务值，但保留同一个 Session ID。
    参数：sessionId 来自 Cookie；now 是本次访问时间。
    逻辑：找到有效记录并刷新过期时间，随后清空内部 ValueMap。
    */
    bool clearValues(const std::string &sessionId,
                     std::time_t now = 0);

    /*
    函数：cleanupExpired
    用途：批量删除达到过期时间的 Session，防止服务器长期运行时无界占用内存。
    参数：now 是调用方传入的检查时间，0 表示读取当前时间。
    逻辑：解析时间后安全遍历 map，删除所有过期项并返回删除数量。
    */
    size_t cleanupExpired(std::time_t now = 0);

    /*
    函数：size
    用途：返回当前 map 中保存的记录数量。
    参数：无。
    逻辑：直接返回容器大小；若要只统计有效记录，调用方应先 cleanupExpired()。
    */
    size_t size() const;

    /*
    函数：getTimeoutSeconds
    用途：返回服务端 Session 的滑动过期秒数，供 Set-Cookie Max-Age 使用。
    参数：无。
    逻辑：直接返回构造时保存并限制后的值。
    */
    unsigned long getTimeoutSeconds() const;

    /*
    函数：getMaxSessions
    用途：返回 SessionStore 最大容量，供测试、日志或状态页读取。
    参数：无。
    逻辑：直接返回 _maxSessions。
    */
    size_t getMaxSessions() const;

    /*
    函数：isValidSessionId
    用途：验证客户端提供的 ID 是否符合本模块生成的固定格式。
    参数：sessionId 是传入的候选字符串。
    逻辑：要求长度为 64，并且全部为小写十六进制字符。
    */
    static bool isValidSessionId(const std::string &sessionId);

private:
    /*
    结构体：SessionRecord
    用途：保存单个 Session 的业务值和生命周期时间。
    成员来源：values 由 setValue() 管理；时间字段由 createSession()、refresh() 和 regenerateSession() 管理。
    */
    struct SessionRecord
    {
        ValueMap values;
        std::time_t createdAt;
        std::time_t lastAccessAt;
        std::time_t expiresAt;

        /*
        函数：SessionRecord
        用途：创建时间为 0、业务值为空的临时记录。
        参数：无。
        逻辑：初始化所有成员，真实时间随后由 SessionStore 写入。
        */
        SessionRecord();
    };

    /*
    类型：SessionMap
    用途：保存 Session ID 到 SessionRecord 的唯一映射。
    数据来源：createSession() 和 regenerateSession() 插入，destroy/cleanup 删除。
    */
    typedef std::map<std::string, SessionRecord> SessionMap;

    SessionMap _sessions;
    unsigned long _timeoutSeconds;
    size_t _maxSessions;

    /*
    函数：SessionStore 复制构造
    用途：禁止复制共享存储，避免两个对象持有相互独立但看似相同的 Session 数据。
    参数：other 是潜在复制源。
    逻辑：只声明不实现，外部无法调用。
    */
    SessionStore(const SessionStore &other);

    /*
    函数：SessionStore 赋值运算符
    用途：禁止通过赋值复制 SessionStore。
    参数：other 是潜在赋值源。
    逻辑：只声明不实现，外部无法调用。
    */
    SessionStore &operator=(const SessionStore &other);

    /*
    函数：resolveNow
    用途：把可选时间参数转换为可用时间，并检测系统时间读取失败。
    参数：requested 是调用方传入值；resolved 是输出变量。
    逻辑：requested 非 0 时直接使用；否则调用 std::time，失败返回 false。
    */
    bool resolveNow(std::time_t requested, std::time_t &resolved) const;

    /*
    函数：computeExpiry
    用途：计算 now 加 timeout 的过期时间，并防止 std::time_t 加法溢出。
    参数：now 是已解析的访问时间；expiry 是输出变量。
    逻辑：接近 time_t 上限时把结果限制到最大值，否则执行安全加法。
    */
    void computeExpiry(std::time_t now, std::time_t &expiry) const;

    /*
    函数：isExpired
    用途：判断一条记录在给定时间是否已经失效。
    参数：record 来自 _sessions；now 是已解析时间。
    逻辑：now 达到或超过 expiresAt 时返回 true。
    */
    bool isExpired(const SessionRecord &record, std::time_t now) const;

    /*
    函数：refresh
    用途：记录有效访问时间并更新滑动过期截止时间。
    参数：record 是 map 中记录；now 是当前访问时间。
    逻辑：写 lastAccessAt，再通过 computeExpiry() 安全计算 expiresAt。
    */
    void refresh(SessionRecord &record, std::time_t now) const;

    /*
    函数：generateUniqueId
    用途：生成当前 store 中不存在的新 Session ID。
    参数：sessionId 是输出变量。
    逻辑：有限次数读取随机字节并编码为十六进制，找到未占用 ID 后返回 true。
    */
    bool generateUniqueId(std::string &sessionId) const;

    /*
    函数：fillRandomBytes
    用途：从系统随机源填满指定缓冲区。
    参数：buffer 指向调用方数组；length 是需要读取的字节数。
    逻辑：打开 /dev/urandom，循环 read 直到填满，并在所有分支关闭 fd。
    */
    static bool fillRandomBytes(unsigned char *buffer, size_t length);

    /*
    函数：bytesToHex
    用途：把随机二进制字节转换成 Cookie 安全的小写十六进制字符串。
    参数：buffer/length 来自 fillRandomBytes() 的结果。
    逻辑：每个字节追加两个十六进制字符。
    */
    static std::string bytesToHex(const unsigned char *buffer,
                                  size_t length);

    /*
    函数：isValidKey
    用途：验证 Session 业务 key 的长度和字符集。
    参数：key 由业务层传入。
    逻辑：只允许 1 到 128 个字母、数字、下划线、连字符和点。
    */
    static bool isValidKey(const std::string &key);

    /*
    函数：findActive
    用途：集中完成 ID 校验、查找、过期删除和可选续期。
    参数：sessionId/now 来自公开函数；refreshExpiry 表示成功时是否刷新。
    逻辑：非法、不存在或过期都返回 end；有效记录按参数刷新后返回迭代器。
    */
    SessionMap::iterator findActive(const std::string &sessionId,
                                    std::time_t now,
                                    bool refreshExpiry);

    /*
    函数：cleanupExpiredAt
    用途：使用已经解析好的时间删除过期记录，避免内部再次读取系统时间。
    参数：now 是 resolveNow() 已成功得到的时间。
    逻辑：C++98 安全遍历并删除，返回删除数量。
    */
    size_t cleanupExpiredAt(std::time_t now);
};

#endif
