/*
文件：srcs/Response/Response.cpp
用途：封装和生成返回给客户端的 HTTP 响应（包含状态码、请求头、文件数据等）。
*/

#include "Response.hpp"
#include "EffectiveRoute.hpp"
#include "RequestHandler.hpp"
#include "Request.hpp"
#include "ConfigRouteUtils.hpp"

#include <fcntl.h>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

/*
函数用途：基于配置文件权限，组装符合 RFC 规范的 Allow 响应头（通常用于 405 响应告知支持哪些方法）。
参数与变量：
- allowMethods (传入参数)：包含被允许方法名称字符串的集合。
- orderedMethods (局部常量数组)：按照 GET, POST, DELETE 标准顺序排列的候选字符串指针数组。
- result (局部变量)：拼接后的结果字符串。
- i (局部变量)：遍历候选数组的循环索引。
实现逻辑：
1. 初始化固定的方法顺序数组，确保最终生成的头部总是按照同一顺序输出。
2. 遍历数组，判断传入的集合中是否存在该方法。
3. 若存在，则拼接到结果字符串后；在拼接非第一个方法时，在前面追加 ", " 分隔符。
4. 返回构造好的完整头部字符串。
*/
static std::string buildAllowHeader(const std::set<std::string> &allowMethods)
{
    const char *orderedMethods[] = {"GET", "POST", "DELETE"};
    std::string result;
    size_t i = 0;
    while (i < 3)
    {
        if (allowMethods.find(orderedMethods[i]) != allowMethods.end())
        {
            if (!result.empty())
                result += ", ";
            result += orderedMethods[i];
        }
        ++i;
    }
    return result;
}

/*
函数用途：判定当前请求路径是否命中了配置中要求作为 CGI 脚本执行的文件后缀规则。
参数与变量：
- location (传入参数)：指向当前路由详细配置块的指针。
- path (传入参数)：请求包含的完整路由路径。
- it (局部变量)：遍历配置表中 cgi_extensions 的 map 迭代器。
- extension (局部变量)：当前校验的后缀名配置项。
- pos / extensionEnd (局部变量)：用于精准定位及判断后缀是否正好处于路径末尾或目录界限处的索引变量。
实现逻辑：
1. 若位置配置指针为空或未配置任何 CGI 扩展规则，直接返回 false。
2. 使用常量迭代器遍历配置设定的每个特定后缀。
3. 通过 find 操作在请求路径中搜索该后缀是否存在。
4. 若存在，判断它的结束位置是否在整个路径末尾，或者是斜杠前的独立分段。若是，视为真正的 CGI 匹配，返回 true。否则步进继续寻找。
5. 所有循环未命中则判定为非 CGI 请求。
*/
static bool isCGIRequest(const LocationConfig *location,
                         const std::string &path)
{
    if (location == NULL || location->cgi_extensions.empty())
        return false;
    std::map<std::string, std::string>::const_iterator it =
        location->cgi_extensions.begin();
    while (it != location->cgi_extensions.end())
    {
        const std::string &extension = it->first;
        size_t pos = path.find(extension);
        while (pos != std::string::npos)
        {
            size_t extensionEnd = pos + extension.size();
            if (extensionEnd == path.size() || path[extensionEnd] == '/')
                return true;
            pos = path.find(extension, pos + 1);
        }
        ++it;
    }
    return false;
}

