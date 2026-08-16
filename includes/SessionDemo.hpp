#ifndef SESSIONDEMO_HPP
#define SESSIONDEMO_HPP
#include "SessionStore.hpp"
#include <ctime>
#include <string>
struct SessionDemoResult
{
    int statusCode;
    bool hasSetCookie;
    std::string setCookieValue;
    std::string body;
    std::string sessionId;
    // Creates a new SessionDemoResult object.
    SessionDemoResult();
    // Resets the result object to its safe default values.
    void reset();
};
class SessionDemo
{
public:
    // Builds counter example.
    static bool buildCounterExample(const std::string &cookieHeader, SessionStore &store, std::time_t now, SessionDemoResult &result);
    // Builds login example.
    static bool buildLoginExample(const std::string &cookieHeader, const std::string &userName, SessionStore &store, std::time_t now, SessionDemoResult &result);
    // Builds logout example.
    static bool buildLogoutExample(const std::string &cookieHeader, SessionStore &store, SessionDemoResult &result);
    // Returns the cookie name used for sessions.
    static const char *sessionCookieName();
private:
    // Creates a new SessionDemo object.
    SessionDemo();
};
#endif
