#include "SessionStore.hpp"
#include <limits>
#include <utility>
static const size_t SESSION_ID_BYTES = 32;
static const size_t SESSION_ID_HEX_LENGTH = SESSION_ID_BYTES * 2;
static const size_t MAX_SESSION_KEY_LENGTH = 128;
static const size_t MAX_SESSION_VALUE_LENGTH = 4096;
static const size_t MAX_VALUES_PER_SESSION = 64;
static const size_t MAX_ID_GENERATION_ATTEMPTS = 32;
static const unsigned long MAX_SESSION_TIMEOUT_SECONDS = 31536000UL;

// Creates a new SessionRecord object.
SessionStore::SessionRecord::SessionRecord() : values(), createdAt(0), lastAccessAt(0), expiresAt(0)
{
}

// Creates a new SessionStore object.
SessionStore::SessionStore(unsigned long timeoutSeconds, size_t maxSessions) : _sessions(), _timeoutSeconds(timeoutSeconds == 0 ? 1 : timeoutSeconds), _maxSessions(maxSessions == 0 ? 1 : maxSessions)
{
    if (_timeoutSeconds > MAX_SESSION_TIMEOUT_SECONDS)
        _timeoutSeconds = MAX_SESSION_TIMEOUT_SECONDS;
}

// Finds now.
bool SessionStore::resolveNow(std::time_t requested, std::time_t &resolved) const
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

// Calculates the expiry time for one session.
void SessionStore::computeExpiry(std::time_t now, std::time_t &expiry) const
{
    const std::time_t maxTime = std::numeric_limits<std::time_t>::max();
    const std::time_t delta = static_cast<std::time_t>(_timeoutSeconds);
    if (now > maxTime - delta)
        expiry = maxTime;
    else expiry = now + delta;
}

// Checks whether the session has expired.
bool SessionStore::isExpired(const SessionRecord &record, std::time_t now) const
{
    return now >= record.expiresAt;
}

// Updates the access and expiry time of one session.
void SessionStore::refresh(SessionRecord &record, std::time_t now) const
{
    record.lastAccessAt = now;
    computeExpiry(now, record.expiresAt);
}

// Fills a byte buffer with values used for a session ID.
bool SessionStore::fillRandomBytes(unsigned char *buffer, size_t length)
{
    if (buffer == NULL || length == 0)
        return false;
    static unsigned long state = 0;
    static unsigned long generationCounter = 0;
    if (state == 0)
    {
        state = static_cast<unsigned long>(std::time(NULL));
        state ^= static_cast<unsigned long>(std::clock());
        state ^= 0x9E3779B9UL;
        if (state == 0)
            state = 0xA5A5A5A5UL;
    }
    ++generationCounter;
    state ^= generationCounter * 2654435761UL;
    size_t i = 0;
    while (i < length)
    {
        state = state * 1664525UL + 1013904223UL;
        state ^= state >> 13;
        state *= 2246822519UL;
        state ^= state >> 16;
        buffer[i] = static_cast<unsigned char>(state & 0xFFUL);
        ++i;
    }
    return true;
}

// Changes bytes into lower-case hexadecimal text.
std::string SessionStore::bytesToHex(const unsigned char *buffer, size_t length)
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

// Checks whether valid session id.
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

// Creates a session ID that is not already in the store.
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
            std::string candidate = bytesToHex(randomBytes, SESSION_ID_BYTES);
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

// Checks whether valid key.
bool SessionStore::isValidKey(const std::string &key)
{
    if (key.empty() || key.size() > MAX_SESSION_KEY_LENGTH)
        return false;
    size_t i = 0;
    while (i < key.size())
    {
        char c = key[i];
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || c == '-' || c == '.'))
            return false;
        ++i;
    }
    return true;
}

// Finds active.
SessionStore::SessionMap::iterator SessionStore::findActive(const std::string &sessionId, std::time_t now, bool refreshExpiry)
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

// Cleans up expired at.
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
        else ++it;
    }
    return removed;
}

// Creates session.
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
        std::pair<SessionMap::iterator, bool> insertResult = _sessions.insert(std::make_pair(candidate, record));
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

// Finds an existing session and refreshes its expiry time.
bool SessionStore::resumeSession(const std::string &sessionId, std::time_t now)
{
    std::time_t resolvedNow = 0;
    if (!resolveNow(now, resolvedNow))
        return false;
    return findActive(sessionId, resolvedNow, true) != _sessions.end();
}

// Replaces an old session ID with a new session ID.
bool SessionStore::regenerateSession(const std::string &oldSessionId, std::string &newSessionId, std::time_t now)
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
        std::pair<SessionMap::iterator, bool> insertResult = _sessions.insert(std::make_pair(candidate, record));
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

// Removes one session from the store.
bool SessionStore::destroySession(const std::string &sessionId)
{
    if (!isValidSessionId(sessionId))
        return false;
    return _sessions.erase(sessionId) == 1;
}

// Sets value.
bool SessionStore::setValue(const std::string &sessionId, const std::string &key, const std::string &value, std::time_t now)
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
    if (existing == it->second.values.end() && it->second.values.size() >= MAX_VALUES_PER_SESSION)
        return false;
    try
    {
        if (existing != it->second.values.end())
            existing->second = value;
        else
        {
            std::pair<ValueMap::iterator, bool> insertResult = it->second.values.insert(std::make_pair(key, value));
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

// Returns value.
bool SessionStore::getValue(const std::string &sessionId, const std::string &key, std::string &value, std::time_t now)
{
    value.clear();
    if (!isValidKey(key))
        return false;
    std::time_t resolvedNow = 0;
    if (!resolveNow(now, resolvedNow))
        return false;
    SessionMap::iterator sessionIt = findActive(sessionId, resolvedNow, false);
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

// Removes value.
bool SessionStore::removeValue(const std::string &sessionId, const std::string &key, std::time_t now)
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

// Clears values.
bool SessionStore::clearValues(const std::string &sessionId, std::time_t now)
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

// Cleans up expired.
size_t SessionStore::cleanupExpired(std::time_t now)
{
    std::time_t resolvedNow = 0;
    if (!resolveNow(now, resolvedNow))
        return 0;
    return cleanupExpiredAt(resolvedNow);
}

// Returns the number of sessions in the store.
size_t SessionStore::size() const
{
    return _sessions.size();
}

// Returns timeout seconds.
unsigned long SessionStore::getTimeoutSeconds() const
{
    return _timeoutSeconds;
}

// Returns max sessions.
size_t SessionStore::getMaxSessions() const
{
    return _maxSessions;
}
