/*
文件：includes/Response.hpp
用途：声明 HTTP Response 对象、受控 header/body 接口、CGI stdout 适配接口，以及唯一的带共享 SessionStore 的顶层响应入口。
模块边界：Response 负责生成最终 HTTP 数据；Session 数据仍由 ServerManager 长期持有的 SessionStore 保存。
拆分说明：类声明集中在本文件，成员函数分别实现在 Response、Headers、Status、Connection、Error 和 Cgi 源文件中。
*/
#ifndef RESPONSE_HPP
#define RESPONSE_HPP

/*
包含：<cstddef>
用途：使用 size_t 表示响应体长度和原始字节追加长度。
*/
#include <cstddef>

/*
包含：<map>
用途：使用 std::map 保存规范化后的响应 headers，以及状态码到自定义错误页路径的映射。
*/
#include <map>

/*
包含：<string>
用途：保存 HTTP 版本、状态文本、header、二进制安全 body 和最终序列化结果。
*/
#include <string>

class Request;
class SessionStore;

/*
类：Response
用途：封装一次已经完成的 HTTP/1.1 响应，包括状态行、headers、二进制 body 和连接关闭策略。
数据来源：普通请求由 buildResponse() 生成；异步 CGI 完成后，ServerManager 调用 parseCgiOutput() 写入脚本 stdout。
使用位置：ServerManager 保存该对象并通过 responseToString() 取得最终发送字节。
封装边界：Content-Length 和 Connection 只能由 Response 内部维护；X-Internal-CGI-Path 只用于 CGI 启动前的内部交接。
*/
class Response
{
public:
    /* 类型：HeaderMap；用途：保存规范化后的唯一响应头名称和值。 */
    typedef std::map<std::string, std::string> HeaderMap;

    /* 类型：ErrorPageMap；用途：保存状态码到自定义错误页文件路径的配置映射。 */
    typedef std::map<int, std::string> ErrorPageMap;

private:
    std::string _version;       /* HTTP 版本，当前固定为 HTTP/1.1。 */
    int _statusCode;            /* HTTP 状态码，默认 200。 */
    std::string _statusMessage; /* 与状态码对应的 reason phrase。 */
    HeaderMap _headers;         /* 名称经过 canonicalHeaderName() 统一的响应头。 */
    std::string _body;          /* 按明确长度保存的二进制安全响应体。 */
    bool _closeConnection;      /* 发送本响应后是否关闭客户端连接。 */

    /*
    函数：statusMessageFor
    用途：把状态码映射为本项目支持的标准 reason phrase。
    参数：statusCode 来自 setStatus()、错误处理或 CGI Status header。
    返回：已知状态码返回对应文本，未知状态码返回空字符串。
    */
    static std::string statusMessageFor(int statusCode);

    /*
    函数：isErrorStatusCode
    用途：判断状态码是否属于 4xx 或 5xx 错误范围。
    参数：statusCode 是调用方准备设置的状态码。
    返回：错误状态返回 true，否则返回 false。
    */
    static bool isErrorStatusCode(int statusCode);

    /*
    函数：statusMayHaveBody
    用途：判断当前状态是否允许携带响应体。
    参数：statusCode 来自当前 Response。
    返回：1xx、204 和 304 返回 false，其他状态返回 true。
    */
    static bool statusMayHaveBody(int statusCode);

    /*
    函数：sizeToString
    用途：把 body.size() 转为 Content-Length 使用的十进制文本。
    参数：value 是字符串或字节缓冲区长度。
    返回：十进制字符串。
    */
    static std::string sizeToString(size_t value);

    /*
    函数：toLowerAscii
    用途：生成只转换 ASCII A-Z 的小写副本，用于大小写不敏感的 header 比较。
    参数：value 是 header 名或 token。
    返回：转换后的字符串。
    */
    static std::string toLowerAscii(const std::string &value);

    /*
    函数：canonicalHeaderName
    用途：把合法 header name 转成稳定的 Title-Case key。
    参数：name 来自 setHeader()、getHeader() 或 CGI 输出。
    返回：规范化名称；调用前仍需执行合法性检查。
    */
    static std::string canonicalHeaderName(const std::string &name);

    /*
    函数：isManagedHeader
    用途：判断 header 是否由 Response 内部独占维护。
    参数：name 是待写入或删除的 header 名。
    返回：Content-Length、Connection 等受控字段返回 true。
    */
    static bool isManagedHeader(const std::string &name);

    /*
    函数：isValidHeaderName
    用途：验证响应头名称是否符合受支持的 HTTP token 字符集。
    参数：name 来自项目代码或 CGI 输出。
    返回：合法返回 true，空值、控制字符或分隔符返回 false。
    */
    static bool isValidHeaderName(const std::string &name);

