/*
文件：srcs/Config/LocationConfig.cpp
location 块解析和 LocationConfig 生命周期实现。它把 location 内的 root/alias/index/cgi_extension/upload_path/return 等指令写入 LocationConfig。
*/
#include "LocationConfig.hpp"
#include "Config.hpp"

/*
函数：Config::parseLocationDirective
用途：解析 location { ... } 里面的一条指令，并写入当前 LocationConfig。
参数来源：parseDirective 在 current_location 不为空时调用。
变量解释：
	- directive：当前 location 指令名，例如 allow_methods、cgi_extension、return。
	- values：当前指令参数数组，来自 parseDirectiveTokens。
	- loc：当前正在填充的 LocationConfig 指针。
	- i：遍历 values 的下标，用于 allow_methods 分支。
	- status：return 分支解析出的 3xx 重定向状态码。
	- endptr：strtol 输出参数，用来判断 return 状态码是否是纯数字。
实现逻辑：
	1. allow_methods：把方法加入 loc->allow_methods。
	2. root：检查参数数量，检查不能重复，也不能和 alias 同时使用，然后写入 loc->root 并设置 has_root。
	3. autoindex/directory_listing：检查 on/off，然后设置 loc->autoindex 和 loc->has_autoindex。
	4. index：设置该 location 的默认首页文件。
	5. cgi_extension：要求两个参数，后缀和解释器路径，写入 loc->cgi_extensions。
	6. upload_path：设置 POST 上传目录。
	7. return：要求状态码和 URL；状态码必须是 300-399；写入 redirect_status/redirect_url。
	8. alias：要求一个参数，并检查不能和 root 同时使用；写入 loc->alias，并设置 has_alias=true。
	9. 未知指令返回 ERROR。
产出：LocationConfig 成为某个 URI 前缀的特殊规则，后续最长前缀匹配会用它。
*/
bool Config::parseLocationDirective(const std::string &directive, const std::vector<std::string> &values, LocationConfig *loc)
{
    if (directive == "allow_methods")
    {
        if (values.size() >= 1)
        {
            for (size_t i = 0; i < values.size(); ++i)
            {
                /* 🎯 【修改点 1：空值过滤与格式规整】 */
                if (values[i].empty())
                {
                    std::cerr << "Error: Empty method token in allow_methods" << std::endl;
                    return ERROR;
                }
                std::string method = values[i];
                for (size_t j = 0; j < method.size(); ++j)
                    method[j] = std::toupper(method[j]); // 💡 强行强制转大写

                /* 🎯 【修改点 2：严格谓词白名单硬卡点】 */
                if (method != "GET" && method != "POST" && method != "DELETE") 
                {
                    std::cerr << "Error: Unsupported HTTP method: " << values[i] << std::endl;
                    return ERROR;
                }
                loc->allow_methods.insert(method);
            }
        }
        else
            loc->allow_methods.insert("NONE");
    }
    else if (directive == "root")
    {
        if (values.size() != 1)
        {
            std::cerr << "Error: Invalid root directive" << std::endl;
            return ERROR;
        }
        if (!loc->root.empty())
        {
            std::cerr << "Error: Duplicate root directive in location " << loc->path << std::endl;
            return ERROR;
        }
        if (!loc->alias.empty())
        {
            std::cerr << "Error: Cannot use root and alias together in location " << loc->path << std::endl;
            return ERROR;
        }
        loc->root = values[0];
        loc->has_root = true;
    }
    else if (directive == "autoindex" || directive == "directory_listing")
    {
        if (values.size() != 1)
        {
            std::cerr << "Invalid " << directive << " directive" << std::endl;
            return ERROR;
        }
        if (values[0] == "on")
        {
            loc->autoindex = true;
            loc->has_autoindex = true;
        }
        else if (values[0] == "off")
        {
            loc->autoindex = false;
            loc->has_autoindex = true;
        }
        else
        {
            std::cerr << "Invalid " << directive << " directive value: " << values[0] << std::endl;
            return ERROR;
        }
    }
    else if (directive == "index")
    {
        /* 🎯 【修改点 3：解除单首页限制，恢复多首页多级Fallback】 */
        if (values.size() < 1)
        {
            std::cerr << "Error: Invalid index directive" << std::endl;
            return ERROR;
        }
        /* 🎯 【修改点 4：并线多重配置清空锁，复刻 Nginx 官方覆盖语义】 */
        loc->index.clear();
        /* 🎯 【修改点 5：修复 int 与 size_t 类型不齐导致的强类型编译报错】 */
        for (size_t i = 0; i < values.size(); ++i)
            loc->index.push_back(values[i]);
    }
    else if (directive == "cgi_extension")
    {
        if (values.size() != 2)
        {
            std::cerr << "Error: Invalid cgi_extension directive" << std::endl;
            return ERROR;
        }
        /* 🎯 【修改点 6：前置后缀形态断言，封杀空穿透引发的 RCE 漏洞】 */
        if (values[0].empty() || values[0][0] != '.' || values[1].empty())
        {
            std::cerr << "Error: Invalid cgi_extension format. Expected e.g., .py path" << std::endl;
            return ERROR;
        }
        loc->cgi_extensions[values[0]] = values[1];
    }
    else if (directive == "upload_path")
    {
        if (values.size() != 1)
        {
            std::cerr << "Error: Invalid upload_path directive" << std::endl;
            return ERROR;
        }
        loc->upload_path = values[0];
    }
    else if (directive == "return")
    {
        if (values.size() != 2)
        {
            std::cerr << "Error: Invalid return directive" << std::endl;
            return ERROR;
        }
        char *endptr;
        int status = strtol(values[0].c_str(), &endptr, 10);
        if (*endptr != '\0' || status < 300 || status > 399)
        {
            std::cerr << "Error: Invalid return status code: " << values[0] << std::endl;
            return ERROR;
        }
        loc->redirect_status = status;
        loc->redirect_url = values[1];
    }
    else if (directive == "alias")
    {
        if (values.size() != 1)
        {
            std::cerr << "Invalid alias directive" << std::endl;
            return ERROR;
        }
        if (!loc->root.empty())
        {
            std::cerr << "Error: Cannot use root and alias together in location " << loc->path << std::endl;
            return ERROR;
        }

        /* 🎯 【修改点 7：为 alias 并线防重单体锁，拦截配置覆盖】 */
        if (!loc->alias.empty())
        {
            std::cerr << "Error: Duplicate alias directive in location " << loc->path << std::endl;
            return ERROR;
        }
        loc->alias = values[0];
        loc->has_alias = true;
    }
    else
    {
        std::cerr << "Error: Unknown location directive: " << directive << std::endl;
        return ERROR;
    }
    /* 🎯 【修改点 8：统一状态标志，由裸数字 0 升级为 SUCCESS 语义互锁】 */
    return SUCCESS;
}

