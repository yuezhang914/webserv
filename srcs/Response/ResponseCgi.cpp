#include "Response.hpp"
#include "Request.hpp"
#include "ServerConfig.hpp"
#include "ResponseInternal.hpp"
#include <sstream>

// Loads cgi output.
bool Response::loadCgiOutput(const std::string &cgiOutput)
{
    if (cgiOutput.empty())
        return false;
    size_t delimiterPos = cgiOutput.find("\r\n\r\n");
    size_t delimiterLength = 4;
    if (delimiterPos == std::string::npos)
    {
        delimiterPos = cgiOutput.find("\n\n");
        delimiterLength = 2;
    }
    bool inheritedClose = _closeConnection;
    if (delimiterPos == std::string::npos)
    {
        _statusCode = 200;
        _statusMessage = statusMessageFor(_statusCode);
        _headers.clear();
        _body = cgiOutput;
        _closeConnection = inheritedClose;
        _headers["Content-Type"] = "text/html";
        updateConnectionHeader();
        updateContentLength();
        return true;
    }
    std::string headerBlock = cgiOutput.substr(0, delimiterPos);
    std::string body = cgiOutput.substr(delimiterPos + delimiterLength);
    HeaderMap importedHeaders;
    int importedStatus = 200;
    bool hasStatus = false;
    size_t cursor = 0;
    while (cursor <= headerBlock.size())
    {
        size_t lineEnd = headerBlock.find('\n', cursor);
        if (lineEnd == std::string::npos)
            lineEnd = headerBlock.size();
        std::string line = headerBlock.substr(cursor, lineEnd - cursor);
        if (!line.empty() && line[line.size() - 1] == '\r')
            line.erase(line.size() - 1);
        if (line.empty())
            return false;
        size_t colon = line.find(':');
        if (colon == std::string::npos)
            return false;
        std::string name = line.substr(0, colon);
        std::string value = responseTrimOws(line.substr(colon + 1));
        if (!isValidHeaderName(name) || !isValidHeaderValue(value))
            return false;
        std::string lowerName = toLowerAscii(name);
        if (lowerName == "status")
        {
            if (hasStatus)
                return false;
            std::istringstream statusStream(value);
            if (!(statusStream >> importedStatus) || importedStatus < 100 || importedStatus > 599)
                return false;
            hasStatus = true;
        }
        else if (lowerName != "content-length" && lowerName != "transfer-encoding" && lowerName != "connection")
        {
            importedHeaders[canonicalHeaderName(name)] = value;
        }
        if (lineEnd == headerBlock.size())
            break;
        cursor = lineEnd + 1;
    }
    _statusCode = importedStatus;
    _statusMessage = statusMessageFor(_statusCode);
    _headers.clear();
    _body.clear();
    _closeConnection = inheritedClose;
    HeaderMap::const_iterator it = importedHeaders.begin();
    while (it != importedHeaders.end())
    {
        if (statusMayHaveBody(_statusCode) || toLowerAscii(it->first) != "content-type")
            _headers[it->first] = it->second;
        ++it;
    }
    if (statusMayHaveBody(_statusCode))
    {
        if (_headers.find("Content-Type") == _headers.end() && _headers.find("Location") == _headers.end())
            _headers["Content-Type"] = "text/html";
        _body = body;
    }
    else _headers.erase("Content-Type");
    updateConnectionHeader();
    updateContentLength();
    return true;
}

// Parses cgi output.
void Response::parseCgiOutput(const std::string &cgiOutput)
{
    if (loadCgiOutput(cgiOutput))
        return;
    ErrorPageMap noErrorPages;
    createResponse(502, "Invalid CGI response", noErrorPages);
    setCloseConnection(true);
}

// Builds cgi response.
Response buildCgiResponse(const Request &request, const std::string &cgiOutput)
{
    Response response(request);
    if (response.loadCgiOutput(cgiOutput))
        return response;
    const ServerConfig *server = request.getConfig();
    if (server != NULL)
        response.createResponse(502, "Invalid CGI response", server->error_pages);
    else
    {
        Response::ErrorPageMap noErrorPages;
        response.createResponse(502, "Invalid CGI response", noErrorPages);
    }
    response.setCloseConnection(true);
    return response;
}
