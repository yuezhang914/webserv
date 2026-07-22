/*
文件：includes/RequestHandlerInternal.hpp
用途：连接 RequestHandler 拆分后的目录、上传和删除实现，保存 MIME 工具与文件操作临时对象的内部声明。
模块边界：本文件只供 srcs/Response 内部使用，不增加 ServerManager 必须调用的新接口。
*/
#ifndef REQUEST_HANDLER_INTERNAL_HPP
#define REQUEST_HANDLER_INTERNAL_HPP

/*
包含：RequestHandler.hpp
用途：使用 Request、Response、EffectiveRoute 和 RequestAction 的公开声明。
*/
#include "RequestHandler.hpp"

/*
函数：requestHandlerToLowerAscii
用途：生成只转换 ASCII A-Z 的小写副本，供扩展名和 Content-Type 比较。
参数：value 来自文件扩展名或 header 文本。
返回：转换后的字符串。
*/
std::string requestHandlerToLowerAscii(const std::string &value);

/*
函数：getMimeType
用途：根据真实文件路径扩展名返回本项目支持的 MIME type。
参数：path 来自 EffectiveRoute::targetPath 或找到的 index 文件。
返回：已知类型返回对应 MIME，未知类型返回 application/octet-stream。
*/
std::string getMimeType(const std::string &path);

/*
函数：handleIndex
用途：处理目录 GET，依次尝试 index 文件并在需要时生成 autoindex。
参数：route 提供目录真实路径和配置；requestPath 用于页面 URL；closeConnection 继承请求策略。
返回：index、autoindex 或错误 Response。
*/
Response handleIndex(const EffectiveRoute &route,
                     const std::string &requestPath,
                     bool closeConnection);

/*
枚举：FileOperationStatus
用途：表示内部文件名、上传验证和文件创建帮助函数的非 HTTP 成功/失败状态。
*/
enum FileOperationStatus
{
    FILE_OPERATION_OK = 0,
    FILE_OPERATION_ERROR = -1
};

/*
结构体：FileOperation
用途：在 POST/DELETE 过程中集中保存文件名、真实路径和正在构造的 Response。
成员来源：handlePost()/handleDelete() 根据当前 Request 创建；帮助函数逐步写入 fileName/filePath/response。
模块边界：对象只在一次文件操作期间存在，不保存 Request 指针或文件描述符。
*/
struct FileOperation
{
    std::string fileName; /* 从 normalized path 提取并验证后的文件名。 */
    std::string filePath; /* 最终上传或删除的真实完整路径。 */
    Response response;    /* 继承当前 Request 连接策略的响应对象。 */

    /*
    函数：FileOperation
    用途：创建与当前 Request 连接策略一致的文件操作临时对象。
    参数：request 来自 handlePost()/handleDelete()。
    实现逻辑：文件名和路径置空，response 调用 Response(request)。
    */
    explicit FileOperation(const Request &request)
        : fileName(), filePath(), response(request) {}

    /*
    函数：getFileName
    用途：从 route.targetPath 提取并验证最后一个文件名片段。
    参数：route 已由 ResponseBuilder 生成目标路径。
    返回：成功返回 FILE_OPERATION_OK，非法文件名时写错误 Response 并返回失败。
    */
    int getFileName(const EffectiveRoute &route);

    /*
    函数：validateUploadRequest
    用途：检查 POST framing、Content-Type、上传目录和文件名。
    参数：request 提供 headers/body；route 提供 upload_path 和错误页。
    返回：全部满足时返回 FILE_OPERATION_OK，否则写 response 并返回失败。
    */
    int validateUploadRequest(const Request &request,
                              const EffectiveRoute &route);

    /*
    函数：createFile
    用途：在上传目录内以唯一名称创建并写入完整 body。
    参数：body 来自 Request；baseDirectory 来自有效 upload_path；errorPages 来自 server。
    返回：成功返回 FILE_OPERATION_OK，失败时设置 response 并返回失败。
    */
    int createFile(const std::string &body,
                   const std::string &baseDirectory,
                   const Response::ErrorPageMap &errorPages);

    /*
    函数：fileExists
    用途：判断候选完整路径是否已经存在。
    参数：fullPath 由上传基础目录和文件名拼出。
    返回：stat 成功返回 true，其他情况返回 false。
    */
    bool fileExists(const std::string &fullPath) const;

    /*
    函数：generateUniqueFilename
    用途：在同名文件存在时生成 name_1.ext、name_2.ext 等候选名称。
    参数：baseDirectory 是上传目标目录。
    返回：找到可用名称时返回文件名，无法生成时返回空字符串。
    */
    std::string generateUniqueFilename(
        const std::string &baseDirectory) const;

    /*
    函数：createDeleteResponse
    用途：把 unlink 前后的 errno 映射为 DELETE HTTP Response。
    参数：errorNumber 是保存的 errno；errorPages 来自 server 配置。
    实现逻辑：成功生成 204，权限/不存在/目录等错误生成对应状态。
    */
    void createDeleteResponse(int errorNumber,
                              const Response::ErrorPageMap &errorPages);

    /*
    函数：checkContentType
    用途：判断 POST Content-Type 是否属于当前原始 body 上传支持范围。
    参数：contentType 来自 Request header。
    返回：支持返回 FILE_OPERATION_OK；multipart 等未实现类型返回失败。
    */
    int checkContentType(const std::string &contentType) const;
};

#endif
