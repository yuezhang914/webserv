/*
文件：srcs/Response/ResponseCgi.cpp
用途：把异步 CGI 收集到的 stdout 转换为最终 Response，并处理 CGI 输出格式错误时的 502 响应。
拆分说明：CGI 解析和构造函数从原 Response.cpp 原样移动，不执行 CGI、不改变 ServerManager 现有接口。
*/
#include "Response.hpp"
#include "Request.hpp"
#include "ServerConfig.hpp"
#include "ResponseInternal.hpp"

#include <sstream>

/*
函数：Response::loadCgiOutput
用途：把异步 CGI 管道收集到的原始 stdout 解析成当前 Response 的状态、普通 headers 和 body。
参数来源：cgiOutput 由 ServerManager 对 CgiFds.read_fd 执行非阻塞 read()，直到 EOF 后汇总得到。
变量说明：
    - delimiterPos/delimiterLength：CGI headers 与 body 的分隔位置和长度。
    - headerBlock/body：分离后的 header 文本和二进制 body。
    - importedHeaders：暂存已验证且允许透传的 CGI headers。
    - importedStatus/hasStatus：Status 特殊头解析出的状态码及是否已经出现。
    - cursor/lineEnd/line/colon/name/value：逐行解析 header 的边界和字段。
    - lowerName/statusStream/reason：大小写判断及 Status value 解析工具。
实现逻辑：
    1. 空输出直接失败；没有空行分隔时把全部输出当作 200 text/html body。
    2. 有 header block 时逐行要求 name:value 结构，并校验 header name/value。
    3. Status 只能出现一次，状态码必须在 100 到 599；reason phrase 只做格式消费，最终由 Response 的统一映射生成。
    4. Content-Length、Transfer-Encoding、Connection 属于服务器管理字段，全部忽略；其他合法 header 暂存并规范化名称。
    5. 清空旧响应数据，恢复解析出的状态；允许 body 时导入 headers 和 body，没有 Content-Type/Location 时默认 text/html。
    6. 对 1xx、204、304 自动丢弃 body 和 Content-Type；最后按真实 body 长度更新 Content-Length，并保留原 Request 的连接策略。
*/
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
            if (!(statusStream >> importedStatus)
                || importedStatus < 100 || importedStatus > 599)
                return false;
            std::string reason;
            std::getline(statusStream, reason);
            reason = responseTrimOws(reason);
            hasStatus = true;
        }
        else if (lowerName != "content-length"
            && lowerName != "transfer-encoding"
            && lowerName != "connection")
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
        if (statusMayHaveBody(_statusCode)
            || toLowerAscii(it->first) != "content-type")
            _headers[it->first] = it->second;
        ++it;
    }

    if (statusMayHaveBody(_statusCode))
    {
        if (_headers.find("Content-Type") == _headers.end()
            && _headers.find("Location") == _headers.end())
            _headers["Content-Type"] = "text/html";
        _body = body;
    }
    else
        _headers.erase("Content-Type");

    updateConnectionHeader();
    updateContentLength();
    return true;
}

/*
函数：Response::parseCgiOutput
用途：保留 ServerManager 已经调用的公开成员接口，把完整 CGI stdout 解析并覆盖当前 Response。
参数来源：cgiOutput 由 ServerManager 从 CGI 输出管道累计到连接或 CGI 任务缓冲区后传入。
变量说明：
    - noErrorPages：当成员函数没有 Request/ServerConfig 可读取时，用于生成 502 的安全空错误页映射。
实现逻辑：
    1. 调用私有 loadCgiOutput() 完成 Status、普通 headers 和二进制 body 的严格解析。
    2. 成功时 loadCgiOutput() 已清空旧内部 header，并按真实 body 更新 Content-Length。
    3. 失败时使用空错误页映射生成 502 Bad Gateway。
    4. 最后强制 Connection: close，避免复用 CGI 输出已经异常的连接。
*/
void Response::parseCgiOutput(const std::string &cgiOutput)
{
    if (loadCgiOutput(cgiOutput))
        return;

    ErrorPageMap noErrorPages;
    createResponse(502, "Invalid CGI response", noErrorPages);
    setCloseConnection(true);
}

/*
函数：buildCgiResponse
用途：在异步 CGI stdout 读取完成后，构造最终可发送的 Response。
参数来源：
    - request：ServerManager 启动 CGI 时保存的原请求，用于继承 Connection 策略和读取 ServerConfig 错误页。
    - cgiOutput：从 CgiFds.read_fd 收集的完整原始 stdout，不包含服务器预制 HTTP 状态行。
变量说明：
    - response：以原 Request 构造的目标响应。
    - server：Request 持有的 ServerConfig 指针，用于 502 自定义错误页。
    - noErrorPages：Request 缺少配置时的安全空映射。
实现逻辑：
    1. 创建继承 request close 策略的 Response。
    2. 调用私有 loadCgiOutput() 解析 CGI stdout；成功时直接返回。
    3. 空输出或格式错误时生成 502 Bad Gateway。
    4. 502 后显式关闭连接，避免客户端继续复用一个 CGI 状态不确定的连接。
*/
Response buildCgiResponse(const Request &request,
                          const std::string &cgiOutput)
{
    Response response(request);
    if (response.loadCgiOutput(cgiOutput))
        return response;

    const ServerConfig *server = request.getConfig();
    if (server != NULL)
        response.createResponse(502, "Invalid CGI response",
            server->error_pages);
    else
    {
        Response::ErrorPageMap noErrorPages;
        response.createResponse(502, "Invalid CGI response",
            noErrorPages);
    }
    response.setCloseConnection(true);
    return response;
}
