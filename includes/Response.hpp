#ifndef RESPONSE_HPP
#define RESPONSE_HPP

#include <map>
#include <string>

class Request;

/*
类：Response
用途：封装一次 HTTP response 的状态行、headers、body 和连接关闭策略。
填充位置：buildResponse()、GET/POST/DELETE 处理函数通过公开成员函数写入。
读取位置：ServerManager 通过 getter 或 responseToString() 取得最终响应数据。
封装边界：外部不能直接修改成员；Content-Length 和 Connection 只能由 Response 自己维护，避免 header 与真实 body/连接策略不一致。
*/
class Response
{
public:
    typedef std::map<std::string, std::string> HeaderMap;
    typedef std::map<int, std::string> ErrorPageMap;

private:
    std::string _version;          /* 状态行使用的 HTTP 版本，固定为 HTTP/1.1。 */
    int _statusCode;               /* HTTP 状态码；默认是可直接序列化的 200。 */
    std::string _statusMessage;    /* 与状态码对应的标准短语。 */
    HeaderMap _headers;            /* 使用规范化名称保存的 response headers。 */
    std::string _body;             /* 二进制安全的 response body。 */
    bool _closeConnection;         /* response 发送完成后是否关闭连接。 */

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

public:
    /* 构造函数：创建可直接序列化的 HTTP/1.1 200 OK 空响应。 */
    explicit Response(bool closeConnection = false);
    explicit Response(const Request &request);

    const std::string &getVersion() const;
    int getStatusCode() const;
    const std::string &getStatusMessage() const;
    const HeaderMap &getHeaders() const;
    const std::string &getBody() const;
    bool shouldCloseConnection() const;

    /* header name 大小写不敏感；不存在时清空 value 并返回 false。 */
    bool getHeader(const std::string &name, std::string &value) const;

    /* 设置状态码并同步状态短语；1xx、204、304 会自动清除 body。 */
    void setStatus(int statusCode);
    /*
    设置普通 response header；名称大小写会统一。
    Content-Length 和 Connection 属于受控 header，外部调用不会覆盖它们。
    含非法 token、CR/LF 或控制字符的 header 会被忽略。
    */
    void setHeader(const std::string &name, const std::string &value);
    /* 删除普通 header；受控 header 不能通过此接口删除。 */
    void removeHeader(const std::string &name);
    /* 替换或追加 body；禁止 body 的状态码会保持空 body。 */
    void setBody(const std::string &body);
    void appendBody(const std::string &data);
    void appendBody(const char *data, size_t length);
    void clearBody();
    /* 修改连接策略，并自动同步 Connection header。 */
    void setCloseConnection(bool closeConnection);

    /*
    函数：createResponse
    用途：生成标准成功或错误响应；错误状态优先加载自定义错误页，失败时生成默认 HTML 错误页。
    */
    void createResponse(unsigned int code, const std::string &bodyText,
                        const ErrorPageMap &errorPages);

    /* 把状态行、headers、空行和二进制 body 序列化为完整 HTTP 文本。 */
    std::string responseToString() const;
};

/* 把已经通过 RequestParser 的 Request 分发成普通 HTTP Response。 */
Response buildResponse(const Request &request);

#endif
