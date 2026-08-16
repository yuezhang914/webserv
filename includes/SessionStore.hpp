#ifndef SESSIONSTORE_HPP
#define SESSIONSTORE_HPP
#include <cstddef>
#include <ctime>
#include <map>
#include <string>
class SessionStore
{
public:
    typedef std::map<std::string, std::string> ValueMap;
    explicit SessionStore(unsigned long timeoutSeconds = 1800, size_t maxSessions = 10000);
    bool createSession(std::string &sessionId, std::time_t now = 0);
    bool resumeSession(const std::string &sessionId, std::time_t now = 0);
    bool regenerateSession(const std::string &oldSessionId, std::string &newSessionId, std::time_t now = 0);
    // Removes one session from the store.
    bool destroySession(const std::string &sessionId);
    bool setValue(const std::string &sessionId, const std::string &key, const std::string &value, std::time_t now = 0);
    bool getValue(const std::string &sessionId, const std::string &key, std::string &value, std::time_t now = 0);
    bool removeValue(const std::string &sessionId, const std::string &key, std::time_t now = 0);
    bool clearValues(const std::string &sessionId, std::time_t now = 0);
    size_t cleanupExpired(std::time_t now = 0);
    // Returns the number of sessions in the store.
    size_t size() const;
    // Returns timeout seconds.
    unsigned long getTimeoutSeconds() const;
    // Returns max sessions.
    size_t getMaxSessions() const;
    // Checks whether valid session id.
    static bool isValidSessionId(const std::string &sessionId);
private:
    struct SessionRecord
    {
        ValueMap values;
        std::time_t createdAt;
        std::time_t lastAccessAt;
        std::time_t expiresAt;
        // Creates a new SessionRecord object.
        SessionRecord();
    };
    typedef std::map<std::string, SessionRecord> SessionMap;
    SessionMap _sessions;
    unsigned long _timeoutSeconds;
    size_t _maxSessions;
    // Creates a new SessionStore object.
    SessionStore(const SessionStore &other);
    // Copies data from another object.
    SessionStore &operator=(const SessionStore &other);
    // Finds now.
    bool resolveNow(std::time_t requested, std::time_t &resolved) const;
    // Calculates the expiry time for one session.
    void computeExpiry(std::time_t now, std::time_t &expiry) const;
    // Checks whether the session has expired.
    bool isExpired(const SessionRecord &record, std::time_t now) const;
    // Updates the access and expiry time of one session.
    void refresh(SessionRecord &record, std::time_t now) const;
    // Creates a session ID that is not already in the store.
    bool generateUniqueId(std::string &sessionId) const;
    // Fills a byte buffer with values used for a session ID.
    static bool fillRandomBytes(unsigned char *buffer, size_t length);
    // Changes bytes into lower-case hexadecimal text.
    static std::string bytesToHex(const unsigned char *buffer, size_t length);
    // Checks whether valid key.
    static bool isValidKey(const std::string &key);
    // Finds active.
    SessionMap::iterator findActive(const std::string &sessionId, std::time_t now, bool refreshExpiry);
    // Cleans up expired at.
    size_t cleanupExpiredAt(std::time_t now);
};
#endif
