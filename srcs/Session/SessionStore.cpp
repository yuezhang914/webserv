/*
文件：srcs/Session/SessionStore.cpp
用途：实现内存 SessionStore，包括随机 ID、生命周期、滑动过期、容量限制、键值操作、ID 轮换和过期回收。
设计原则：外部不能取得内部记录指针；每次操作都经过 ID 格式和过期检查；失败时尽量保持原数据与输出状态明确。
随机来源：读取 /dev/urandom 的 32 字节并编码成 64 位小写十六进制；随机源失败时拒绝创建，不退化为时间戳 ID。
标准限制：实现保持 C++98，只使用标准库以及 Subject 允许的 open、read 和 close。
*/

/*
包含：SessionStore.hpp
用途：取得 SessionStore、SessionRecord、ValueMap 和所有成员函数声明。
*/
#include "SessionStore.hpp"

/*
包含：<fcntl.h>
用途：使用 O_RDONLY 和 open() 打开系统随机源。
*/
#include <fcntl.h>

/*
包含：<limits>
用途：使用 std::numeric_limits<std::time_t>::max() 防止过期时间加法溢出。
*/
#include <limits>

/*
包含：<unistd.h>
用途：使用 read() 填充随机缓冲区，并使用 close() 释放文件描述符。
*/
#include <unistd.h>

/*
包含：<utility>
用途：使用 std::make_pair 构造 map 插入项。
*/
#include <utility>

/*
常量组：SessionStore 文件私有限制
用途：固定 ID 长度、key/value 容量、生成尝试次数和最大超时，防止无界内存及时间转换问题。
*/
static const size_t SESSION_ID_BYTES = 32;
static const size_t SESSION_ID_HEX_LENGTH = SESSION_ID_BYTES * 2;
static const size_t MAX_SESSION_KEY_LENGTH = 128;
static const size_t MAX_SESSION_VALUE_LENGTH = 4096;
static const size_t MAX_VALUES_PER_SESSION = 64;
static const size_t MAX_ID_GENERATION_ATTEMPTS = 32;
static const unsigned long MAX_SESSION_TIMEOUT_SECONDS = 31536000UL;

/*
函数：SessionStore::SessionRecord::SessionRecord
用途：建立尚未绑定时间和数据的空记录。
参数：无；由 createSession() 或 regenerateSession() 创建临时记录。
变量：values 是空 ValueMap；createdAt、lastAccessAt、expiresAt 先设为 0。
实现逻辑：只初始化成员，真实时间由 SessionStore 后续统一写入。
*/
SessionStore::SessionRecord::SessionRecord()
    : values(), createdAt(0), lastAccessAt(0), expiresAt(0)
{
}

/*
函数：SessionStore::SessionStore
用途：创建空存储并保存滑动过期与最大容量配置。
参数：timeoutSeconds/maxSessions 都由调用方传入；默认值适合简单 Bonus 示例。
变量：无局部变量；成员在初始化列表中设置。
实现逻辑：0 秒或 0 容量修正为 1；超时大于一年时限制为一年，避免异常配置和 time_t 转换风险。
*/
SessionStore::SessionStore(unsigned long timeoutSeconds,
                           size_t maxSessions)
    : _sessions(),
      _timeoutSeconds(timeoutSeconds == 0 ? 1 : timeoutSeconds),
      _maxSessions(maxSessions == 0 ? 1 : maxSessions)
{
    if (_timeoutSeconds > MAX_SESSION_TIMEOUT_SECONDS)
        _timeoutSeconds = MAX_SESSION_TIMEOUT_SECONDS;
}

/*
函数：SessionStore::resolveNow
用途：统一解析公开接口的可选时间参数，并检测系统时钟读取失败。
参数：requested 是调用方传入值；resolved 是调用方提供的输出变量。
变量：current 保存 std::time(NULL) 的结果。
实现逻辑：requested 非 0 时直接使用；否则读取当前时间，返回 time_t(-1) 时失败。
*/
bool SessionStore::resolveNow(std::time_t requested,
                              std::time_t &resolved) const
{
    if (requested != static_cast<std::time_t>(0))
    {
        resolved = requested;
        return true;
    }
    std::time_t current = std::time(NULL);
    if (current == static_cast<std::time_t>(-1))
        return false;
    resolved = current;
    return true;
}