/*
函数：LocationConfig::LocationConfig
用途：创建一个带默认值的 location 配置对象。
变量解释：
	- allow_methods/cgi_extensions：容器字段，默认空。
	- root/index/upload_path/path/redirect_url/alias：字符串字段，默认空。
	- autoindex：目录列表开关，默认 false。
	- has_autoindex：是否显式写过 autoindex，默认 false。
	- redirect_status：重定向状态码，默认 0 表示无重定向。
	- has_root/has_alias：是否显式写过 root/alias，默认 false。
实现逻辑：
	1. 字符串字段设置为空。
	2. autoindex=false，has_autoindex=false，redirect_status=0。
	3. allow_methods/cgi_extensions 等容器默认空。
	4. has_root/has_alias=false，表示还没有显式配置 root 或 alias。
*/
LocationConfig::LocationConfig()
	: allow_methods(), root(""), autoindex(false), has_autoindex(false), index(""), cgi_extensions(), upload_path(""), path(""), redirect_status(0), redirect_url(""), alias(""), has_root(false), has_alias(false)
{
}

/*
函数：LocationConfig 拷贝构造
用途：复制一个 location 配置对象。
变量解释：
	- src：被复制的 LocationConfig。
	- allow_methods/root/autoindex/has_autoindex/index/cgi_extensions/upload_path/path/redirect_status/redirect_url/alias/has_root/has_alias：都从 src 对应字段复制。
实现逻辑：逐个复制 allow_methods、root、autoindex、has_autoindex、index、cgi_extensions、upload_path、path、redirect、alias、has_root、has_alias。
使用场景：vector<LocationConfig> 扩容或复制 ServerConfig 时会用到。
*/
LocationConfig::LocationConfig(const LocationConfig &src)
	: allow_methods(src.allow_methods), root(src.root), autoindex(src.autoindex), has_autoindex(src.has_autoindex), index(src.index), cgi_extensions(src.cgi_extensions), upload_path(src.upload_path), path(src.path), redirect_status(src.redirect_status), redirect_url(src.redirect_url), alias(src.alias), has_root(src.has_root), has_alias(src.has_alias)
{
}

/*
函数：LocationConfig::operator=
用途：把 rhs 的 location 规则赋值给当前对象。
变量解释：
	- rhs：赋值来源对象。
	- this：当前被赋值对象；如果 this == &rhs，说明是自我赋值。
	- 各成员字段：在非自我赋值时逐一复制 rhs 的配置。
实现逻辑：
	1. 检查是否 self-assignment。
	2. 复制所有配置字段和标志位。
	3. 返回 *this。
*/
LocationConfig &LocationConfig::operator=(const LocationConfig &rhs)
{
	if (this != &rhs)
	{
		allow_methods = rhs.allow_methods;
		root = rhs.root;
		autoindex = rhs.autoindex;
		has_autoindex = rhs.has_autoindex;
		index = rhs.index;
		cgi_extensions = rhs.cgi_extensions;
		upload_path = rhs.upload_path;
		path = rhs.path;
		redirect_status = rhs.redirect_status;
		redirect_url = rhs.redirect_url;
		alias = rhs.alias;
		has_root = rhs.has_root;
		has_alias = rhs.has_alias;
	}
	return *this;
}

/*
函数：LocationConfig::~LocationConfig
用途：销毁 LocationConfig。
变量解释：
	- allow_methods/cgi_extensions 等容器：由标准库自动释放。
	- 字符串和 bool/int 字段：无需手动处理。
实现逻辑：没有手动管理的 fd 或堆内存，string/map/set 自动析构即可。
*/
LocationConfig::~LocationConfig() {}
