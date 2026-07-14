#ifndef REQUEST_HPP
#define REQUEST_HPP

#include <map>
#include <string>
#include <cstddef>

class ServerConfig;
class RequestParser;

/*
兼容说明：ERROR_MAX_BODY_LENGTH 是 RequestParser 返回给 ServerManager 的特殊状态。
有些合并版本的 Defines.hpp 里还没有这个宏；这里做兜底定义，保证 Request 对外接口自洽。
*/
#ifndef ERROR_MAX_BODY_LENGTH
#define ERROR_MAX_BODY_LENGTH -42
#endif

/*
枚举：RequestStatus
作用：让 ServerManager 明确知道当前 client buffer 的解析状态。
*/
enum RequestStatus
{
    REQUEST_OK = 0,          /* 请求完整且合法，Request 已填好，可以进入 Response。 */
    REQUEST_INCOMPLETE = 1,  /* 请求尚未收完整，调用方继续等待 POLLIN。 */
    REQUEST_ERROR = -1       /* 请求格式非法，调用方通常生成 400 并关闭连接。 */
};

/*
类：Request
用途：以只读接口保存一次 HTTP request 的解析结果。
创建位置：ServerManager 在准备解析当前 client buffer 时创建或复用 Request 对象。
填充位置：只有友元 RequestParser 能重置并写入 method、uri、version、headers、body 和 config。
读取位置：Response、CGI、RequestHandler 和 ServerManager 通过 const getter 读取解析结果。
封装目的：
    1. HTTP 报文解析完成后，普通业务模块不能直接改写 method、uri、headers 或 body。
    2. 初始化和重复解析前的清理集中在构造函数与 resetForParsing() 中。
    3. _config 明确为非拥有型指针，Request 不负责 delete。
    4. 只为连接管理保留受控的 setCloseConnection() 修改入口。
*/
class Request
{
public:
    /* HeaderMap：统一表示已经把 header name 转成小写后的请求头表。 */
    typedef std::map<std::string, std::string> HeaderMap;

private:
    /* 请求行第一段，例如 GET、POST、DELETE。 */
    std::string _method;
    /* 请求行第二段，例如 /index.html 或 /search?q=test。 */
    std::string _uri;
    /* 请求行第三段；当前 parser 只接受 HTTP/1.1。 */
    std::string _version;
    /* 已验证并转成小写 key 的请求头。 */
    HeaderMap _headers;
    /* 已按 Content-Length 或 chunked framing 还原的二进制安全 body。 */
    std::string _body;
    /*
    当前 client 初步关联的 ServerConfig。
    这是非拥有型指针：配置对象由 Config/ServerManager 管理，Request 不 delete。
    ServerConfig 的生命周期必须长于本次 Request 的使用期。
    */
    const ServerConfig *_config;
    /* 响应发送完成后是否关闭 client；parser 先设为安全默认 true。 */
    bool _closeConnection;

    /*
    函数：resetForParsing
    用途：每次解析新 buffer 前清空上一次请求数据，并保存当前 server 上下文。
    参数来源：server 由 ServerManager 调用 RequestParser::parseBuffer() 时传入。
    访问限制：只有 RequestParser 作为友元可以调用，其他模块不能伪造解析结果。
    */
    void resetForParsing(const ServerConfig *server);

    /* RequestParser 需要写入私有解析字段，因此声明为友元类。 */
    friend class RequestParser;

public:
    /* 构造函数：创建空 Request；config 为 NULL，默认响应后关闭连接。 */
    Request();
    /* 拷贝构造：复制全部解析数据和非拥有型 config 指针。 */
    Request(const Request &src);
    /* 赋值运算符：复制全部解析数据和连接策略，不取得 config 所有权。 */
    Request &operator=(const Request &rhs);
    /* 析构函数：string/map 自动释放；不释放非拥有型 _config。 */
    ~Request();

    /* 以下 getter 全部返回只读结果，外部模块不能修改 Request 内部字段。 */
    const std::string &getMethod() const;
    const std::string &getUri() const;
    const std::string &getVersion() const;
    const HeaderMap &getHeaders() const;
    const std::string &getBody() const;
    const ServerConfig *getConfig() const;

    /*
    函数：getHeader
    用途：大小写不敏感地读取单个 header。
    参数：name 是调用者要查询的 header 名；value 接收找到的值。
    返回值：存在时返回 true 并写入 value；不存在返回 false，value 清空。
    */
    bool getHeader(const std::string &name, std::string &value) const;

    /* 返回响应发送完成后是否应关闭 client 连接。 */
    bool shouldCloseConnection() const;
    /* 仅允许连接管理层在解析后更新最终 close/keep-alive 策略。 */
    void setCloseConnection(bool closeConnection);
};

/*
函数：to_lower
用途：返回输入字符串的小写副本。
说明：这是 Request 模块共享的无状态字符串工具，CGI、Response 和 ServerManager 也可能复用。
*/
std::string to_lower(const std::string &str);

#endif