/*
函数用途：核心分发中枢。接收完整的 Request 对象，调度解析其路由及权限规范，最终产出正确的 Response 对象反馈给通讯层。
参数与变量：
- request (传入参数)：包含已解析 HTTP 头和正文体的请求类对象。
- server (局部变量)：获取请求中对应的整机服务配置指针。
- response (局部变量)：基于请求实例化并最终返回的响应对象。
- noErrorPages (局部变量)：处理极端异常状况时传入的空错误页面容器。
- location (局部变量)：调用匹配算法找出的具体子路由位置指针。
- route (局部变量)：EffectiveRoute 类的实例，用以生成合并服务器层和路由层的物理映射结构。
- routeReady (局部变量)：记录路由配置合并结果的成功与否。
- action (局部变量)：请求方法枚举化后的动作。
- pathStatus (局部变量)：检验目标物理路径有效性后返回的状态码。
实现逻辑：
1. 校验服务端配置上下文的存在性，若丢失直接构造 500 并撤出。
2. 调用工具函数找到当前路径最符合的 Location 配置。
3. 执行安全过滤。若是被认定为由于未实现而必须拦截的 CGI 请求，拦截之并返回 501，防止源码未经处理以静态文件下发。
4. 综合 Server 与 Location 信息组合出具有确定指令层级的路由对象。
5. 执行重定向（3xx 状态拦截）：若存在合法且完整的跳转状态要求，注入重定向地址与提示主体后立刻返回。
6. 判断方法枚举值，无效则抛出 501。判断方法是否有权限，越权则抛出 405 并挂载 Allow Header。
7. 进一步把系统计算的路径结合意图映射于磁盘检验上。抛错则立即转交构建响应。
8. 把通过层层关卡校验后的安全请求与路由，投放到具体的 GET、POST 或 DELETE 处理模块完成终点执行操作。
*/
Response buildResponse(const Request &request)
{
    const ServerConfig *server = request.getConfig();
    Response response(request);
    if (server == NULL)
    {
        Response::ErrorPageMap noErrorPages;
        response.createResponse(500, "", noErrorPages);
        return response;
    }

    const LocationConfig *location =
        findMatchingLocation(request.getPath(), server->locations);

    EffectiveRoute route;
    bool routeReady = location != NULL
                          ? route.createEffectiveRoute(server, location)
                          : route.createEffectiveRoute(server);
    if (!routeReady)
    {
        response.createResponse(500, "", server->error_pages);
        return response;
    }

    // 1. 【安全第一关】：优先处理 3xx 重定向拦截
    if (location != NULL && route.redirect_status >= 300 && route.redirect_status <= 399 && !route.redirect_url.empty())
    {
        response.setStatus(route.redirect_status);
        response.setHeader("Location", route.redirect_url);
        if (route.redirect_status != 304)
        {
            response.setHeader("Content-Type", "text/html");
            response.setBody("<!DOCTYPE html><html><head><title>Redirect</title></head><body>Redirecting</body></html>");
        }
        return response;
    }

    // 2. 【安全第二关】：检测方法是否支持 (501)
    RequestAction action = requestActionFromMethod(request.getMethod());
    if (action == ACTION_UNSUPPORTED)
    {
        response.createResponse(501, "", route.server->error_pages);
        return response;
    }

    // 3. 【安全第三关】：检测该 Location 块是否有权运行此方法 (405)
    if (!isMethodAllowed(action, route.allow_methods))
    {
        response.createResponse(405, "", route.server->error_pages);
        response.setHeader("Allow", buildAllowHeader(route.allow_methods));
        return response;
    }

    // 4. 【安全第四关】：合成磁盘物理路径（利用刚才 EffectiveRoute 优秀的 joinPaths 工艺）
    int pathStatus = route.createEffectivePath(request.getPath(), action);

    // =================================================================
    // 🚀 ✨ ✨ ✨ 【大结局级：CGI 终点站并网特权通道】 ✨ ✨ ✨ 🚀
    // =================================================================
    // 在这里截获符合 CGI 后缀的文件。此时路径已经拼好（存在 route.targetPath 中），且方法合规
    if (isCGIRequest(location, request.getPath()))
    {
        // 🚨 【核心防御】：无论 GET 还是 POST，CGI 脚本在执行前必须确保物理文件真实存在！
        struct stat cgiStat;
        if (::stat(route.targetPath.c_str(), &cgiStat) != 0)
        {
            // 文件不存在，一枪回绝 404，拒绝执行无效弹射
            response.createResponse(404, "", route.server->error_pages);
            return response;
        }
        if (S_ISDIR(cgiStat.st_mode))
        {
            // 如果它是个文件夹而不是个文件，回绝 403 Forbidden
            response.createResponse(403, "", route.server->error_pages);
            return response;
        }

        // 🎯 完好无损，封装特权响应，用正确的 `route.targetPath` 传递火种给大管家！
        Response cgiResponse(request);
        cgiResponse.setStatus(200);
        cgiResponse.setManagedHeader("X-Internal-CGI-Path", route.targetPath);

        return cgiResponse; // 🏎️ 瞬间异步离场！
    }

    // 5. 【常规静态发货】：只有非 CGI 的普通静态文件请求，才去遵从 isValidPath 的静态规则
    if (pathStatus != PATH_OK)
    {
        response.createResponse(pathStatus, "", route.server->error_pages);
        return response;
    }

    if (action == ACTION_GET)
        return handleGet(request, route);
    if (action == ACTION_POST)
        return handlePost(request, route);
    return handleDelete(request, route);
}

/*
函数用途：无源请求上下文的 Response 类的构造函数。常用于内部抛错阶段或直接初始化特定状态的响应体。
参数与变量：
- closeConnection (传入参数)：用来强制干预响应结束后 TCP 连接是否断开的布尔值。
实现逻辑：
1. 采用初始化列表配置默认的基础报文段信息（协议为 HTTP/1.1，状态 200 OK）。
2. 在函数体内调用自含管理机制封装，统筹规划连接控制报文头及主体长度报文头的计算更新。
*/
Response::Response(bool closeConnection)
    : _version("HTTP/1.1"), _statusCode(200), _statusMessage("OK"),
      _headers(), _body(), _closeConnection(closeConnection)
{
    updateConnectionHeader();
    updateContentLength();
}

