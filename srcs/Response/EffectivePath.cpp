/*
文件：srcs/Response/EffectivePath.cpp
用途：把 normalized URL path 映射为真实文件系统路径，并执行 GET 所需的基础路径类型与 errno 校验。
拆分说明：函数从原 EffectiveRoute.cpp 按“路径生成与验证”职责原样移动，不重复 URI 解码或规范化。
*/
#include "EffectiveRoute.hpp"

#include <cerrno>
#include <sys/stat.h>

/*
函数：EffectiveRoute::createEffectivePath
用途：把 normalized request path 映射到 root 或 alias 下的真实 targetPath，并执行当前动作需要的基础路径验证。
参数来源：
    - requestPath：RequestParser 已解码、规范化、去 query 的 request.getPath()。
    - action：buildResponse() 通过 requestActionFromMethod() 得到的 GET/POST/DELETE 动作。
变量说明：
    - base：最终使用的 root 或 alias 目录。
    - suffix：需要追加到 base 的 URL path 部分；alias 模式会先移除 location_prefix。
实现逻辑：
    1. requestPath 必须非空且以 / 开头，否则返回 400。
    2. alias 模式检查 requestPath 确实属于当前 location，再移除 location 前缀。
    3. root 模式直接使用完整 normalized path 作为 suffix。
    4. 调用 joinPaths() 生成 targetPath；空结果返回 500。
    5. 调用 isValidPath(action)：GET 立即 stat，POST/DELETE 暂留给对应处理函数。
*/
int EffectiveRoute::createEffectivePath(const std::string &requestPath,
                                        RequestAction action)
{
    if (requestPath.empty() || requestPath[0] != '/')
        return 400;
    std::string base;
    std::string suffix;

    if (use_alias)
    {
        base = alias;
        if (requestPath.compare(0, location_prefix.size(),
            location_prefix) != 0)
            return 404;
        suffix = requestPath.substr(location_prefix.size());
    }
    else
    {
        base = root;
        suffix = requestPath;
    }
    targetPath = joinPaths(base, suffix);
    if (targetPath.empty())
        return 500;
    return isValidPath(action);
}

/*
函数：joinPaths
用途：连接两个文件系统路径片段，并只处理交界处缺少或重复斜杠的问题。
参数来源：
    - base：EffectiveRoute.root/alias 或上传基础目录。
    - suffix：normalized path、upload_path 或文件名片段。
变量说明：baseSlash/suffixSlash 分别记录两段边界是否已有 /。
实现逻辑：
    1. base 为空时返回空，避免生成无根目标。
    2. suffix 为空时直接返回 base。
    3. 两边都有斜杠时删除 suffix 首斜杠。
    4. 两边都没有斜杠时补一个。
    5. 其余情况直接拼接。
*/
std::string joinPaths(const std::string &base,
                      const std::string &suffix)
{
    if (base.empty())
        return std::string();
    if (suffix.empty())
        return base;
    bool baseSlash = base[base.size() - 1] == '/';
    bool suffixSlash = suffix[0] == '/';
    if (baseSlash && suffixSlash)
        return base + suffix.substr(1);
    if (!baseSlash && !suffixSlash)
        return base + "/" + suffix;
    return base + suffix;
}

/*
函数：EffectiveRoute::isValidPath
用途：验证 GET targetPath 存在且只能是普通文件或目录；POST/DELETE 保留到真正操作时验证。
参数来源：action 由 createEffectivePath() 原样传入；targetPath 已在同一对象中生成。
变量说明：pathInfo 是 stat() 填充的文件类型/权限信息。
实现逻辑：
    1. 重置 isDir=false。
    2. 非 GET 直接返回 PATH_OK，因为 POST 使用 upload_path，DELETE 需要更精确 errno 映射。
    3. GET stat 失败时把不存在映射 404、权限/环/过长映射 403、其他映射 500。
    4. 目标不是普通文件或目录时返回 403，避免 FIFO/设备/socket 被读取。
    5. 目录时设置 isDir=true 并保证 targetPath 末尾有 /。
*/
int EffectiveRoute::isValidPath(RequestAction action)
{
    isDir = false;
    if (action != ACTION_GET)
        return PATH_OK;

    struct stat pathInfo;
    if (::stat(targetPath.c_str(), &pathInfo) != 0)
    {
        switch (errno)
        {
            case ENOENT:
            case ENOTDIR:
                return 404;
            case EACCES:
            case EPERM:
            case ELOOP:
            case ENAMETOOLONG:
                return 403;
            default:
                return 500;
        }
    }

    if (!S_ISREG(pathInfo.st_mode) && !S_ISDIR(pathInfo.st_mode))
        return 403;
    isDir = S_ISDIR(pathInfo.st_mode);
    if (isDir && targetPath[targetPath.size() - 1] != '/')
        targetPath += '/';
    return PATH_OK;
}
