#ifndef RESPONSE_HPP
#define RESPONSE_HPP
#include <cstddef>
#include <map>
#include <string>
class Request;
class SessionStore;
class Response
{
public:
    typedef std::map<std::string, std::string> HeaderMap;
    typedef std::map<int, std::string> ErrorPageMap;
private:
    std::string _version;
    int _statusCode;
    std::string _statusMessage;
    HeaderMap _headers;
    std::string _body;
    bool _closeConnection;
    bool _suppressBody;
    // Returns the text message for an HTTP status code.
    static std::string statusMessageFor(int statusCode);
    // Checks whether the status code is an error status.
    static bool isErrorStatusCode(int statusCode);
    // Checks whether this HTTP status may have a body.
    static bool statusMayHaveBody(int statusCode);
    // Changes a size value into decimal text.
    static std::string sizeToString(size_t value);
    // Changes ASCII letters in the text to lower case.
    static std::string toLowerAscii(const std::string &value);
    // Changes a header name to the form used by this project.
    static std::string canonicalHeaderName(const std::string &name);
    // Checks whether the response header is managed by this class.
    static bool isManagedHeader(const std::string &name);
    // Checks whether valid header name.
    static bool isValidHeaderName(const std::string &name);
    // Checks whether valid header value.
    static bool isValidHeaderValue(const std::string &value);
    // Checks whether the request asks to close the connection.
    static bool requestWantsClose(const Request &request);
    // Sets managed header.
    void setManagedHeader(const std::string &name, const std::string &value);
    // Updates content length.
    void updateContentLength();
    // Updates connection header.
    void updateConnectionHeader();
    // Loads custom error page.
    bool loadCustomErrorPage(const ErrorPageMap &errorPages);
    // Reads opened file into body.
    bool readOpenedFileIntoBody(int fd);
    // Sets default error page.
    void setDefaultErrorPage();
    // Loads cgi output.
    bool loadCgiOutput(const std::string &cgiOutput);
    // Builds cgi response.
    friend Response buildCgiResponse(const Request &request, const std::string &cgiOutput);
public:
    explicit Response(bool closeConnection = false);
    // Creates a new Response object.
    explicit Response(const Request &request);
    // Returns version.
    const std::string &getVersion() const;
    // Returns status code.
    int getStatusCode() const;
    // Returns status message.
    const std::string &getStatusMessage() const;
    // Returns headers.
    const HeaderMap &getHeaders() const;
    // Returns body.
    const std::string &getBody() const;
    // Returns whether this response should close the connection.
    bool shouldCloseConnection() const;
    // Returns header.
    bool getHeader(const std::string &name, std::string &value) const;
    // Sets status.
    void setStatus(int statusCode);
    // Sets header.
    void setHeader(const std::string &name, const std::string &value);
    // Removes header.
    void removeHeader(const std::string &name);
    // Sets body.
    void setBody(const std::string &body);
    // Adds body.
    void appendBody(const std::string &data);
    // Adds body.
    void appendBody(const char *data, size_t length);
    // Clears body.
    void clearBody();
    // Clears body only.
    void clearBodyOnly();
    // Sets close connection.
    void setCloseConnection(bool closeConnection);
    // Creates response.
    void createResponse(unsigned int code, const std::string &bodyText, const ErrorPageMap &errorPages);
    // Parses cgi output.
    void parseCgiOutput(const std::string &cgiOutput);
    // Builds the full HTTP response text.
    std::string responseToString() const;
};

// Builds the response for one HTTP request.
Response buildResponse(const Request &request, SessionStore &sessionStore);

// Builds cgi response.
Response buildCgiResponse(const Request &request, const std::string &cgiOutput);
#endif