/*
函数：SessionStore::computeExpiry
用途：计算滑动过期时间，并避免 now + timeout 发生有符号溢出。
参数：now 是 resolveNow() 得到的时间；expiry 是输出变量。
变量：maxTime 是 time_t 最大值；delta 是已限制超时转换后的 time_t。
实现逻辑：如果 now 已接近最大值则把 expiry 限制为 maxTime，否则安全相加。
*/
void SessionStore::computeExpiry(std::time_t now,
                                 std::time_t &expiry) const
{
    const std::time_t maxTime = std::numeric_limits<std::time_t>::max();
    const std::time_t delta = static_cast<std::time_t>(_timeoutSeconds);
    if (now > maxTime - delta)
        expiry = maxTime;
    else
        expiry = now + delta;
}

/*
函数：SessionStore::isExpired
用途：判断记录在给定时间是否已失效。
参数：record 来自 _sessions；now 是已解析的当前时间。
变量：无局部变量。
实现逻辑：now 达到或超过 expiresAt 时返回 true。
*/
bool SessionStore::isExpired(const SessionRecord &record,
                             std::time_t now) const
{
    return now >= record.expiresAt;
}

/*
函数：SessionStore::refresh
用途：记录有效访问时间并更新滑动过期截止时间。
参数：record 是 map 中可修改记录；now 是本次访问时间。
变量：无局部变量。
实现逻辑：写入 lastAccessAt，再调用 computeExpiry() 计算 expiresAt。
*/
void SessionStore::refresh(SessionRecord &record, std::time_t now) const
{
    record.lastAccessAt = now;
    computeExpiry(now, record.expiresAt);
}

/*
函数：SessionStore::fillRandomBytes
用途：从系统随机源读取指定长度字节，用于生成不可预测 Session ID。
参数：buffer 是调用方传入的可写数组；length 是需要填充的字节数。
变量：fd 是随机源文件描述符；offset 是已读取长度；bytesRead 是单次 read 结果。
实现逻辑：打开 /dev/urandom，循环读取直到填满；0、负值或打开失败都返回 false，并保证 fd 被关闭。
*/
bool SessionStore::fillRandomBytes(unsigned char *buffer, size_t length)
{
    if (buffer == NULL || length == 0)
        return false;
    int fd = open("/dev/urandom", O_RDONLY | O_NONBLOCK);
    if (fd < 0)
        return false;
    size_t offset = 0;
    while (offset < length)
    {
        ssize_t bytesRead = read(fd, buffer + offset, length - offset);
        if (bytesRead <= 0)
        {
            close(fd);
            return false;
        }
        offset += static_cast<size_t>(bytesRead);
    }
    close(fd);
    return true;
}

/*
函数：SessionStore::bytesToHex
用途：把随机字节转换成 Cookie 安全的小写十六进制文本。
参数：buffer 是随机字节数组；length 是数组有效长度。
变量：hex 是字符表；result 是输出字符串；i 是字节下标。
实现逻辑：每个字节拆为高四位和低四位，各追加一个字符。
*/
std::string SessionStore::bytesToHex(const unsigned char *buffer,
                                     size_t length)
{
    const char *hex = "0123456789abcdef";
    std::string result;
    result.reserve(length * 2);
    size_t i = 0;
    while (i < length)
    {
        result += hex[(buffer[i] >> 4) & 0x0F];
        result += hex[buffer[i] & 0x0F];
        ++i;
    }
    return result;
}

