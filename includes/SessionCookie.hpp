#ifndef SESSIONCOOKIE_HPP
#define SESSIONCOOKIE_HPP
#include <map>
#include <string>
class SessionCookie
{
public:
    typedef std::map<std::string, std::string> CookieMap;
    // Parses cookie header.
    static bool parseCookieHeader(const std::string &headerValue, CookieMap &cookies);
    // Returns cookie.
    static bool getCookie(const std::string &headerValue, const std::string &name, std::string &value);
    // Builds set cookie.
    static bool buildSetCookie(const std::string &name, const std::string &value, const std::string &path, unsigned long maxAge, bool httpOnly, const std::string &sameSite, bool secure, std::string &headerValue);
    // Builds expired cookie.
    static bool buildExpiredCookie(const std::string &name, const std::string &path, bool httpOnly, const std::string &sameSite, bool secure, std::string &headerValue);
    // Checks whether valid cookie name.
    static bool isValidCookieName(const std::string &name);
    // Checks whether valid cookie value.
    static bool isValidCookieValue(const std::string &value);
private:
    // Creates a new SessionCookie object.
    SessionCookie();
    // Removes extra spaces from ows.
    static std::string trimOws(const std::string &value);
    // Checks whether the character is valid in an HTTP token.
    static bool isTokenChar(char c);
    // Checks whether valid path.
    static bool isValidPath(const std::string &path);
    // Checks whether valid same site.
    static bool isValidSameSite(const std::string &sameSite, bool secure);
    // Changes an unsigned long value into decimal text.
    static std::string unsignedLongToString(unsigned long value);
};
#endif
