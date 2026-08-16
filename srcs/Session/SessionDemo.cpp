#include "SessionDemo.hpp"
#include "SessionCookie.hpp"
#include <limits>
#include <sstream>
static const char *SESSION_COOKIE_NAME = "WEBSERV_SESSION";

// Escapes text so it can be used safely in HTML.
static std::string escapeHtml(const std::string &text)
{
    std::string output;
    size_t i = 0;
    while (i < text.size())
    {
        if (text[i] == '&')
            output += "&amp;";
        else if (text[i] == '<')
            output += "&lt;";
        else if (text[i] == '>')
            output += "&gt;";
        else if (text[i] == '"')
            output += "&quot;";
        else if (text[i] == '\'')
            output += "&#39;";
        else output += text[i];
        ++i;
    }
    return output;
}

// Checks whether valid user name.
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

// Parses unsigned long.
static bool parseUnsignedLong(const std::string &value, unsigned long &result)
{
    result = 0;
    if (value.empty())
        return false;
    const unsigned long maxValue = std::numeric_limits<unsigned long>::max();
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

// Changes an unsigned long value into decimal text.
static std::string unsignedLongToString(unsigned long value)
{
    std::ostringstream output;
    output << value;
    return output.str();
}

// Finds or create session.
static bool resolveOrCreateSession(const std::string &cookieHeader, SessionStore &store, std::time_t now, std::string &sessionId, bool &created)
{
    sessionId.clear();
    created = false;
    std::string candidate;
    if (SessionCookie::getCookie(cookieHeader, SESSION_COOKIE_NAME, candidate) && store.resumeSession(candidate, now))
    {
        sessionId = candidate;
        return true;
    }
    if (!store.createSession(sessionId, now))
        return false;
    created = true;
    return true;
}

// Sets refresh cookie.
static bool setRefreshCookie(const std::string &sessionId, const SessionStore &store, SessionDemoResult &result)
{
    if (!SessionCookie::buildSetCookie(SESSION_COOKIE_NAME, sessionId, "/", store.getTimeoutSeconds(), true, "Lax", false, result.setCookieValue))
        return false;
    result.hasSetCookie = true;
    return true;
}

// Cleans up failed example.
static bool cleanupFailedExample(SessionStore &store, const std::string &sessionId, bool destroySession, SessionDemoResult &result)
{
    if (destroySession && !sessionId.empty())
        store.destroySession(sessionId);
    result.reset();
    return false;
}

// Creates a new SessionDemoResult object.
SessionDemoResult::SessionDemoResult() : statusCode(500), hasSetCookie(false), setCookieValue(), body(), sessionId()
{
}

// Resets the result object to its safe default values.
void SessionDemoResult::reset()
{
    statusCode = 500;
    hasSetCookie = false;
    setCookieValue.clear();
    body.clear();
    sessionId.clear();
}

// Returns the cookie name used for sessions.
const char *SessionDemo::sessionCookieName()
{
    return SESSION_COOKIE_NAME;
}

// Builds counter example.
bool SessionDemo::buildCounterExample(const std::string &cookieHeader, SessionStore &store, std::time_t now, SessionDemoResult &result)
{
    result.reset();
    bool created = false;
    try
    {
        if (!resolveOrCreateSession(cookieHeader, store, now, result.sessionId, created))
            return false;
        std::string username;
        bool isLoggedIn = store.getValue(result.sessionId, "user", username, now);
        std::string storedVisits;
        unsigned long visits = 0;
        if (store.getValue(result.sessionId, "visits", storedVisits, now) && !parseUnsignedLong(storedVisits, visits))
            visits = 0;
        const unsigned long maxValue = std::numeric_limits<unsigned long>::max();
        if (visits == maxValue)
            visits = 0;
        ++visits;
        if (!setRefreshCookie(result.sessionId, store, result) || !store.setValue(result.sessionId, "visits", unsignedLongToString(visits), now))
            return cleanupFailedExample(store, result.sessionId, created, result);
        result.statusCode = 200;
        result.body = "<!DOCTYPE html><html><head><title>Session Counter" "</title></head><body><h1>Cookie and Session Demo</h1>";
        if (isLoggedIn && !username.empty())
        {
            result.body += "<p style=\"color:green;\"><strong>Current User: </strong>" + escapeHtml(username) + "</p>";
        }
        else
        {
            result.body += "<p style=\"color:gray;\"><strong>Current User: </strong><em>Guest (Not Logged In)</em></p>";
        }
        result.body += "<p>Visits: ";
        result.body += unsignedLongToString(visits);
        result.body += "</p><form method=\"post\" action=\"/session/login\">" "<label>User <input name=\"user\" maxlength=\"64\"></label>" "<button type=\"submit\">Login</button></form>" "<form method=\"post\" action=\"/session/logout\">" "<button type=\"submit\">Logout</button></form>" "</body></html>";
        return true;
    }
    catch (...)
    {
        return cleanupFailedExample(store, result.sessionId, created, result);
    }
}

// Builds login example.
bool SessionDemo::buildLoginExample(const std::string &cookieHeader, const std::string &userName, SessionStore &store, std::time_t now, SessionDemoResult &result)
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
        if (!resolveOrCreateSession(cookieHeader, store, now, result.sessionId, created))
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
        if (!setRefreshCookie(result.sessionId, store, result) || !store.setValue(result.sessionId, "user", userName, now))
            return cleanupFailedExample(store, result.sessionId, ownsCurrentSession, result);
        result.statusCode = 200;
        result.body = "<!DOCTYPE html><html><head><title>Session Login" "</title></head><body><h1>Logged in</h1><p>User: ";
        result.body += escapeHtml(userName);
        result.body += "</p><p><a href=\"/session/counter\">" "Back to counter</a></p><form method=\"post\" " "action=\"/session/logout\"><button type=\"submit\">" "Logout</button></form></body></html>";
        return true;
    }
    catch (...)
    {
        return cleanupFailedExample(store, result.sessionId, ownsCurrentSession, result);
    }
}

// Builds logout example.
bool SessionDemo::buildLogoutExample(const std::string &cookieHeader, SessionStore &store, SessionDemoResult &result)
{
    result.reset();
    try
    {
        std::string expiredCookie;
        if (!SessionCookie::buildExpiredCookie(SESSION_COOKIE_NAME, "/", true, "Lax", false, expiredCookie))
            return false;
        std::string body = "<!DOCTYPE html><html><head><title>Session Logout" "</title></head><body><h1>Logged out</h1><p><a " "href=\"/session/counter\">Start a new session</a></p>" "</body></html>";
        result.statusCode = 200;
        result.hasSetCookie = true;
        result.setCookieValue = expiredCookie;
        result.body = body;
        std::string sessionId;
        if (SessionCookie::getCookie(cookieHeader, SESSION_COOKIE_NAME, sessionId))
            store.destroySession(sessionId);
        return true;
    }
    catch (...)
    {
        result.reset();
        return false;
    }
}
