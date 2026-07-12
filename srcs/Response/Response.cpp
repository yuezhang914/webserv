/*
文件：srcs/Response/Response.cpp
HTTP response 核心实现。这里保留 buildResponse 分发入口、Response 对象基础方法、状态码和错误页逻辑；GET/POST/DELETE 具体动作已拆到 RequestHandler.cpp。
*/
#include "Response.hpp"
#include "ResponseUtils.hpp"
#include "RequestHandler.hpp"

/*
函数：buildAllowHeader
用途：把 EffectiveRoute.allow_methods 转成 405 response 需要的 Allow header 文本。
参数来源：eff.allow_methods，它已经合并了 location、server 和默认方法规则。
实现逻辑：
    1. 按 GET、POST、DELETE 的固定顺序检查集合。
    2. 多个方法之间用逗号和空格连接。
    3. 返回例如 "GET, POST" 的字符串。
修改意义：405 Method Not Allowed 应明确告诉 client 当前 route 允许哪些已实现方法。
*/
static std::string buildAllowHeader(const std::set<std::string>& allow_methods) {
	const char* ordered_methods[] = {"GET", "POST", "DELETE"};
	std::string result;
	for (size_t i = 0; i < 3; ++i) {
		if (allow_methods.find(ordered_methods[i]) == allow_methods.end())
			continue;
		if (!result.empty())
			result += ", ";
		result += ordered_methods[i];
	}
	return result;
}

/*
函数：buildResponse
用途：把 RequestParser 生成的完整 Request 转成普通 HTTP Response，并在 CGI 尚未实现时安全拦截脚本请求。
参数来源：request 由 ServerManager 在 parseRequestBuffer() 返回 REQUEST_OK 后传入。
变量解释：
    - res：当前主流程使用的 Response，负责生成 CGI fallback、错误、重定向、405 和 501。
    - loc/use_location：根据 request.uri 做最长前缀匹配得到的 location 结果。
    - eff：把 server 默认配置和 location 覆盖配置合并后的最终路由。
    - result：真实路径检查结果，PATH_OK 表示可以继续 GET/POST/DELETE。
返回值：返回已经填好状态码、headers、body 和 closingConnection 的 Response。
实现逻辑：
    1. 先继承 request.closeConnection，保证所有提前返回分支都使用正确连接策略。
    2. 匹配当前 URI 对应的 location。
    3. 如果 location 把当前扩展名配置为 CGI，但 CGI 执行模块尚未实现，立即返回 501。
    4. CGI fallback 必须发生在真实文件路径检查和 handleGet() 之前，避免把脚本源码作为静态文件泄露。
    5. 创建 EffectiveRoute，合并 server 和 location 规则。
    6. 处理 redirect；redirect 不依赖本地文件是否存在。
    7. 非 GET/POST/DELETE 方法返回 501；已实现但 route 不允许的方法返回 405，并设置 Allow。
    8. 创建并检查真实路径，再分发给 handleGet、handlePost 或 handleDelete。
当前阶段边界：本文件不包含 SessionManager，也不包含或调用任何 CGI 执行模块；以后 CGI 完成后，只需把安全 501 分支替换成 CGI 调度入口。
*/
Response buildResponse(const Request& request) {
	Response res;
	res.closingConnection = request.closeConnection;
	bool use_location = false;
	LocationConfig* loc = getMatchingLocation(request, request.config, res, use_location);

	if (use_location && isCGIRequest(loc, request.uri)) {
		res.createResponse(501, "CGI execution is not implemented yet.", request.config->error_pages);
		res.closingConnection = request.closeConnection;
		res.setClosingConnection();
		return res;
	}

	EffectiveRoute eff;
	eff.getMethod = request.method == "GET";
	eff.closeConnection = request.closeConnection;
	if (use_location && eff.createEffectiveRoute(request.config, loc) == false) {
		res.createResponse(500, "", request.config->error_pages);
		return res;
	}
	if (!use_location && eff.createEffectiveRoute(request.config) == false) {
		res.createResponse(500, "", request.config->error_pages);
		return res;
	}

	if (use_location && eff.redirect_status >= 300 && eff.redirect_status <= 399 && !eff.redirect_url.empty()) {
		res.statusCode = eff.redirect_status;
		res.statusMessage = getStatusMessage(eff.redirect_status);
		res.headers["Location"] = eff.redirect_url;
		res.body = "<!DOCTYPE html><html><head><title>Redirect</title></head><body>Redirecting</body></html>";
		res.headers["Content-Type"] = "text/html";
		res.headers["Content-Length"] = toString<size_t>(res.body.size());
		res.setClosingConnection();
		return res;
	}

	int action = 0;
	if (request.method == "GET")
		action = GET;
	else if (request.method == "POST")
		action = POST;
	else if (request.method == "DELETE")
		action = DELETE;
	else {
		res.createResponse(501, "", eff.server->error_pages);
		return res;
	}

	if (!isMethodAllowed(action, eff.allow_methods)) {
		res.createResponse(405, "", eff.server->error_pages);
		res.headers["Allow"] = buildAllowHeader(eff.allow_methods);
		return res;
	}

	int result = eff.createEffectivePath(request.uri);
	if (result != PATH_OK) {
		res.createResponse(result, "", eff.server->error_pages);
		return res;
	}

	if (action == GET)
		return handleGet(request, eff);
	if (action == POST)
		return handlePost(request, eff);
	return handleDelete(request, eff);
}