/*
函数用途：基于明确解析完毕的有效请求去构建一个待处理的响应上下文构造函数。
参数与变量：
- request (传入参数)：已解析且具有属性与请求报头的输入请求实体引用。
实现逻辑：
1. 使用初始化列表生成默认无负荷 200 响应参数。
2. 利用内置类方法，针对 request 头域字段中涉及连接管理的关键字预判客户端意愿，提取结果赋值给 _closeConnection 标量。
3. 执行受系统管制的关联性包头动态更新动作。
*/
Response::Response(const Request &request)
    : _version("HTTP/1.1"), _statusCode(200), _statusMessage("OK"),
      _headers(), _body(), _closeConnection(requestWantsClose(request))
{
    updateConnectionHeader();
    updateContentLength();
}

/*
函数用途：获取当前 HTTP 响应对象封装的协议版本字符串常量引用。
参数与变量：无传入参数。无局部变量。
实现逻辑：
1. 暴露类私有变量，直接返回 _version。使用常引用避免堆内存不必要的深度拷贝消耗。
*/
const std::string &Response::getVersion() const { return _version; }

/*
函数用途：获取当前 HTTP 响应记录的具体数字形态响应状态码（如 200/404）。
参数与变量：无传入参数。无局部变量。
实现逻辑：
1. 直接向外部调用层返回记录于私有区域 _statusCode 中的整型值。
*/
int Response::getStatusCode() const { return _statusCode; }

/*
函数用途：获取与数字状态码绑定的具象化语义状态消息文本常量引用（如 "OK"/"Not Found"）。
参数与变量：无传入参数。无局部变量。
实现逻辑：
1. 返回类实例记录的 _statusMessage 变量地址。
*/
const std::string &Response::getStatusMessage() const { return _statusMessage; }

/*
函数用途：获取存放当前响应将要下发的全部扩展头部字典（Map 容器）结构对象的常引用。
参数与变量：无传入参数。无局部变量。
实现逻辑：
1. 返回 _headers。保证外部环境只可探查键值对应关系且不破坏基于类的内部数据封装结构。
*/
const Response::HeaderMap &Response::getHeaders() const { return _headers; }

/*
函数用途：获取构建好的将要作为网络数据包传输主体投递的源文件正文字符串内容引用。
参数与变量：无传入参数。无局部变量。
实现逻辑：
1. 返回 _body 结构内部内存存储区的常引用。
*/
const std::string &Response::getBody() const { return _body; }

/*
函数用途：查询当前响应指令流程在物理结束下发阶段，是否将关闭该网络套接字描述符连接。
参数与变量：无传入参数。无局部变量。
实现逻辑：
1. 直接向调用上游提供 _closeConnection 布尔值，借此规划多路复用连接释放资源节点调度。
*/
bool Response::shouldCloseConnection() const { return _closeConnection; }

/*
函数用途：以安全无异常且屏蔽大小写差异的方式获取内部存放过的任意请求头数据值。
参数与变量：
- name (传入参数)：需查询检索内容的 Header 标题头原称呼字符序列。
- value (传入参数/修改)：将要借用实参所在内存引用带回主调方环境的数据写入槽点。
- it (局部变量)：标准库 map 固定使用搜寻查找结构体迭代器。
实现逻辑：
1. 将查询键经内部 canonicalHeaderName() 标准格式化清洗后，提交底层容器字典的 find 方法检索对应树节点。
2. 找不到则清空 value 容器，并返回布尔 false。
3. 如果匹配成功，将值通过拷贝落于参数所对应的地址中，并返回寻找成功的判断标识 true。
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
函数用途：更改封装中的 HTTP 状态参数体系，引发对应关联内容的同步自重组机制。
参数与变量：
- statusCode (传入参数)：目标更改写入的新状态码十进制值。
实现逻辑：
1. 存储目标传入数值，随即通过状态语义转义方法获得并附着更新对应的纯文本说明串信息。
2. 利用业务限制原则识别状态类型特性，倘若依据报文严格规范（如 204、304 码段类）必须不留有负载。当即抛弃已有之信息体与格式说明报头。
3. 基于最终结果数据进行系统代管头的重写和对正文总数长度的测量刷新策略调用。
*/
void Response::setStatus(int statusCode)
{
    _statusCode = statusCode;
    _statusMessage = statusMessageFor(statusCode);
    if (!statusMayHaveBody(_statusCode))
    {
        _body.clear();
        _headers.erase("Content-Type");
    }
    updateConnectionHeader();
    updateContentLength();
}

/*
函数用途：为出站通信内容设定常规配置键与数值选项（非禁止人工配置范围内的键位可调）。
参数与变量：
- name (传入参数)：要设置配置键的具体字面命名。
- value (传入参数)：赋值配置的等同串行结构对象。
实现逻辑：
1. 前置安全与防冲突阻断探测：查验该名字名称及值内部是否存在特殊阻断字符集。若为框架禁止私自涂抹更新的强制保留参数项（Content-Length 等），立刻退回终止命令。
2. 使用首字母及杠后字母大写格式的标准化函数生成确定键节点，执行 map 数据添加或旧值替换。
*/
void Response::setHeader(const std::string &name, const std::string &value)
{
    if (!isValidHeaderName(name) || !isValidHeaderValue(value) || isManagedHeader(name))
        return;
    _headers[canonicalHeaderName(name)] = value;
}

