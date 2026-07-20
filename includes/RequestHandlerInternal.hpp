/*
文件：includes/RequestHandlerInternal.hpp
用途：连接 RequestHandler 拆分后的多个实现文件，保存目录处理、MIME 工具与文件操作临时对象的内部声明。
边界：本文件只供 srcs/Response 内部实现使用，不增加 ServerManager 或其他模块必须调用的新接口。
*/
#ifndef REQUEST_HANDLER_INTERNAL_HPP
#define REQUEST_HANDLER_INTERNAL_HPP

#include "RequestHandler.hpp"

/*
内部接口说明：本头文件只连接 RequestHandler 的多个实现文件，不属于 Response 模块对外公开接口。
拆分边界：目录处理函数和 FileOperation 临时对象仍只由普通 GET/POST/DELETE handler 使用。
*/

std::string requestHandlerToLowerAscii(const std::string &value);
std::string getMimeType(const std::string &path);

Response handleIndex(const EffectiveRoute &route,
                     const std::string &requestPath,
                     bool closeConnection);

enum FileOperationStatus
{
    FILE_OPERATION_OK = 0,
    FILE_OPERATION_ERROR = -1
};

/*
结构体：FileOperation
用途：只在 RequestHandler 实现文件间保存一次 POST/DELETE 文件操作的文件名、真实路径和正在构造的 Response。
来源：handlePost()/handleDelete() 根据当前 Request 创建。
清理说明：不保存与 request.getBody().size() 重复的 length，也不进入公共 RequestHandler.hpp。
*/
struct FileOperation
{
    std::string fileName;
    std::string filePath;
    Response response;

    /*
    函数：FileOperation::FileOperation
    用途：创建与当前 Request 连接策略一致的文件操作临时对象。
    参数来源：request 来自 handlePost()/handleDelete()。
    变量说明：fileName/filePath 初始化为空；response 调用 Response(request) 继承 Connection: close。
    实现逻辑：只初始化状态，不读写文件。
    */
    explicit FileOperation(const Request &request)
        : fileName(), filePath(), response(request) {}

    int getFileName(const EffectiveRoute &route);
    int validateUploadRequest(const Request &request,
        const EffectiveRoute &route);
    int createFile(const std::string &body,
        const std::string &baseDirectory,
        const Response::ErrorPageMap &errorPages);
    bool fileExists(const std::string &fullPath) const;
    std::string generateUniqueFilename(
        const std::string &baseDirectory) const;
    void createDeleteResponse(int errorNumber,
        const Response::ErrorPageMap &errorPages);
    int checkContentType(const std::string &contentType) const;
};

#endif