/*
函数：SessionStore::isValidSessionId
用途：验证客户端提供的 Session ID 是否符合固定格式。
参数：sessionId 是 Cookie 或调用方传入的候选值。
变量：i 遍历 64 个字符；c 是当前字符。
实现逻辑：长度必须为 64，且每个字符只能是 0-9 或 a-f。
*/
bool SessionStore::isValidSessionId(const std::string &sessionId)
{
    if (sessionId.size() != SESSION_ID_HEX_LENGTH)
        return false;
    size_t i = 0;
    while (i < sessionId.size())
    {
        char c = sessionId[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
            return false;
        ++i;
    }
    return true;
}

/*
函数：SessionStore::generateUniqueId
用途：生成当前 store 中尚未使用的合法 Session ID。
参数：sessionId 是调用方提供的输出变量。
变量：randomBytes 保存 32 字节随机数；attempt 限制碰撞重试；candidate 是编码后的候选值。
实现逻辑：最多尝试 32 次；读取、编码并查 map，不冲突时输出；任何异常或随机源失败都清空输出并返回 false。
*/
bool SessionStore::generateUniqueId(std::string &sessionId) const
{
    sessionId.clear();
    try
    {
        unsigned char randomBytes[SESSION_ID_BYTES];
        size_t attempt = 0;
        while (attempt < MAX_ID_GENERATION_ATTEMPTS)
        {
            if (!fillRandomBytes(randomBytes, SESSION_ID_BYTES))
                return false;
            std::string candidate = bytesToHex(randomBytes,
                SESSION_ID_BYTES);
            if (_sessions.find(candidate) == _sessions.end())
            {
                sessionId = candidate;
                return true;
            }
            ++attempt;
        }
    }
    catch (...)
    {
        sessionId.clear();
        return false;
    }
    return false;
}

/*
函数：SessionStore::isValidKey
用途：限制业务 key 的长度和字符集，避免空键、控制字符和无界名称。
参数：key 由 SessionDemo 或未来业务处理函数传入。
变量：i 是字符下标；c 是当前字符。
实现逻辑：长度必须为 1..128，只允许字母、数字、下划线、连字符和点。
*/
bool SessionStore::isValidKey(const std::string &key)
{
    if (key.empty() || key.size() > MAX_SESSION_KEY_LENGTH)
        return false;
    size_t i = 0;
    while (i < key.size())
    {
        char c = key[i];
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z')
            || (c >= 'a' && c <= 'z') || c == '_' || c == '-' || c == '.'))
            return false;
        ++i;
    }
    return true;
}

/*
函数：SessionStore::findActive
用途：集中执行 ID 校验、map 查找、过期删除和可选滑动续期。
参数：sessionId/now 来自公开操作；refreshExpiry 表示成功时是否刷新。
变量：it 是 _sessions 中的候选迭代器。
实现逻辑：非法或不存在返回 end；过期时删除并返回 end；有效时按参数刷新并返回迭代器。
*/
SessionStore::SessionMap::iterator SessionStore::findActive(
    const std::string &sessionId, std::time_t now, bool refreshExpiry)
{
    if (!isValidSessionId(sessionId))
        return _sessions.end();
    SessionMap::iterator it = _sessions.find(sessionId);
    if (it == _sessions.end())
        return it;
    if (isExpired(it->second, now))
    {
        _sessions.erase(it);
        return _sessions.end();
    }
    if (refreshExpiry)
        refresh(it->second, now);
    return it;
}

/*
函数：SessionStore::cleanupExpiredAt
用途：使用已解析时间删除全部过期记录，避免内部重复调用系统时间。
参数：now 来自 resolveNow() 成功结果。
变量：removed 是删除数量；it/current 用于 C++98 安全遍历删除。
实现逻辑：遇到过期记录时先推进主迭代器再 erase，最后返回累计数量。
*/
size_t SessionStore::cleanupExpiredAt(std::time_t now)
{
    size_t removed = 0;
    SessionMap::iterator it = _sessions.begin();
    while (it != _sessions.end())
    {
        if (isExpired(it->second, now))
        {
            SessionMap::iterator current = it;
            ++it;
            _sessions.erase(current);
            ++removed;
        }
        else
            ++it;
    }
    return removed;
}

/*
函数：SessionStore::createSession
用途：创建空 Session 并输出新随机 ID。
参数：sessionId 是输出变量；now 是调用方传入时间，0 表示当前时间。
变量：resolvedNow 是最终时间；candidate 是候选 ID；inserted 记录是否已插入；record 是待插入记录；insertResult 是 map 插入结果。
实现逻辑：解析时间、清理过期、检查容量、生成 ID、设置时间并插入；若输出赋值失败则删除刚插入记录，避免孤儿 Session。
*/
bool SessionStore::createSession(std::string &sessionId, std::time_t now)
{
    sessionId.clear();
    std::time_t resolvedNow = 0;
    if (!resolveNow(now, resolvedNow))
        return false;
    cleanupExpiredAt(resolvedNow);
    if (_sessions.size() >= _maxSessions)
        return false;
    std::string candidate;
    bool inserted = false;
    try
    {
        if (!generateUniqueId(candidate))
            return false;
        SessionRecord record;
        record.createdAt = resolvedNow;
        refresh(record, resolvedNow);
        std::pair<SessionMap::iterator, bool> insertResult =
            _sessions.insert(std::make_pair(candidate, record));
        if (!insertResult.second)
            return false;
        inserted = true;
        sessionId = candidate;
        return true;
    }
    catch (...)
    {
        if (inserted)
            _sessions.erase(candidate);
        sessionId.clear();
        return false;
    }
}