/*
函数用途：针对被主动设定的键位参数头施展清除并放弃使用其值的摘录移除操作。
参数与变量：
- name (传入参数)：指定删除动作对象字符串。
实现逻辑：
1. 保障底线框架管理层属性（比如保持连接管理信息域）免受销毁侵害。若命中断，阻绝指令传导。
2. 传递键面特征经正规排版修正过后，利用 erase 进行容器清除收尾。
*/
void Response::removeHeader(const std::string &name)
{
    if (isManagedHeader(name))
        return;
    _headers.erase(canonicalHeaderName(name));
}

/*
函数用途：重置并整批更替需要下放给客户端的网页实体文件数据存储段，强制刷新核对长度配额。
参数与变量：
- body (传入参数)：新的完整载荷字节结构组合形态集合体。
实现逻辑：
1. 判断所记录的状态码类别，判断本该被封堵的通道类别。若是属于无内容约束响应段流，执行空载清零并更新后阻断赋值指令流程。
2. 将整块数据结构引用进行深层次内复制赋值替换，调用工具方法丈量与记录字节规模至长度通知报文序列头部。
*/
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

/*
函数用途：针对动态创建大内存字符串进行碎片式段落附着于末端的写入方式工具。
参数与变量：
- data (传入参数)：单次分发传来的串列片段流向对象。
实现逻辑：
1. 与其余主体动作并无二致的第一步规避验证：判定当前代码有无资质承载该内容体。
2. 使用 C++ 字符串重载运算符，于对象原有内存分配块堆末端追加载体扩建容量。
3. 更新对应内容标识长宽计算域的值。
*/
void Response::appendBody(const std::string &data)
{
    if (!statusMayHaveBody(_statusCode))
        return;
    _body += data;
    updateContentLength();
}

/*
函数用途：面向纯粹文件读取或者原始底端网络输入的数据内存缓冲区做追加截取记录的方法版本。
参数与变量：
- data (传入参数)：纯指针首地址传递阵列内容区域。
- length (传入参数)：配合指针限定提取大小以避免内存泄漏或越界。
实现逻辑：
1. 做空响应类别预备防卫阻拦确认动作。
2. 检测入参内存指针合规且索取配额量不为边界零，使用针对 C 指针和数值规模扩展对象的特殊追加截入操作手法。
3. 基于载荷扩张后的实体测算更新其对外通知长值。
*/
void Response::appendBody(const char *data, size_t length)
{
    if (!statusMayHaveBody(_statusCode))
        return;
    if (data != NULL && length != 0)
        _body.append(data, length);
    updateContentLength();
}

/*
函数用途：清零摧毁存放主体传输承载文本内存区资料的行为逻辑。
参数与变量：无传入参数。无局部变量。
实现逻辑：
1. 执行自带标准字符串空间释放截断处理 clear。
2. 自动唤醒关联统计长域记录核对置换值清零指令环节。
*/
void Response::clearBody()
{
    _body.clear();
    updateContentLength();
}

/*
函数用途：设置请求连接闭环标志行为指令，决定是复用或者终了阻断该底层通信连接线。
参数与变量：
- closeConnection (传入参数)：控制决策流布尔开关形态量。
实现逻辑：
1. 把外界期望之态更新写入对应 _closeConnection 布尔域值。
2. 借由此触发基于连接策略相关的 Connection 报表头项执行被动修改更新联动反应。
*/
void Response::setCloseConnection(bool closeConnection)
{
    _closeConnection = closeConnection;
    updateConnectionHeader();
}

