#ifndef REQUEST_HPP
#define REQUEST_HPP

#include <map>
#include <string>

class ServerConfig;
class RequestParser;

/*
枚举：RequestStatus
用途：统一表示 RequestParser 的全部解析结果，避免一部分状态放在 enum、另一部分依赖全局宏。
说明：RequestParser 只返回这些状态；调用方再把它们映射成 400、413、505 等 HTTP response。
*/
enum RequestStatus
{
    REQUEST_OK = 0,                        /* 请求完整且合法，Request 已填好。 */
    REQUEST_INCOMPLETE = 1,                /* 当前 buffer 还不够，继续等待数据。 */
    REQUEST_ERROR = -1,                    /* 请求语法或消息边界非法，通常对应 400。 */
    REQUEST_VERSION_NOT_SUPPORTED = -2,    /* version 语法正确但不是 HTTP/1.1，通常对应 505。 */
    REQUEST_BODY_TOO_LARGE = -3            /* body 长度合法但超过有效限制，通常对应 413。 */
};

/*
类：Request
用途：只读保存一次 HTTP request 的解析结果。
填充位置：只有友元 RequestParser 能重置和写入私有字段。
读取位置：后续模块通过 const getter 读取 method、URI、path、query、headers、body 和 config。
封装边界：Request 只保存请求数据与当前配置上下文；socket 生命周期、keep-alive 和 response 不属于本类。
*/
class Request
{
public:
    /* HeaderMap：header name 已统一为 ASCII 小写。 */
    typedef std::map<std::string, std::string> HeaderMap;

private:
    std::string _method;      /* request-line 第一段，例如 GET。 */
    std::string _uri;         /* 原始 request-target，例如 /a%20b?x=1。 */
    std::string _path;        /* percent-decoding 与 normalization 后的安全 path。 */
    std::string _query;       /* 原始 query，不包含问号，不在 RequestParser 中解码。 */
    std::string _version;     /* request-line 第三段；当前只接受 HTTP/1.1。 */
    HeaderMap _headers;       /* 已验证、key 已转成小写的 headers。 */
    std::string _body;        /* 按 Content-Length 或 chunked 还原的二进制安全 body。 */
    const ServerConfig *_config; /* 非拥有型配置指针，Request 不负责 delete。 */

    /*
    函数：toLowerAscii
    用途：把 HTTP header 名或 transfer-coding 中的 ASCII 大写字母转成小写。
    访问限制：只供 Request 自身与友元 RequestParser 使用，不暴露全局字符串工具。
    */
    static std::string toLowerAscii(const std::string &value);

    /*
    函数：resetForParsing
    用途：解析新 buffer 前清空旧结果，并保存调用方传入的 ServerConfig。
    */
    void resetForParsing(const ServerConfig *server);

    friend class RequestParser;

public:
    /* 构造函数：创建空 Request，config 初始为 NULL。 */
    Request();

    /* 以下接口只读返回解析结果。编译器生成的拷贝构造、赋值和析构已足够安全。 */
    const std::string &getMethod() const;
    const std::string &getUri() const;
    const std::string &getPath() const;
    const std::string &getQuery() const;
    const std::string &getVersion() const;
    const HeaderMap &getHeaders() const;
    const std::string &getBody() const;
    const ServerConfig *getConfig() const;

    /*
    函数：getHeader
    用途：大小写不敏感地读取单个 header。
    返回：找到时写入 value 并返回 true；不存在时清空 value 并返回 false。
    */
    bool getHeader(const std::string &name, std::string &value) const;
};

#endif