    /*
    函数：isValidHeaderValue
    用途：拒绝 CR/LF 和控制字符，防止响应头注入。
    参数：value 是待写入的响应头值。
    返回：合法返回 true，否则返回 false。
    */
    static bool isValidHeaderValue(const std::string &value);

    /*
    函数：requestWantsClose
    用途：根据 HTTP 版本与 Connection token 判断请求完成后是否关闭连接。
    参数：request 由 RequestParser 完整解析。
    返回：应关闭连接时返回 true。
    */
    static bool requestWantsClose(const Request &request);

    /*
    函数：setManagedHeader
    用途：允许 Response 内部安全写入 Content-Length、Connection 等受控字段。
    参数：name/value 由内部状态同步函数生成。
    实现逻辑：绕过公共 setHeader() 的受控字段拒绝，但仍使用规范化 key。
    */
    void setManagedHeader(const std::string &name,
                          const std::string &value);

    /*
    函数：updateContentLength
    用途：根据状态码和真实 body 长度同步 Content-Length。
    参数：无，读取当前 _statusCode 与 _body。
    实现逻辑：禁止 body 的状态删除该字段，其他状态写入 _body.size()。
    */
    void updateContentLength();

    /*
    函数：updateConnectionHeader
    用途：根据 _closeConnection 同步 Connection header。
    参数：无。
    实现逻辑：关闭时写 close，否则写 keep-alive。
    */
    void updateConnectionHeader();

    /*
    函数：loadCustomErrorPage
    用途：尝试读取当前状态码对应的自定义错误页。
    参数：errorPages 来自当前 ServerConfig。
    返回：成功装入 body 返回 true，否则返回 false。
    */
    bool loadCustomErrorPage(const ErrorPageMap &errorPages);

    /*
    函数：readOpenedFileIntoBody
    用途：从已经打开的普通文件描述符读取完整内容到 body。
    参数：fd 由错误页读取逻辑打开并传入。
    返回：读取成功返回 true，失败返回 false。
    */
    bool readOpenedFileIntoBody(int fd);

    /*
    函数：setDefaultErrorPage
    用途：在没有可用自定义错误页时生成最小默认 HTML。
    参数：无，读取当前状态码和状态文本。
    实现逻辑：生成 HTML、设置 Content-Type，并同步 body 长度。
    */
    void setDefaultErrorPage();

    /*
    函数：loadCgiOutput
    用途：把异步 CGI 管道收集到的原始 stdout 安全装入当前 Response。
    参数：cgiOutput 由 ServerManager 从 CGI 输出管道读取并在 EOF 后汇总。
    返回：格式合法时返回 true，空输出或畸形 header 返回 false。
    实现逻辑：解析 Status 和普通 headers，忽略 CGI 对消息边界字段的覆盖，保存二进制 body 并重算长度。
    */
    bool loadCgiOutput(const std::string &cgiOutput);

    /* buildCgiResponse() 需要调用私有 CGI 输出适配器。 */
    friend Response buildCgiResponse(const Request &request,
                                     const std::string &cgiOutput);

public:
    /*
    函数：Response(bool)
    用途：创建默认 200 响应并设置初始连接策略。
    参数：closeConnection 由调用方传入，默认 false。
    实现逻辑：初始化状态、空 body，并同步 Content-Length 和 Connection。
    */
    explicit Response(bool closeConnection = false);

    /*
    函数：Response(Request)
    用途：创建继承当前请求连接策略的默认响应。
    参数：request 由 RequestParser 完整解析。
    实现逻辑：调用 requestWantsClose() 决定初始 Connection。
    */
    explicit Response(const Request &request);

    /* 函数：getVersion；用途：返回 HTTP 版本只读引用；参数：无。 */
    const std::string &getVersion() const;

    /* 函数：getStatusCode；用途：返回当前状态码；参数：无。 */
    int getStatusCode() const;

    /* 函数：getStatusMessage；用途：返回当前 reason phrase；参数：无。 */
    const std::string &getStatusMessage() const;

    /* 函数：getHeaders；用途：返回响应头只读映射；参数：无。 */
    const HeaderMap &getHeaders() const;

    /* 函数：getBody；用途：返回二进制安全 body 只读引用；参数：无。 */
    const std::string &getBody() const;

    /* 函数：shouldCloseConnection；用途：返回发送后是否关闭连接；参数：无。 */
    bool shouldCloseConnection() const;

    /*
    函数：getHeader
    用途：大小写不敏感读取指定响应头。
    参数：name 是输入；value 是调用方提供的输出变量。
    返回：存在时复制值并返回 true，不存在时清空输出并返回 false。
    */
    bool getHeader(const std::string &name, std::string &value) const;