/*
函数用途：构建快速反馈错误（如访问禁制/丢失路径/系统毁损）情况体系的专用流水线制造工具，集成页面检索等诸多附属环节一并生效。
参数与变量：
- code (传入参数)：确切表征业务异常阶段所发出的无符号正数代码代号。
- bodyText (传入参数)：如若不配备指定网页结构体系时的兜底单调通报文字段落。
- errorPages (传入参数)：在主处理逻辑与主配置系统衔接后提供的错误美化模板文件查询系统索引表。
实现逻辑：
1. 切断与清剿任何先前的残存属性和主体遗留内存。
2. 使用强制降级类型转录为有符号值赋予核心状态标位管理模块。对于支持携带实体的非虚构状态将辅助文案文本进行纯文类型灌入并配以 text/plain 标记。
3. 经鉴别判定目前身处错误处理维度数值内（4xx至5xx），开始着手于对错误图录查账装填；失败者开启兜底极简写死代码组装页面工程。
4. 若是经过更改判定进入了强制干预抹除的特殊不可带内容域代码限制期，直接清理正文痕迹并且剔掉不必要的格式解析声明。
5. 尾声阶段下放命令，联动统合生成涉及尺寸大小记录指标与系统接引管理指令等配套参数体。
*/
void Response::createResponse(unsigned int code, const std::string &bodyText,
                              const ErrorPageMap &errorPages)
{
    // 1. 全盘洗涤旧资产，腾出干净的舱位
    _headers.clear();
    _body.clear();
    
    // 2. 注入状态码并联动触发底层状态语义自重组
    setStatus(static_cast<int>(code));
    
    // 3. 🟢 【规范化注入】：使用规范的直接写入，不留大小写隐患
    if (statusMayHaveBody(_statusCode) && !bodyText.empty())
    {
        _body = bodyText;
        // 采用统一的系统特权写入，确保键名经过 canonical 规整装订！
        setManagedHeader("Content-Type", "text/plain");
    }

    // 4. 【页面覆盖与兜底】：如果是错误状态，尝试加载客制化好的人脸页面
    if (isErrorStatusCode(_statusCode))
    {
        // 💡 提示：如果 loadCustomErrorPage 成功，它会把 _body 替换为 HTML 内容
        // 并自动调用 setManagedHeader("Content-Type", "text/html") 进行升级
        if (!loadCustomErrorPage(errorPages))
        {
            // 如果配了自定义页面但读取失败，或者压根没配，走应急草图兜底
            setDefaultErrorPage();
        }
    }
    
    // 5. 【协议边界死守】：如果 RFC 规定这个状态码绝对不准带 Body（如 240/304），强行清空
    if (!statusMayHaveBody(_statusCode))
    {
        _body.clear();
        _headers.erase(canonicalHeaderName("Content-Type")); // 规范清除
    }
    
    // 6. 重新拉起连接大闸，丈量最终的报文尺寸
    updateConnectionHeader();
    updateContentLength();
}

/*
函数用途：组装总输出枢纽管道段，依照通信准则章程硬性编排组装头体等独立结构单元，转交并合成一块可用的原始字节流字符串结构体供网络端点投送下发。
参数与变量：
- output (局部变量)：以纯操作堆栈拼凑流方式分配用以合成的 ostringstream 生成拼接器件。
- it (局部变量)：容器用字典指针循行工具。
实现逻辑：
1. 第一阶段打定起头声明：用串式推送按序放置协议标识，后跟空白与具体执行指令码状态与名称，随后敲定换行符标志线。
2. 对内部所有存在项做有序抽取。由常量迭代器循环调出属性名字与数据串间配上 ": " 的规范化标识与段末截断回车位标记字符 "\r\n"。
3. 脱离循环，放置用于切分头体部位的重要空白双重回车段 "\r\n"。
4. 取出实心内存里封存的所有体积量大小实体内容利用底层串流操作接口将二进制与非二进制的复合内容安全写注进总成拼接块。
5. 返回该被完全构建为系统网络底层所需的一揽子字符串形态结构物件。
*/
std::string Response::responseToString() const
{
    std::ostringstream output;
    output << _version << " " << _statusCode << " "
           << _statusMessage << "\r\n";
    HeaderMap::const_iterator it = _headers.begin();
    while (it != _headers.end())
    {
        output << it->first << ": " << it->second << "\r\n";
        ++it;
    }
    output << "\r\n";
    output.write(_body.data(), static_cast<std::streamsize>(_body.size()));
    return output.str();
}

/*
函数用途：基于对方发派过来的申请包体解剖并透析它在管理连接时长方面是否有迫切希望断裂中止的标志（解析 "Connection: close"）。
参数与变量：
- request (传入参数)：接收由上游处理网卡信息截流并提纯过的对象体系。
- value (局部变量)：捕获提取出该关联项头域后内部值存放介质池。
- begin / end / comma (局部变量)：针对复杂逗号拼接的特征值在解剖寻查中作起止标识的计算用定位指针位移量数值体。
实现逻辑：
1. 提取对象头信息，假如此头从未在客户端请求时配置申报，默认为常态留存并不执行斩断阻拦判定返回 false。
2. 基于含有连接策略信息结构体中带有包含多种串联以逗号划区的格式，开启区间截取式的游离探查算法过程。
3. 发现以分隔符设限段内容并将双边无实际价值影响的空号符号消除剔除排查掉。
4. 全数转化为安全小写以防兼容规避。倘若提取发现等值于 "close"，则立即回送具备切断该网路需求的预判确认布尔判定 true。
5. 依次跨逗号处理后续指令，全数遍历排查未发现斩断号则默认常态复用假值。
*/
bool Response::requestWantsClose(const Request &request)
{
    std::string value;
    if (!request.getHeader("connection", value))
        return false;
    size_t begin = 0;
    while (begin <= value.size())
    {
        size_t comma = value.find(',', begin);
        size_t end = comma == std::string::npos ? value.size() : comma;
        while (begin < end && (value[begin] == ' ' || value[begin] == '\t'))
            ++begin;
        while (end > begin && (value[end - 1] == ' ' || value[end - 1] == '\t'))
            --end;
        if (toLowerAscii(value.substr(begin, end - begin)) == "close")
            return true;
        if (comma == std::string::npos)
            break;
        begin = comma + 1;
    }
    return false;
}

