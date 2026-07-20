#ifndef RESPONSE_HPP
#define RESPONSE_HPP

#include <map>
#include <string>

class Request;

/*
类：Response
用途：封装一次已经完成的 HTTP/1.1 响应，包括状态行、headers、二进制 body 和连接关闭策略。
数据来源：普通请求由 buildResponse() 生成；异步 CGI 完成后，现有 ServerManager 调用 parseCgiOutput() 把脚本 stdout 写入 Response。
使用位置：普通请求由 ServerManager 直接接收 buildResponse() 的返回值；CGI 完成后由 ServerManager 调用 parseCgiOutput()，最后统一使用 responseToString() 取得发送字节。
封装边界：Content-Length 和 Connection 只能由 Response 内部维护；X-Internal-CGI-Path 仅为兼容现有 ServerManager 的内部交接 header，必须在真正发送前被 ServerManager 截获。
*/
class Response
{
public:
    typedef std::map<std::string, std::string> HeaderMap;
    typedef std::map<int, std::string> ErrorPageMap;

private:
    std::string _version;          /* 状态行版本，当前固定为 HTTP/1.1。 */
    int _statusCode;               /* HTTP 状态码，默认是合法的 200。 */
    std::string _statusMessage;    /* 与状态码对应的 reason phrase。 */
    HeaderMap _headers;            /* 名称经 canonicalHeaderName() 统一后的响应头。 */
    std::string _body;             /* 按明确长度保存的二进制安全响应体。 */
    bool _closeConnection;         /* 发送完本响应后是否关闭 client fd。 */

    static std::string statusMessageFor(int statusCode);
    static bool isErrorStatusCode(int statusCode);
    static bool statusMayHaveBody(int statusCode);
    static std::string sizeToString(size_t value);
    static std::string toLowerAscii(const std::string &value);
    static std::string canonicalHeaderName(const std::string &name);
    static bool isManagedHeader(const std::string &name);
    static bool isValidHeaderName(const std::string &name);
    static bool isValidHeaderValue(const std::string &value);
    static bool requestWantsClose(const Request &request);

    void setManagedHeader(const std::string &name,
                          const std::string &value);
    void updateContentLength();
    void updateConnectionHeader();
    bool loadCustomErrorPage(const ErrorPageMap &errorPages);
    bool readOpenedFileIntoBody(int fd);
    void setDefaultErrorPage();

    /*
    函数：loadCgiOutput
    用途：把异步 CgiHandler 管道收集到的原始 CGI stdout 安全装入当前 Response。
    参数来源：cgiOutput 由 ServerManager 从 CGI 输出管道非阻塞读取并在 EOF 后汇总。
    变量含义：函数内部会区分 CGI header block、Status 特殊头、普通透传头和二进制 body。
    实现逻辑：
        1. 空输出返回 false；没有 header/body 分隔符时按纯 body 的 200 HTML 处理。
        2. 有分隔符时逐行验证 CGI headers，并把 Status: nnn reason 转为真正状态码。
        3. 忽略 Content-Length、Transfer-Encoding 和 Connection，避免 CGI 破坏服务器消息边界与连接策略。
        4. 对合法普通 headers 进行大小写规范化；最后按状态规则保存 body。
        5. 根据真实 body.size() 重算 Content-Length，并继承原 Request 的 Connection 策略。
    */
    bool loadCgiOutput(const std::string &cgiOutput);

    /* buildCgiResponse() 需要调用私有 CGI 输出适配器。 */
    friend Response buildCgiResponse(const Request &request,
                                     const std::string &cgiOutput);

public:
    explicit Response(bool closeConnection = false);
    explicit Response(const Request &request);

    const std::string &getVersion() const;
    int getStatusCode() const;
    const std::string &getStatusMessage() const;
    const HeaderMap &getHeaders() const;
    const std::string &getBody() const;
    bool shouldCloseConnection() const;

    bool getHeader(const std::string &name, std::string &value) const;
    void setStatus(int statusCode);
    void setHeader(const std::string &name, const std::string &value);
    void removeHeader(const std::string &name);
    void setBody(const std::string &body);
    void appendBody(const std::string &data);
    void appendBody(const char *data, size_t length);
    void clearBody();
    void setCloseConnection(bool closeConnection);
    void createResponse(unsigned int code, const std::string &bodyText,
                        const ErrorPageMap &errorPages);

    /*
    函数：parseCgiOutput
    用途：保留 ServerManager 已经使用的公开接口，把异步 CGI 管道收集到的原始 stdout 转换为当前 Response。
    参数来源：cgiOutput 由 ServerManager 在 CGI read fd 读到 EOF 后，从连接或 CGI 任务的输出缓冲区传入。
    变量说明：本函数不要求 ServerManager 改成新的返回类型；解析工作继续交给私有 loadCgiOutput()。
    实现逻辑：
        1. 调用 loadCgiOutput() 解析 Status、普通 headers 和二进制 body。
        2. 解析成功时，当前对象已经更新 Content-Length 和 Connection，直接返回。
        3. 空输出或畸形输出时生成 502 Bad Gateway，并强制关闭连接。
        4. 解析前后的内部 CGI 路径 header 会被清除，不会进入最终客户端响应。
    */
    void parseCgiOutput(const std::string &cgiOutput);

    std::string responseToString() const;
};

/*
函数：buildResponse
用途：保留 ServerManager 原来使用的 Response 返回接口，完成配置合并、重定向、方法权限、路径检查和普通请求处理。
参数来源：request 由 RequestParser 完整解析后由 ServerManager 传入。
返回规则：
    - 普通请求直接返回最终 Response。
    - CGI 请求返回带有 X-Internal-CGI-Path 的内部占位 Response，供现有 ServerManager 截获并启动异步 CGI。
接口兼容：返回类型继续是 Response，不要求 ServerManager 改用 新的分发结果类型。
*/
Response buildResponse(const Request &request);

/*
函数：buildCgiResponse
用途：在异步 CGI stdout 全部读取完成后，把原始输出转换成最终可发送的 Response。
参数来源：request 是启动 CGI 时保存的原请求；cgiOutput 是从 CGI 输出管道收集的完整字节串。
实现逻辑：创建继承原请求连接策略的 Response，调用私有 loadCgiOutput()；空或畸形输出统一生成 502 并关闭连接。
*/
Response buildCgiResponse(const Request &request,
                          const std::string &cgiOutput);

#endif
