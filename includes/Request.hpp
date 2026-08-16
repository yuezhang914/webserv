#ifndef REQUEST_HPP
#define REQUEST_HPP
#include <map>
#include <string>
class ServerConfig;
class RequestParser;
enum RequestStatus
{
    REQUEST_OK = 0, REQUEST_INCOMPLETE = 1, REQUEST_ERROR = -1, REQUEST_VERSION_NOT_SUPPORTED = -2, REQUEST_BODY_TOO_LARGE = -3
};
class Request
{
public:
    typedef std::map<std::string, std::string> HeaderMap;
private:
    std::string _method;
    std::string _uri;
    std::string _path;
    std::string _query;
    std::string _version;
    HeaderMap _headers;
    std::string _body;
    const ServerConfig *_config;
    // Changes ASCII letters in the text to lower case.
    static std::string toLowerAscii(const std::string &value);
    // Resets for parsing.
    void resetForParsing(const ServerConfig *server);
    friend class RequestParser;
public:
    // Creates a new Request object.
    Request();
    // Returns method.
    const std::string &getMethod() const;
    // Returns uri.
    const std::string &getUri() const;
    // Returns path.
    const std::string &getPath() const;
    // Returns query.
    const std::string &getQuery() const;
    // Returns version.
    const std::string &getVersion() const;
    // Returns headers.
    const HeaderMap &getHeaders() const;
    // Returns body.
    const std::string &getBody() const;
    // Returns config.
    const ServerConfig *getConfig() const;
    // Returns header.
    bool getHeader(const std::string &name, std::string &value) const;
};
#endif