    /*
    函数：setStatus
    用途：设置状态码、状态文本并应用无 body 状态规则。
    参数：statusCode 由处理函数或 CGI 解析传入。
    实现逻辑：更新状态，必要时清空 body，并同步 Content-Length。
    */
    void setStatus(int statusCode);

    /*
    函数：setHeader
    用途：写入一个合法的普通响应头。
    参数：name/value 由处理函数或 CGI 解析传入。
    实现逻辑：拒绝非法名称、非法值和内部受控字段，再按规范化名称覆盖。
    */
    void setHeader(const std::string &name, const std::string &value);

    /*
    函数：removeHeader
    用途：删除一个普通响应头。
    参数：name 是调用方传入的逻辑 header 名。
    实现逻辑：受控字段不能由外部删除，其余按规范化名称移除。
    */
    void removeHeader(const std::string &name);

    /*
    函数：setBody
    用途：替换完整响应体。
    参数：body 是调用方传入的二进制安全字符串。
    实现逻辑：禁止 body 的状态保持为空，其他状态复制内容并重算长度。
    */
    void setBody(const std::string &body);

    /*
    函数：appendBody(string)
    用途：把完整字符串追加到响应体。
    参数：data 是调用方传入的二进制安全字符串。
    实现逻辑：按状态规则追加并同步 Content-Length。
    */
    void appendBody(const std::string &data);

    /*
    函数：appendBody(buffer,length)
    用途：按明确长度追加可能包含 NUL 的原始字节。
    参数：data 指向调用方缓冲区；length 是有效字节数。
    实现逻辑：空指针或零长度不处理，成功追加后同步长度。
    */
    void appendBody(const char *data, size_t length);

    /*
    函数：clearBody
    用途：清空响应体并同步 Content-Length。
    参数：无。
    */
    void clearBody();

    /*
    函数：setCloseConnection
    用途：设置发送完成后的连接关闭策略。
    参数：closeConnection 由 ServerManager 或错误处理传入。
    实现逻辑：更新布尔状态并同步 Connection header。
    */
    void setCloseConnection(bool closeConnection);

    /*
    函数：createResponse
    用途：生成指定状态的错误或普通响应，并优先使用自定义错误页。
    参数：code/bodyText 由失败分支传入；errorPages 来自 ServerConfig。
    实现逻辑：设置状态，错误状态尝试读取自定义页面，否则使用 bodyText 或默认错误页。
    */
    void createResponse(unsigned int code, const std::string &bodyText,
                        const ErrorPageMap &errorPages);

    /*
    函数：parseCgiOutput
    用途：保留 ServerManager 使用的公开接口，把完整 CGI stdout 转换为当前 Response。
    参数：cgiOutput 由 CGI 输出管道在 EOF 后汇总。
    实现逻辑：调用 loadCgiOutput()；失败生成 502 并强制关闭；内部 CGI 路径 header 不进入最终响应。
    */
    void parseCgiOutput(const std::string &cgiOutput);

    /*
    函数：responseToString
    用途：把状态行、headers、空行和二进制 body 序列化为发送缓冲区。
    参数：无。
    返回：可以交给 ServerManager 非阻塞 send/write 流程的完整字节串。
    */
    std::string responseToString() const;
};

/*
函数：buildResponse
用途：作为项目唯一的顶层 Response 入口，处理 Session 虚拟路由、配置合并、重定向、方法权限、CGI 和普通文件请求。
参数：request 由 RequestParser 完整解析；sessionStore 由 ServerManager 长期持有并在每次请求时传入。
返回规则：
    - /session/counter、/session/login、/session/logout 返回最终 Session 示例响应。
    - CGI 请求返回带 X-Internal-CGI-Path 的内部占位 Response，供 ServerManager 启动异步 CGI。
    - 其他请求返回静态文件、上传、删除、重定向或错误响应。
所有权：Response 不拥有 sessionStore，也不会创建隐藏全局 Store；所有请求必须使用同一个服务器级实例。
*/
Response buildResponse(const Request &request,
                       SessionStore &sessionStore);

/*
函数：buildCgiResponse
用途：在异步 CGI stdout 全部读取完成后，把原始输出转换成最终可发送的 Response。
参数：request 是启动 CGI 时保存的原请求；cgiOutput 是从 CGI 输出管道收集的完整字节串。
实现逻辑：创建继承请求连接策略的 Response，调用私有 CGI 适配器；空或畸形输出生成 502 并关闭连接。
*/
Response buildCgiResponse(const Request &request,
                          const std::string &cgiOutput);

#endif
