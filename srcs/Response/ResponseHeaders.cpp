/*
文件：srcs/Response/ResponseHeaders.cpp
用途：实现响应 header 的读取、设置、删除、名称规范化、合法性验证和受控 header 保护。
拆分说明：Content-Length 和 Connection 仍只能由 Response 内部维护；连接 token 解析独立放入 ResponseConnection.cpp。
*/
#include "Response.hpp"

/*
函数：Response::getHeader
用途：以大小写不敏感方式读取一个响应头。
参数来源：name 由 ServerManager、测试或调用方提供；value 是调用方准备接收结果的输出变量。
变量说明：
    - it：在 canonical header map 中查找后的迭代器。
实现逻辑：
    1. canonicalHeaderName() 统一 name。
    2. 不存在时清空 value 并返回 false。
    3. 存在时复制 header value 并返回 true。
*/
bool Response::getHeader(const std::string &name, std::string &value) const
{
    HeaderMap::const_iterator it = _headers.find(canonicalHeaderName(name));
    if (it == _headers.end())
    {
        value.clear();
        return false;
    }
    value = it->second;
    return true;
}


/*
函数：Response::setHeader
用途：设置普通响应头，同时阻止非法 header 和外部覆盖受控字段。
参数来源：name/value 来自 redirect、MIME 判断、CGI 适配或业务处理函数。
变量说明：无额外局部变量。
实现逻辑：
    1. 校验 header name 是 HTTP token。
    2. 校验 value 不含 CR/LF、非法控制字符或 DEL。
    3. 拒绝 Content-Length 和 Connection，它们只能由 Response 内部维护。
    4. 统一名称大小写后写入 map；同名 header 会覆盖而不会重复。
*/
void Response::setHeader(const std::string &name, const std::string &value)
{
    if (!isValidHeaderName(name) || !isValidHeaderValue(value)
        || isManagedHeader(name))
        return;
    _headers[canonicalHeaderName(name)] = value;
}

/*
函数：Response::removeHeader
用途：删除普通响应头，但保护 Content-Length 和 Connection。
参数来源：name 由状态切换或业务逻辑提供。
变量说明：无局部变量。
实现逻辑：受控 header 直接忽略；其他名称规范化后从 map 删除。
*/
void Response::removeHeader(const std::string &name)
{
    if (isManagedHeader(name))
        return;
    _headers.erase(canonicalHeaderName(name));
}


/*
函数：Response::toLowerAscii
用途：只转换 ASCII A-Z，供大小写不敏感的 header/token 比较。
参数来源：value 来自 header name 或 Connection token。
变量说明：result 是可修改副本；i 是字符下标。
实现逻辑：遍历全部字符，只把 A-Z 加偏移改为 a-z，其他字节保持不变。
*/
std::string Response::toLowerAscii(const std::string &value)
{
    std::string result = value;
    size_t i = 0;
    while (i < result.size())
    {
        if (result[i] >= 'A' && result[i] <= 'Z')
            result[i] = static_cast<char>(result[i] - 'A' + 'a');
        ++i;
    }
    return result;
}

/*
函数：Response::canonicalHeaderName
用途：把任意大小写 header name 统一成 Content-Type 形式的 map key。
参数来源：name 来自 setHeader/getHeader/removeHeader 或 CGI header。
变量说明：result 是先全小写后的副本；uppercaseNext 表示当前字符是否位于开头或连字符后；i 是下标。
实现逻辑：先转小写，再把首字母及每个 '-' 后的字母转为大写。
*/
std::string Response::canonicalHeaderName(const std::string &name)
{
    std::string result = toLowerAscii(name);
    bool uppercaseNext = true;
    size_t i = 0;
    while (i < result.size())
    {
        if (uppercaseNext && result[i] >= 'a' && result[i] <= 'z')
            result[i] = static_cast<char>(result[i] - 'a' + 'A');
        uppercaseNext = result[i] == '-';
        ++i;
    }
    return result;
}

/*
函数：Response::isManagedHeader
用途：识别只能由 Response 内部维护的消息边界/连接 headers。
参数来源：name 来自 setHeader/removeHeader 或 CGI 导入。
变量说明：lower 是 ASCII 小写名称。
实现逻辑：Content-Length 或 Connection 返回 true，其余 false。
*/
bool Response::isManagedHeader(const std::string &name)
{
    std::string lower = toLowerAscii(name);
    return lower == "content-length" || lower == "connection";
}

/*
函数：Response::isValidHeaderName
用途：验证 response header name 是否为合法 HTTP token，防止坏格式和 header 注入。
参数来源：name 来自业务代码或 CGI response。
变量说明：symbols 是 token 允许的特殊字符；i/c 用于逐字节检查。
实现逻辑：空名称拒绝；每个字符必须是字母、数字或 symbols 中的字符。
*/
bool Response::isValidHeaderName(const std::string &name)
{
    if (name.empty())
        return false;
    const std::string symbols("!#$%&'*+-.^_`|~");
    size_t i = 0;
    while (i < name.size())
    {
        unsigned char c = static_cast<unsigned char>(name[i]);
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z')
            || (c >= 'a' && c <= 'z') || symbols.find(c) != std::string::npos))
            return false;
        ++i;
    }
    return true;
}

/*
函数：Response::isValidHeaderValue
用途：拒绝 response header value 中的 CR/LF、非法控制字符和 DEL。
参数来源：value 来自业务代码或 CGI header。
变量说明：i/c 用于逐字节检查。
实现逻辑：允许可见字节和 tab；其他 0-31 控制字符及 127 返回 false。
*/
bool Response::isValidHeaderValue(const std::string &value)
{
    size_t i = 0;
    while (i < value.size())
    {
        unsigned char c = static_cast<unsigned char>(value[i]);
        if ((c < 32 && c != '\t') || c == 127)
            return false;
        ++i;
    }
    return true;
}

/*
函数：Response::setManagedHeader
用途：供 Response 内部写入 Content-Length 或 Connection，绕过公开接口的保护。
参数来源：name/value 只来自 updateContentLength()、updateConnectionHeader() 和内部适配流程。
变量说明：无局部变量。
实现逻辑：规范化名称后直接写入 _headers。
*/
void Response::setManagedHeader(const std::string &name,
                                const std::string &value)
{
    _headers[canonicalHeaderName(name)] = value;
}