/*
函数：SessionStore::resumeSession
用途：验证客户端 ID 是否对应有效记录，并刷新滑动过期时间。
参数：sessionId 通常来自 Cookie；now 是请求时间，0 表示当前时间。
变量：resolvedNow 是解析后的时间。
实现逻辑：时间有效时调用 findActive(..., true)，找到记录返回 true。
*/
bool SessionStore::resumeSession(const std::string &sessionId,
                                 std::time_t now)
{
    std::time_t resolvedNow = 0;
    if (!resolveNow(now, resolvedNow))
        return false;
    return findActive(sessionId, resolvedNow, true) != _sessions.end();
}

/*
函数：SessionStore::regenerateSession
用途：保留业务数据并替换 Session ID，防止权限提升后继续使用旧 ID。
参数：oldSessionId 是输入；newSessionId 是输出；now 是轮换时间。
变量：oldIdCopy 在清空输出前保存旧 ID；it 定位旧记录；candidate 是新 ID；inserted 记录新项是否已插入；record 是旧记录副本；insertResult 是插入结果。
实现逻辑：先查有效旧记录并插入新记录；新 ID 成功写入输出后才删除旧记录，异常时删除新记录并保留旧记录。
*/
bool SessionStore::regenerateSession(const std::string &oldSessionId,
                                     std::string &newSessionId,
                                     std::time_t now)
{
    std::string oldIdCopy;
    try
    {
        oldIdCopy = oldSessionId;
    }
    catch (...)
    {
        newSessionId.clear();
        return false;
    }
    newSessionId.clear();
    std::time_t resolvedNow = 0;
    if (!resolveNow(now, resolvedNow))
        return false;
    SessionMap::iterator it = findActive(oldIdCopy, resolvedNow, true);
    if (it == _sessions.end())
        return false;
    std::string candidate;
    bool inserted = false;
    try
    {
        if (!generateUniqueId(candidate))
            return false;
        SessionRecord record = it->second;
        refresh(record, resolvedNow);
        std::pair<SessionMap::iterator, bool> insertResult =
            _sessions.insert(std::make_pair(candidate, record));
        if (!insertResult.second)
            return false;
        inserted = true;
        newSessionId = candidate;
        _sessions.erase(it);
        return true;
    }
    catch (...)
    {
        if (inserted)
            _sessions.erase(candidate);
        newSessionId.clear();
        return false;
    }
}

/*
函数：SessionStore::destroySession
用途：立即删除指定 Session。
参数：sessionId 来自 Cookie 或业务层。
变量：无局部变量。
实现逻辑：格式非法返回 false；map 中存在并删除一条时返回 true。
*/
bool SessionStore::destroySession(const std::string &sessionId)
{
    if (!isValidSessionId(sessionId))
        return false;
    return _sessions.erase(sessionId) == 1;
}

