/*
文件：srcs/Response/RequestHandler.cpp
用途：实现普通静态文件 GET，以及 method 字符串到内部动作的映射和 allow_methods 检查。
拆分说明：目录、上传和删除处理分别移动到独立实现文件；公开 RequestHandler.hpp 接口保持不变。
*/
/*
包含：RequestHandler.hpp
用途：取得 GET/POST/DELETE 公开入口和 RequestAction 映射声明。
*/
#include "RequestHandler.hpp"

/*
包含：RequestHandlerInternal.hpp
用途：调用目录处理和 MIME 类型内部辅助接口。
*/
#include "RequestHandlerInternal.hpp"

/*
包含：<fstream>
用途：使用 std::ifstream 以二进制方式读取普通静态文件。
*/
#include <fstream>

/*
包含：<sstream>
用途：把静态文件流内容汇总为二进制安全 std::string。
*/
#include <sstream>

/*
函数：requestHandlerToLowerAscii
用途：为 MIME 扩展名和 Content-Type 比较生成 ASCII 小写副本。
参数来源：value 来自文件扩展名或 Request 的 Content-Type header。
变量说明：result 是可修改副本；i 是字符下标。
实现逻辑：遍历全部字符，只把 A-Z 改为 a-z，其他字节保持不变。
*/
std::string requestHandlerToLowerAscii(const std::string &value)
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
函数：getMimeType
用途：根据最终真实文件路径扩展名选择 Content-Type。
参数来源：path 来自 route.targetPath 或实际命中的 indexPath。
变量说明：dot 是最后一个点位置；extension 是小写扩展名。
实现逻辑：没有扩展名返回 application/octet-stream；匹配常用类型；未知类型回退二进制流。
*/
std::string getMimeType(const std::string &path)
{
    size_t dot = path.find_last_of('.');
    if (dot == std::string::npos)
        return "application/octet-stream";
    std::string extension = requestHandlerToLowerAscii(path.substr(dot));
    if (extension == ".html" || extension == ".htm") return "text/html";
    if (extension == ".css") return "text/css";
    if (extension == ".js") return "application/javascript";
    if (extension == ".json") return "application/json";
    if (extension == ".txt") return "text/plain";
    if (extension == ".png") return "image/png";
    if (extension == ".jpg" || extension == ".jpeg") return "image/jpeg";
    if (extension == ".gif") return "image/gif";
    if (extension == ".svg") return "image/svg+xml";
    if (extension == ".pdf") return "application/pdf";
    return "application/octet-stream";
}

/*
函数：handleGet
用途：返回普通静态文件，或把目录交给 index/autoindex 处理。
参数来源：request 来自 buildResponse()；route 已完成配置合并、targetPath 构造和 GET stat 验证。
变量说明：response 保存结果；input/buffer/content 读取普通文件。
实现逻辑：
    1. route.isDir 时调用 handleIndex()，并传递 URL path 和连接策略。
    2. 普通文件以 binary 模式打开，失败返回 500。
    3. 读完整内容并确认关闭流无错误。
    4. 设置 200、真实 MIME 和二进制 body。
*/
Response handleGet(const Request &request, EffectiveRoute &route)
{
    Response response(request);
    if (route.isDir)
        return handleIndex(route, request.getPath(),
            response.shouldCloseConnection());

    std::ifstream input(route.targetPath.c_str(), std::ios::binary);
    if (!input)
    {
        response.createResponse(500, "Failed to open file",
            route.server->error_pages);
        return response;
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    std::string content = buffer.str();
    input.close();
    if (!input)
    {
        response.createResponse(500, "Error closing file",
            route.server->error_pages);
        return response;
    }

    response.setStatus(200);
    response.setHeader("Content-Type", getMimeType(route.targetPath));
    response.setBody(content);
    return response;
}


/*
函数：requestActionFromMethod
用途：把 Request.getMethod() 字符串映射为 Response 模块内部 enum。
参数来源：method 来自 buildResponse() 的 Request。
变量说明：无局部变量。
实现逻辑：GET/POST/DELETE 分别返回对应动作，其他合法 token 返回 ACTION_UNSUPPORTED。
*/
RequestAction requestActionFromMethod(const std::string &method)
{
    if (method == "GET") return ACTION_GET;
    if (method == "POST") return ACTION_POST;
    if (method == "DELETE") return ACTION_DELETE;
    return ACTION_UNSUPPORTED;
}

/*
函数：isMethodAllowed
用途：检查已经实现的动作是否出现在 EffectiveRoute 最终 allow_methods 中。
参数来源：action 来自 requestActionFromMethod()；allowMethods 来自 server/location 合并。
变量说明：无局部变量。
实现逻辑：按动作查找对应字符串；未知动作返回 false。
*/
bool isMethodAllowed(RequestAction action,
                     const std::set<std::string> &allowMethods)
{
    if (action == ACTION_GET)
        return allowMethods.find("GET") != allowMethods.end();
    if (action == ACTION_POST)
        return allowMethods.find("POST") != allowMethods.end();
    if (action == ACTION_DELETE)
        return allowMethods.find("DELETE") != allowMethods.end();
    return false;
}