/*
函数：checkRequestVersion
用途：再次确认请求 HTTP 版本是否是项目唯一支持的 HTTP/1.1。
参数来源：version 来自 Request.version；response 和 error_pages 由 GET/POST/DELETE 处理函数传入。
实现逻辑：
    1. 使用完整字符串比较 version == "HTTP/1.1"，不能只做前缀比较。
    2. 不相等时调用 response.createResponse(505) 生成错误响应并返回 ERROR。
    3. 完全相等时返回 SUCCESS。
修改说明：修复旧 compare(0, version.size(), allowedVersion) 可能把 HTTP/1 等前缀误判为合法的问题；虽然 RequestParser 已严格检查，这里仍保留独立防线。
*/
int checkRequestVersion(const std::string& version, Response& response, std::map<int, std::string> error_pages) {
	const std::string allowedVersion = "HTTP/1.1";
	if (version != allowedVersion) {
		response.createResponse(505, "", error_pages);
		return ERROR;
	}
	return SUCCESS;
}

/*
函数：Response::responseToString
用途：把 Response 对象转成真正通过 socket 发送的 HTTP 文本。
实现逻辑：
    1. 先拼状态行：HTTP/1.1 200 OK
。
    2. 遍历 headers，每个 header 拼成 Key: Value
。
    3. 拼一个空行 
，表示 header 结束。
    4. 追加 body。
    5. 返回完整字符串给 ServerManager，再由 ClientIO 写入 client socket。
*/
std::string Response::responseToString() const {
	std::ostringstream oss;

	oss << version << " " << statusCode << " " << statusMessage << "\r\n";
	for (std::map<std::string, std::string>::const_iterator it = headers.begin(); it != headers.end(); ++it) {
		oss << it->first << ": " << it->second << "\r\n";
	}
	oss << "\r\n" << body;
	return oss.str();
}

/*
函数：Response::createResponse
用途：快速生成标准 response，特别适合错误页和简单文本响应。
实现逻辑：
    1. 设置 version=HTTP/1.1、statusCode=code、statusMessage=getStatusMessage(code)。
    2. 判断该状态码是否允许有 body：1xx、204、304 不应该有 body。
    3. 如果允许 body 且 bodyText 非空，就把 bodyText 放入 body，并默认 Content-Type=text/plain。
    4. 否则清空 body，并删除 Content-Type。
    5. 调用 setClosingConnection 设置 Connection header。
    6. 如果是错误状态码，优先尝试 setCustomErrorPage；失败则 setDefaultErrorPage。
    7. 最后设置 Content-Length=body.size()。
*/
void Response::createResponse(unsigned int code, const std::string& bodyText, std::map<int, std::string> error_pages) {
    version = "HTTP/1.1";
    statusCode = code;
    statusMessage = getStatusMessage(code);

    const bool mayHaveBody = !( (code >= 100 && code < 200) || code == 204 || code == 304 );

    if (mayHaveBody && !bodyText.empty()) {
        body = bodyText;
        if (headers.find("Content-Type") == headers.end())
            headers["Content-Type"] = "text/plain";
    } else {
        body.clear();
        headers.erase("Content-Type");
    }

	setClosingConnection();
	if (isErrorStatusCode(statusCode)) {
		if (setCustomErrorPage(error_pages) == false)
			setDefaultErrorPage();
	}
    headers["Content-Length"] = toString<size_t>(body.size());
}