/*
函数：SessionStore::setValue
用途：在有效 Session 中新增或覆盖受限键值，并刷新会话过期时间。
参数：sessionId/key/value 是调用方输入；now 是请求时间。
变量：resolvedNow 是最终时间；it 是有效记录；existing 判断 key 是否存在；insertResult 检查新 key 插入结果。
实现逻辑：已有 key 直接赋值，新 key 使用 map::insert 原子插入；分配失败时不留下默认空 value。
*/
bool SessionStore::setValue(const std::string &sessionId,
                            const std::string &key,
                            const std::string &value,
                            std::time_t now)
{
    if (!isValidKey(key) || value.size() > MAX_SESSION_VALUE_LENGTH)
        return false;
    std::time_t resolvedNow = 0;
    if (!resolveNow(now, resolvedNow))
        return false;
    SessionMap::iterator it = findActive(sessionId, resolvedNow, false);
    if (it == _sessions.end())
        return false;
    ValueMap::iterator existing = it->second.values.find(key);
    if (existing == it->second.values.end()
        && it->second.values.size() >= MAX_VALUES_PER_SESSION)
        return false;
    try
    {
        if (existing != it->second.values.end())
            existing->second = value;
        else
        {
            std::pair<ValueMap::iterator, bool> insertResult =
                it->second.values.insert(std::make_pair(key, value));
            if (!insertResult.second)
                return false;
        }
        refresh(it->second, resolvedNow);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

/*
函数：SessionStore::getValue
用途：读取有效 Session 中的一个值，并在成功读取后刷新过期时间。
参数：sessionId/key 是输入；value 是输出；now 是请求时间。
变量：resolvedNow 是最终时间；sessionIt/valueIt 分别定位会话和键。
实现逻辑：失败先保持 value 为空；只有 key 存在且复制成功后才刷新并返回 true。
*/
bool SessionStore::getValue(const std::string &sessionId,
                            const std::string &key,
                            std::string &value,
                            std::time_t now)
{
    value.clear();
    if (!isValidKey(key))
        return false;
    std::time_t resolvedNow = 0;
    if (!resolveNow(now, resolvedNow))
        return false;
    SessionMap::iterator sessionIt = findActive(sessionId,
        resolvedNow, false);
    if (sessionIt == _sessions.end())
        return false;
    ValueMap::iterator valueIt = sessionIt->second.values.find(key);
    if (valueIt == sessionIt->second.values.end())
        return false;
    try
    {
        value = valueIt->second;
        refresh(sessionIt->second, resolvedNow);
        return true;
    }
    catch (...)
    {
        value.clear();
        return false;
    }
}

/*
函数：SessionStore::removeValue
用途：删除有效 Session 中的指定 key，并在确实删除后刷新过期时间。
参数：sessionId/key 是调用方输入；now 是请求时间。
变量：resolvedNow 是最终时间；it 是有效会话；removed 是 erase 返回数量。
实现逻辑：输入和会话有效后执行 erase；只有删除成功才刷新并返回 true。
*/
bool SessionStore::removeValue(const std::string &sessionId,
                               const std::string &key,
                               std::time_t now)
{
    if (!isValidKey(key))
        return false;
    std::time_t resolvedNow = 0;
    if (!resolveNow(now, resolvedNow))
        return false;
    SessionMap::iterator it = findActive(sessionId, resolvedNow, false);
    if (it == _sessions.end())
        return false;
    size_t removed = it->second.values.erase(key);
    if (removed == 0)
        return false;
    refresh(it->second, resolvedNow);
    return true;
}

/*
函数：SessionStore::clearValues
用途：清空有效 Session 的所有业务值，并保留同一个 ID。
参数：sessionId 是输入；now 是请求时间。
变量：resolvedNow 是最终时间；it 是有效记录。
实现逻辑：找到会话后 clear ValueMap，并把本次成功操作视为有效访问进行续期。
*/
bool SessionStore::clearValues(const std::string &sessionId,
                               std::time_t now)
{
    std::time_t resolvedNow = 0;
    if (!resolveNow(now, resolvedNow))
        return false;
    SessionMap::iterator it = findActive(sessionId, resolvedNow, false);
    if (it == _sessions.end())
        return false;
    it->second.values.clear();
    refresh(it->second, resolvedNow);
    return true;
}

/*
函数：SessionStore::cleanupExpired
用途：批量删除所有已经过期的 Session。
参数：now 是检查时间，0 表示读取当前时间。
变量：resolvedNow 是最终时间。
实现逻辑：系统时间读取失败时不删除任何记录；成功时转交 cleanupExpiredAt()。
*/
size_t SessionStore::cleanupExpired(std::time_t now)
{
    std::time_t resolvedNow = 0;
    if (!resolveNow(now, resolvedNow))
        return 0;
    return cleanupExpiredAt(resolvedNow);
}

/*
函数：SessionStore::size
用途：返回当前 map 中保存的记录数量。
参数：无。
变量：无局部变量。
实现逻辑：直接返回 _sessions.size()。
*/
size_t SessionStore::size() const
{
    return _sessions.size();
}

/*
函数：SessionStore::getTimeoutSeconds
用途：返回经过构造函数边界保护的 Session 超时秒数。
参数：无。
变量：无局部变量。
实现逻辑：直接返回 _timeoutSeconds。
*/
unsigned long SessionStore::getTimeoutSeconds() const
{
    return _timeoutSeconds;
}

/*
函数：SessionStore::getMaxSessions
用途：返回最大 Session 容量。
参数：无。
变量：无局部变量。
实现逻辑：直接返回 _maxSessions。
*/
size_t SessionStore::getMaxSessions() const
{
    return _maxSessions;
}
