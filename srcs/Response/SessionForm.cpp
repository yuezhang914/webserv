/*
文件：srcs/Response/SessionForm.cpp
用途：解析 Session login 演示所需的 text/plain 和 application/x-www-form-urlencoded 请求正文。
模块边界：本文件只负责把 Request body 转成用户名，不创建 Session、不生成 Cookie，也不构造 Response。
安全边界：百分号编码必须完整；解码后拒绝 NUL、控制字符和 DEL；user 字段缺失或重复均返回 400。
*/

/*
包含：SessionResponseInternal.hpp
用途：取得 extractSessionLoginUserName() 的内部接口声明。
*/
#include "SessionResponseInternal.hpp"

/*
包含：Request.hpp
用途：读取 Content-Type header 和 RequestParser 已还原的二进制安全 body。
*/
#include "Request.hpp"

/*
包含：<string>
用途：处理媒体类型、表单字段、百分号编码和用户名输出。
*/
#include <string>

/*
函数：sessionFormToLowerAscii
用途：为 Content-Type 比较生成只转换 ASCII A-Z 的小写副本。
参数：value 来自 Request 的 Content-Type header。
变量：result 是可修改副本；i 是字符下标。
实现逻辑：逐字节把 A-Z 转成 a-z，其他字符原样保留，不受系统 locale 影响。
*/
static std::string sessionFormToLowerAscii(const std::string &value)
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
函数：sessionFormTrimOws
用途：删除 Content-Type 主值两端的空格与 tab。
参数：value 是调用方传入的字符串片段。
变量：begin/end 表示保留区间边界。
实现逻辑：从两端跳过 SP 和 HTAB，再返回中间子串。
*/
static std::string sessionFormTrimOws(const std::string &value)
{
    size_t begin = 0;
    while (begin < value.size()
        && (value[begin] == ' ' || value[begin] == '\t'))
        ++begin;
    size_t end = value.size();
    while (end > begin
        && (value[end - 1] == ' ' || value[end - 1] == '\t'))
        --end;
    return value.substr(begin, end - begin);
}

/*
函数：sessionFormHexValue
用途：把 application/x-www-form-urlencoded 百分号编码中的十六进制字符转成数值。
参数：c 来自 % 后的两个字符。
返回：合法时返回 0..15，其他字符返回 -1。
实现逻辑：分别处理数字、小写和大写十六进制字符。
*/
static int sessionFormHexValue(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

/*
函数：decodeSessionFormComponent
用途：解码一个表单字段，把 + 转为空格并处理 %HH，同时拒绝控制字符和 NUL。
参数：encoded 是按 & 和 = 分割后的字段；decoded 是调用方提供的输出字符串。
变量：i 是输入位置；high/low 是百分号编码的两个十六进制值；c 是解码字节。
实现逻辑：先在临时字符串中完成全部解码，任何错误清空输出并返回 false；整体成功后 swap 到输出。
*/
static bool decodeSessionFormComponent(const std::string &encoded,
                                       std::string &decoded)
{
    decoded.clear();
    std::string result;
    size_t i = 0;
    while (i < encoded.size())
    {
        unsigned char c = 0;
        if (encoded[i] == '+')
        {
            c = ' ';
            ++i;
        }
        else if (encoded[i] == '%')
        {
            if (i + 2 >= encoded.size())
                return false;
            int high = sessionFormHexValue(encoded[i + 1]);
            int low = sessionFormHexValue(encoded[i + 2]);
            if (high < 0 || low < 0)
                return false;
            c = static_cast<unsigned char>((high << 4) | low);
            i += 3;
        }
        else
        {
            c = static_cast<unsigned char>(encoded[i]);
            ++i;
        }
        if (c < 32 || c == 127)
            return false;
        result += static_cast<char>(c);
    }
    decoded.swap(result);
    return true;
}

/*
函数：extractSessionFormUser
用途：从 application/x-www-form-urlencoded body 中提取唯一的 user 参数。
参数：body 来自 Request::getBody()；userName 是调用方提供的输出变量。
变量：pos/end 遍历字段；equal 分割 name/value；decodedName/decodedValue 保存解码结果；found 防止重复 user。
实现逻辑：逐个解析 & 分隔字段，要求每项有 =；只接受一个名为 user 的字段，未知字段允许忽略，缺失或重复均失败。
*/
static bool extractSessionFormUser(const std::string &body,
                                   std::string &userName)
{
    userName.clear();
    bool found = false;
    size_t pos = 0;
    while (pos <= body.size())
    {
        size_t end = body.find('&', pos);
        if (end == std::string::npos)
            end = body.size();
        std::string field = body.substr(pos, end - pos);
        size_t equal = field.find('=');
        if (equal == std::string::npos)
            return false;
        std::string decodedName;
        std::string decodedValue;
        if (!decodeSessionFormComponent(field.substr(0, equal), decodedName)
            || !decodeSessionFormComponent(field.substr(equal + 1),
                decodedValue))
            return false;
        if (decodedName == "user")
        {
            if (found)
                return false;
            userName = decodedValue;
            found = true;
        }
        if (end == body.size())
            break;
        pos = end + 1;
    }
    if (!found)
        userName.clear();
    return found;
}

/*
函数：extractSessionLoginUserName
用途：根据请求 Content-Type 从 login body 中得到用户名，并用 HTTP 状态码表达失败原因。
参数：request 来自 ResponseBuilder；userName 是调用方提供的输出变量。
变量：contentType 保存 header；semicolon 截断参数；mediaType 是规范化后的主类型。
实现逻辑：无 Content-Type 或 text/plain 直接使用 body；urlencoded 提取 user；其他类型返回 415，格式错误返回 400。
*/
int extractSessionLoginUserName(const Request &request,
                                std::string &userName)
{
    userName.clear();
    std::string contentType;
    if (!request.getHeader("content-type", contentType))
    {
        userName = request.getBody();
        return 200;
    }
    size_t semicolon = contentType.find(';');
    std::string mediaType = sessionFormTrimOws(
        contentType.substr(0, semicolon));
    mediaType = sessionFormToLowerAscii(mediaType);
    if (mediaType == "text/plain")
    {
        userName = request.getBody();
        return 200;
    }
    if (mediaType == "application/x-www-form-urlencoded")
    {
        if (extractSessionFormUser(request.getBody(), userName))
            return 200;
        userName.clear();
        return 400;
    }
    return 415;
}
