#include "Response.hpp"
#include "Request.hpp"
#include <sstream>

// Creates a new Response object.
Response::Response(bool closeConnection) : _version("HTTP/1.1"), _statusCode(200), _statusMessage("OK"), _headers(), _body(), _closeConnection(closeConnection), _suppressBody(false)
{
    updateConnectionHeader();
    updateContentLength();
}

// Creates a new Response object.
Response::Response(const Request &request) : _version("HTTP/1.1"), _statusCode(200), _statusMessage("OK"), _headers(), _body(), _closeConnection(requestWantsClose(request)), _suppressBody(request.getMethod() == "HEAD")
{
    updateConnectionHeader();
    updateContentLength();
}

// Returns version.
const std::string &Response::getVersion() const
{
    return _version;
}

// Returns status code.
int Response::getStatusCode() const
{
    return _statusCode;
}

// Returns status message.
const std::string &Response::getStatusMessage() const
{
    return _statusMessage;
}

// Returns headers.
const Response::HeaderMap &Response::getHeaders() const
{
    return _headers;
}

// Returns body.
const std::string &Response::getBody() const
{
    return _body;
}

// Returns whether this response should close the connection.
bool Response::shouldCloseConnection() const
{
    return _closeConnection;
}

// Sets body.
void Response::setBody(const std::string &body)
{
    if (!statusMayHaveBody(_statusCode))
    {
        _body.clear();
        updateContentLength();
        return;
    }
    _body = body;
    updateContentLength();
}

// Adds body.
void Response::appendBody(const std::string &data)
{
    if (!statusMayHaveBody(_statusCode))
        return;
    _body += data;
    updateContentLength();
}

// Adds body.
void Response::appendBody(const char *data, size_t length)
{
    if (!statusMayHaveBody(_statusCode))
        return;
    if (data != NULL && length != 0)
        _body.append(data, length);
    updateContentLength();
}

// Clears body.
void Response::clearBody()
{
    _body.clear();
    updateContentLength();
}

// Clears body only.
void Response::clearBodyOnly()
{
    _body.clear();
}

// Sets close connection.
void Response::setCloseConnection(bool closeConnection)
{
    _closeConnection = closeConnection;
    updateConnectionHeader();
}

// Builds the full HTTP response text.
std::string Response::responseToString() const
{
    std::ostringstream output;
    output << _version << " " << _statusCode << " " << _statusMessage << "\r\n";
    HeaderMap::const_iterator it = _headers.begin();
    while (it != _headers.end())
    {
        output << it->first << ": " << it->second << "\r\n";
        ++it;
    }
    output << "\r\n";
    if (!_suppressBody)
        output.write(_body.data(), static_cast<std::streamsize>(_body.size()));
    return output.str();
}