/*
函数：Response::setClosingConnection
用途：决定 Connection header 是 close 还是 keep-alive。
实现逻辑：
    1. 如果 closingConnection 已经是 true，直接设置 Connection: close。
    2. 如果不是强制关闭，再检查状态码。
    3. 某些错误状态如 400、408、413、505 等为了安全会强制关闭连接。
    4. 其他状态保持 keep-alive。
*/
void Response::setClosingConnection(void) {
    if (closingConnection) {
        headers["Connection"] = "close";
    } else {
        switch (statusCode) {
            case 400:
            case 408:
			case 409:
            case 411:
            case 413:
            case 414:
            case 431:
            case 505:
                headers["Connection"] = "close";
                closingConnection = true;
                break;
            default:

                headers["Connection"] = "keep-alive";
                break;
        }
    }
}

/*
函数：getStatusMessage
用途：把 HTTP 状态码转成状态行使用的标准短语。
实现逻辑：
    1. switch 匹配项目会生成的成功、重定向、client error 和 server error 状态码。
    2. 常见 3xx 返回标准短语；其他 300-399 返回通用 Redirect。
    3. 完全未知状态返回 Unknown。
修改说明：补入 301/302/303/307/308 和 501，避免 redirect 或未知 method 响应出现 "Unknown"。
*/
std::string getStatusMessage(int statusCode) {
	switch(statusCode) {
		case 200: return "OK";
		case 201: return "Created";
		case 204: return "No Content";
		case 300: return "Multiple Choices";
		case 301: return "Moved Permanently";
		case 302: return "Found";
		case 303: return "See Other";
		case 304: return "Not Modified";
		case 307: return "Temporary Redirect";
		case 308: return "Permanent Redirect";
		case 400: return "Bad Request";
		case 403: return "Forbidden";
		case 404: return "Not Found";
		case 405: return "Method Not Allowed";
		case 409: return "Directory does not Exists";
		case 411: return "Length Required";
		case 413: return "Payload Too Large";
		case 414: return "URI Too Long";
		case 415: return "Unsupported Media Type";
		case 423: return "Locked";
		case 500: return "Internal Server Error";
		case 501: return "Not Implemented";
		case 504: return "Gateway Timeout";
		case 505: return "HTTP Version Not Supported";
	}
	if (statusCode >= 300 && statusCode <= 399)
		return "Redirect";
	return "Unknown";
}

/*
函数：Response::setHeader
用途：安全设置 Content-Type 或 Content-Length。
实现逻辑：
    1. 只有当 headers 中还没有该 header 时才设置，避免覆盖已经明确设置过的值。
    2. 当前只处理 Content-Type 和 Content-Length。
*/
void Response::setHeader(std::string header, std::string content) {
	if (headers.find(header) == headers.end()) {
		if (header == "Content-Type")
        	headers["Content-Type"] = content;
		else if (header == "Content-Length")
        	headers["Content-Length"] = content;
	}
	return ;
}

/*
函数：isErrorStatusCode
用途：判断某个状态码是否应该尝试加载配置里的自定义错误页。
实现逻辑：switch 列出项目会生成并允许配置错误页的 4xx/5xx 状态；命中返回 true，否则 false。
修改说明：加入 501 Not Implemented，使未知 HTTP method 可以使用 default.conf 中配置的 501 页面。
*/
bool isErrorStatusCode(int statusCode) {
	switch (statusCode)
	{
		case 400:
		case 403:
		case 404:
		case 405:
		case 409:
		case 411:
		case 413:
		case 414:
		case 415:
		case 423:
		case 500:
		case 501:
		case 505:
			return true;

		default:
			return false;
	}
	return true;
}