/*
函数用途：集中管控并且匹配状态号码段所对等的官方指定通讯解释名语汇库转换接口，防除在分散地代配错导致的不统一。
参数与变量：
- statusCode (传入参数)：接收请求指令验证并分发确定的数值号码代称。
实现逻辑：
1. 启用枚举式开关体系对号码库常被使用的节点建立分发支路并且挂载恒定不可修改的字面语汇。如 201 挂 "Created" 以及其余一系列匹配库。
2. 当逃离枚举式准确分支捕捉体系则退入次级的 3 开头重定位概括性大区间并使用广泛的重定代表词进行接管。
3. 如果未匹配到上述情形，利用未知属性词 "Unknown" 当做最后安全阀防线防系统解构。
*/
std::string Response::statusMessageFor(int statusCode)
{
    switch (statusCode)
    {
    case 200:
        return "OK";
    case 201:
        return "Created";
    case 204:
        return "No Content";
    case 300:
        return "Multiple Choices";
    case 301:
        return "Moved Permanently";
    case 302:
        return "Found";
    case 303:
        return "See Other";
    case 304:
        return "Not Modified";
    case 307:
        return "Temporary Redirect";
    case 308:
        return "Permanent Redirect";
    case 400:
        return "Bad Request";
    case 403:
        return "Forbidden";
    case 404:
        return "Not Found";
    case 405:
        return "Method Not Allowed";
    case 408:
        return "Request Timeout";
    case 409:
        return "Conflict";
    case 411:
        return "Length Required";
    case 413:
        return "Payload Too Large";
    case 414:
        return "URI Too Long";
    case 415:
        return "Unsupported Media Type";
    case 423:
        return "Locked";
    case 431:
        return "Request Header Fields Too Large";
    case 500:
        return "Internal Server Error";
    case 501:
        return "Not Implemented";
    case 504:
        return "Gateway Timeout";
    case 505:
        return "HTTP Version Not Supported";
    }
    if (statusCode >= 300 && statusCode <= 399)
        return "Redirect";
    return "Unknown";
}

/*
函数用途：划定判定当前结果的层级属性是否为需要作为严重负反馈抛弃处理层面的报错归属体系类别。
参数与变量：
- statusCode (传入参数)：等待评估鉴别系统归类类别的码体。
实现逻辑：
1. 判断数字其否身处 400 到 599 这个囊括客户端与后端操作故障的危险禁限数值段中，是则返真。
*/
bool Response::isErrorStatusCode(int statusCode)
{
    return statusCode >= 400 && statusCode <= 599;
}

/*
函数用途：遵守底层通讯设计法则与标准协议框架，鉴定特殊状况段下达的指令是否存在严苛要求清剿空余主体的必要要求。
参数与变量：
- statusCode (传入参数)：用以定位协议法例规定限制状态点数字的识别基点代码体。
实现逻辑：
1. 使用非逻辑运算推翻掉归属于禁止存有额外承重负载（如全套处于沟通准备阶段 100~199，或无需实体 204 号以及重复使用先前实体的 304 号）这类情况从而实现判决认定。
*/
bool Response::statusMayHaveBody(int statusCode)
{
    return !((statusCode >= 100 && statusCode < 200) || statusCode == 204 || statusCode == 304);
}

/*
函数用途：对非格式化的数理统计结果体积大小变量赋予文本外观转变包装服务的小功能模块。
参数与变量：
- value (传入参数)：内存管理空间计算反馈所得体积实参量。
- output (局部变量)：流式组装拼接构件用具。
实现逻辑：
1. 投入并置载到流式工具处理流水线输出转换字符串。返回包装后的成体文字串对象。
*/
std::string Response::sizeToString(size_t value)
{
    std::ostringstream output;
    output << value;
    return output.str();
}

/*
函数用途：与 RequestHandler 相同的功能拷贝工具，规整统一字母体系实现忽略字号的识别。
参数与变量：
- value (传入参数)：目标转换字符串。
- result / i (局部变量)：新分配对象接收区和序列排查游标索引数字。
实现逻辑：
1. 全程拷贝字符串本体。判断如在此段 'A' - 'Z' 的字母表跨度中利用代数转换差额值将位置点移动到小写区间点实现小写替换，最后提交完成品副本字符串。
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
函数用途：针对 HTTP 请求头键名称执行外观整理工程（强制转换为符合常见感知的破折号后首字母起大写的排布组合模型），防止多版本产生匹配偏差。
参数与变量：
- name (传入参数)：混乱或是任意大小写参杂的属性名字组合词条。
- result (局部变量)：经历前次基础小写梳理改造后的预处理产品结构对象。
- uppercaseNext (局部变量)：信号流指示仪标志布尔点，指示跟随下一个操作的是否为目标需要进行跨区转换。
- i (局部变量)：探查推进进度数字标定。
实现逻辑：
1. 取出全部转化为基底平摊的小写结构版本并且初始化针对第一个字的大写触发条件设置许可。
2. 循环走访探查各个小写结构区域，如果探知具备上升条件信号且处在正常字母值段区间便施行强制拉高动作转变至大写层。
3. 同步监察当前游标字符是不是为触发断代的连字破折符号 "-", 作为重置上升触发器的指令标，并推送后续直至返回结束工作品串条。
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
函数用途：排查甄别该指定的特定操作名字标签，是否存在于此套框架已明确表示需独占强制操盘管控（不允许应用代码擅自干扰）的管辖权清单。
参数与变量：
- name (传入参数)：需查验并比对匹配是否为接管清单人员名字字段。
- lower (局部变量)：防范格式错误进行的保险统一转换产品名称代称字符串。
实现逻辑：
1. 用统一降温小写防线过滤对象，直接对 "content-length" 和 "connection" 两大重点底层运作相关字段开展排他比定运算。真即拦截管控禁入。
*/
bool Response::isManagedHeader(const std::string &name)
{
    std::string lower = toLowerAscii(name);
    return lower == "content-length" || lower == "connection";
}

/*
函数用途：甄选排堵那些利用恶性特殊符号伪装潜入而破坏通讯协议安全结构语法的畸形头部名字构成组合体。
参数与变量：
- name (传入参数)：拟将加入头部位序列名字对象词段结构体。
- symbols (局部常量)：严格参照框架通信协定制定的特有可用非常规符号防撞排查字符体系表（白名单）。
- i / c (局部变量)：走访索引标与提取并剥去符号表示形态强制作为纯数字比较的基础单元位。
实现逻辑：
1. 在对空名进行一票否决之后，启用符合规定的标记名单名录。
2. 循环对拆卸成单兵单元后的数据运用数理范围检测，如果既并非处在数字区间亦不落座于字母体系且同样不包括在定制安全集内。执行驳回处理输出 false。
3. 畅通完成一切游历后盖印证明无害化返回正常通行。
*/
bool Response::isValidHeaderName(const std::string &name)
{
    if (name.empty())
        return false;

    // 🟢 【黄金站位】：把下划线 '_' 放进来，同时把连字符 '-' 强行死死钉在最末尾！
    // 这样任何 C++ 编译器都绝对不可能再把它误判为“范围减号”了！
    const std::string symbols("!#$%&'*+.^`|~_-");

    size_t i = 0;
    while (i < name.size())
    {
        unsigned char c = static_cast<unsigned char>(name[i]);
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || symbols.find(c) != std::string::npos))
            return false;
        ++i;
    }
    return true;
}

/*
函数用途：对内容所依附数值序列同样开展违规和隐身非法攻击排版结构探明。
参数与变量：
- value (传入参数)：伴生或装载于该标定点后的附加数据结构片段载体集合。
- i / c (局部变量)：用以游标探索追踪以及做值转换比对方的单兵承揽变量组。
实现逻辑：
1. 走访并将内容解构为 ASCII 表达数值段，直接筛选屏蔽低于可视化展示区间且排除制表容错点的隐匿性破坏码（如 0 到 31 间控制符及处于 127 点段抹除控制符号等）。命中则不给予添加许可返回排斥标记 false。通畅即可成功。
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
函数用途：面向被接管管控体系内强设特权使用的直接指令穿透注入专用设置函数。避开一切阻拦审查径直进入。
参数与变量：
- name (传入参数)：需设立的管辖系统核心配置命名键位字词。
- value (传入参数)：强迫设下匹配内容所携数值参数对象。
实现逻辑：
1. 在进行外观正轨标准美化化规整装订后将其当做确定目标直冲底端 Map 图谱节点替换更写或扩容并填平值空穴。
*/
void Response::setManagedHeader(const std::string &name,
                                const std::string &value)
{
    _headers[canonicalHeaderName(name)] = value;
}

/*
函数用途：动态监测当前内存中用于输出传递对象块占用数并计算记录用于告知对面，保证对方能按数裁切出正确的收取结构区长段尺寸参数刷新记录体系。
参数与变量：无传入参数。无局部变量。
实现逻辑：
1. 依照法制规定测度若本没有权利发送任何承载载体物件的情况时剔除剥除掉对应报数标牌，退出检测。
2. 反之借用底层特制穿插管理设权体系写入提取并运用数值尺寸规整字串后包装成最新尺寸并登基记录入薄。
*/
void Response::updateContentLength()
{
    if (!statusMayHaveBody(_statusCode))
    {
        _headers.erase("Content-Length");
        return;
    }
    setManagedHeader("Content-Length", sizeToString(_body.size()));
}

/*
函数用途：综合汇总目前环境状况发生严重且有安全隐患等必须切断的危险点时执行底层管理强干预命令置入及报文挂名通报策略制定功能模块实施行动部段。
参数与变量：无传入参数。无局部变量。
实现逻辑：
1. 基于假定原暂无需断离情形为起点判定开启，将现在阶段面临遭遇的诸如坏包、超容量传输限制、过载和非法头版尺寸或版本拒绝承认这些触犯高维管制的指令集中触发并执行复写断连确认令标为真（true）。
2. 在通过特权注入管线更新网络底层对应命令名称至包体附加上供网络传输。如果标为要关将注入并下放 "close"，不则保持留守 "keep-alive"。
*/
void Response::updateConnectionHeader()
{
    if (!_closeConnection)
    {
        switch (_statusCode)
        {
        case 400:
        case 408:
        case 409:
        case 411:
        case 413:
        case 414:
        case 431:
        case 505:
            _closeConnection = true;
            break;
        default:
            break;
        }
    }
    setManagedHeader("Connection",
                     _closeConnection ? "close" : "keep-alive");
}