/*
函数：Response::setCustomErrorPage
用途：如果配置了 error_page，就把自定义错误页面读入 body。
实现逻辑：
    1. 在 error_pages 中查找当前 statusCode。
    2. 没找到返回 false，让 createResponse 使用默认错误页。
    3. open 配置的错误页路径。
    4. stat 检查它是普通文件。
    5. 清空 body，调用 writeCustomErrorToBody 读取文件内容。
    6. 成功后直接覆盖 Content-Type=text/html，避免原 bodyText 预先写入的 text/plain 阻止 HTML 类型更新。
    7. 返回 true。
修改说明：自定义 HTML 错误页必须覆盖旧 Content-Type，而不能使用“只在不存在时设置”的 setHeader()。
*/
bool Response::setCustomErrorPage(std::map<int, std::string> error_pages) {
	std::map<int, std::string>::iterator it = error_pages.find(statusCode);
	if (it == error_pages.end())
		return false;

	int fd = open(it->second.c_str(), O_RDONLY);
	if (fd < 0) {
		return false;
	}

	struct stat st;
	if (stat(it->second.c_str(), &st) != 0) {
		close(fd);
		return false;
	} else if (!S_ISREG(st.st_mode)) {
		close(fd);
		return false;
	}

	body.clear();
	if (!writeCustomErrorToBody(fd)) {
		body.clear();
		return false;
	}

	headers["Content-Type"] = "text/html";
	headers["Content-Length"] = toString<size_t>(body.size());
	return true;
}

/*
函数：Response::writeCustomErrorToBody
用途：从已经打开的错误页 fd 中读取内容到 Response.body。
实现逻辑：
    1. 准备 64KB buffer。
    2. 循环 read(fd) 直到 EOF。
    3. 每次读到的数据 append 到 body。
    4. 读完 close(fd)。
    5. 如果 read 出错，清空 body 并返回 false。
    6. 成功后直接按最终 body.size() 覆盖 Content-Length，并返回 true。
修改说明：避免 Response 中旧的 Content-Length 已存在时，setHeader() 因不覆盖而留下错误长度。
*/
bool Response::writeCustomErrorToBody(int fd) {
	const size_t BUFSZ = 64 * 1024;
    std::vector<char> buf(BUFSZ);
    ssize_t n;

    while ((n = read(fd, &buf[0], BUFSZ)) > 0)
		body.append(&buf[0], static_cast<std::string::size_type>(n));
    close(fd);

    if (n < 0) {
		body.clear();
		return false;
	}

	headers["Content-Length"] = toString<size_t>(body.size());
	return true;
}

/*
函数：Response::setDefaultErrorPage
用途：当没有配置自定义错误页或读取失败时，生成一个最简单的 HTML 错误页。
实现逻辑：
    1. 设置 Content-Type=text/html。
    2. 拼接 <!DOCTYPE html>、html/head/title/body/h1。
    3. 把 statusCode 和 statusMessage 放入 title 和 h1。
    4. 设置 Content-Length。
*/
void Response::setDefaultErrorPage() {
	headers["Content-Type"] = "text/html";
	body = 	"<!DOCTYPE html>";
	body +=	"<html>";
	body +=	"<head><title>";
	body += toString<int>(statusCode);
	body += statusMessage;
	body += "</title></head>";
	body +=	"<body>";
	body +=	"<h1>";
	body += toString<int>(statusCode);
	body += statusMessage;
	body +=	"</h1>";
	body +=	"</body>";
	body +=	"</html>";

    headers["Content-Length"] = toString<size_t>(body.size());
}

/*
函数：Response::Response
用途：初始化空响应对象。
实现逻辑：
    1. version 默认 HTTP/1.1。
    2. statusCode=0，statusMessage/body 为空。
    3. closingConnection=false，默认可 keep-alive。
    4. 清空 headers。
*/
Response::Response()
	: version("HTTP/1.1"), statusCode(0), statusMessage(""), body(""), closingConnection(false)
{
	headers.clear();
}