/*
函数用途：寻路与发掘由用户个人编写制作预留在磁盘系统中具有装饰及信息明细等个性化报幕页面并尝试安全开启截取利用行为总成操作器。
参数与变量：
- errorPages (传入参数)：携带包含对应配置位置图谱引路资料表名录档案系统物件常引结构用体。
- it (局部变量)：图录字典搜捕人员代表用指针对查探工具组件。
- fd (局部变量)：向系统借用用做传输接引口管道凭证序号证明对象代编号码。
- fileInfo (局部变量)：系统调集传报归档查明物理身世档案记录表格文件状态。
实现逻辑：
1. 以现有处于危机关头错误值搜录在配置库图档里查看。倘如名录库查无可利用人员当场退走上交失效信息。
2. 从底级系统端尝试建接仅阅读权管路管道操作，失效同样阻挡。利用系统命令复核验身确保来者实乃常规非目录等怪类安全件载体失败也闭管道返回。
3. 清零排废本体附挂结构内存预备灌浆阶段，使用定制接流读工具 readOpenedFileIntoBody 进行内容抽取截流注。如过程失误同样闭嘴闭合走人。成功便贴挂内容标志装填类型标志符代表操作流结并告成返真回执指令。
*/
bool Response::loadCustomErrorPage(const ErrorPageMap &errorPages)
{
    ErrorPageMap::const_iterator it = errorPages.find(_statusCode);
    if (it == errorPages.end())
        return false;
    int fd = open(it->second.c_str(), O_RDONLY);
    if (fd < 0)
        return false;
    struct stat fileInfo;
    if (stat(it->second.c_str(), &fileInfo) != 0 || !S_ISREG(fileInfo.st_mode))
    {
        close(fd);
        return false;
    }
    _body.clear();
    if (!readOpenedFileIntoBody(fd))
    {
        _body.clear();
        return false;
    }
    _headers["Content-Type"] = "text/html";
    return true;
}

/*
函数用途：接受底层管控所分派下达已打开连接状态通道代号去提取内存并在保障不出越界崩塌漏水危险中源源获取截存进入总管理系统体腔内部运作工程件。
参数与变量：
- fd (传入参数)：确认并且核准有效的文件连接挂载口标编号码实体。
- bufferSize (局部常量)：限定框定每一次汲取水量最高峰值规模型内存容器框长上限数值限控点(64K标准模组版)。
- buffer (局部变量)：从操作系统要来的安全可靠且基于规模型限制范围所临时筹办容储水池空间载具阵列容器。
- bytesRead (局部变量)：执行阶段收纳每一波系统水泵返回实装记录数值用来做界限判断终止依据条件存变量体。
实现逻辑：
1. 设置系统标准安全限定范围框区与连续安全缓存空间数组。
2. 运转底层水泵机器接口命令展开抽水动作源源汇填进数组池并在安全范围内进行总腔载体的扩建加固。
3. 抽水断停后合闸断电结束通道。凭借最终判定探查泵机是否有底层破坏损坏的标识位状态汇报返回通报。
*/
bool Response::readOpenedFileIntoBody(int fd)
{
    const size_t bufferSize = 64 * 1024;
    std::vector<char> buffer(bufferSize);
    ssize_t bytesRead = 0;
    while ((bytesRead = read(fd, &buffer[0], bufferSize)) > 0)
        _body.append(&buffer[0], static_cast<size_t>(bytesRead));
    close(fd);
    return bytesRead >= 0;
}

/*
函数用途：作为防御一切因无法搜查匹配调集用户客制装饰等所有突发事故最后终极兜底接盘硬接写操作工，制造极度粗糙简陋应急提示排版页面输出的封箱底措施流程段。
参数与变量：
- body (局部变量)：充当应急草图手画版绘画流的装载中继转流承接器材拼接站组建操作类具。
实现逻辑：
1. 发动拼接器材写入极其初浅短微符合规制却无丝毫装饰点亮色底版头尾及主要错点数字和名号拼接至载流体中心画板。将成品直接盖附到响应管理最终外投载体内，打发其带有 HTML 名签投递回主轴线上去填补系统空洞缺项。
*/
void Response::setDefaultErrorPage()
{
    std::ostringstream body;
    body << "<!DOCTYPE html><html><head><title>"
         << _statusCode << " " << _statusMessage
         << "</title></head><body><h1>"
         << _statusCode << " " << _statusMessage
         << "</h1></body></html>";
    _body = body.str();
    _headers["Content-Type"] = "text/html";
}