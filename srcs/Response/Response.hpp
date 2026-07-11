#ifndef RESPONSE_HPP
#define RESPONSE_HPP

#include <string>
#include <iostream>
#include <map>
#include <sstream>
#include "../Request/Request.hpp"
#include "EffectiveRoute.hpp"
#include "RequestAction.hpp"

/*
结构体：Response
作用：保存一次 HTTP response 的全部内容，最后通过 responseToString() 变成真正发给 curl/浏览器的字符串。
从哪来：buildResponse()/handleGet()/handlePost()/handleDelete()/finalizeCGIResponse() 创建或填充它。
给谁用：ServerManager 调用 responseToString() 得到完整 HTTP 文本，再交给 ClientIO::pushWriteBuffer()/writeToNet() 分批发送。
*/
struct Response {
	/* HTTP 版本，通常固定为 HTTP/1.1。 */
	std::string version;
	/* 状态码，例如 200、404、500。来源：业务处理结果或 createResponse 参数。 */
	int statusCode;
	/* 状态短语，例如 OK、Not Found。来源：getStatusMessage(statusCode)。 */
	std::string statusMessage;
	/* 响应头表，例如 Content-Length、Content-Type、Connection、Location。 */
	std::map<std::string, std::string> headers;
	/* 响应正文。静态文件内容、错误页 HTML、CGI 输出 body 都会放这里。 */
	std::string body;
	/* 是否在发送完成后关闭连接。来源：Request.closeConnection、错误状态、CGI 超时等。 */
	bool closingConnection;

	Response();
	std::string responseToString() const;
	void createResponse(unsigned int code, const std::string& bodyText, std::map<int, std::string> error_pages);
	void setHeader(std::string header, std::string content);
	void setClosingConnection(void);
	bool setCustomErrorPage(std::map<int, std::string> error_pages);
	bool writeCustomErrorToBody(int fd);
	void setDefaultErrorPage(void);
};

/* 模板函数：把 int/size_t 等值转成 string。主要用于 Content-Length 和端口等需要拼接成文本的地方。 */
template <typename T>
std::string toString(T value) {
	std::ostringstream oss;
	oss << value;
	return oss.str();
}

/*
结构体：File
作用：辅助 POST 上传和 DELETE 删除时保存文件相关中间状态。
从哪来：handlePost()/handleDelete() 在函数内部创建临时 File。
给谁用：File 的成员函数负责提取文件名、检查 Content-Length/Content-Type、生成唯一文件名、创建文件、生成删除错误响应。
*/
struct File {
	/* 目标文件名。来源：EffectiveRoute.uri 最后一个 / 后面的部分。 */
	std::string fileName;
	/* 目标完整文件路径。POST 时由 generateUniqueFilename 生成；DELETE 时直接等于 eff.uri。 */
	std::string filePath;
	/* 请求 body 应有长度。普通请求来自 Content-Length；chunked 请求来自解码后的 request.body.size()。 */
	size_t length;
	/* 处理 POST/DELETE 过程中的响应对象。出错时成员函数直接往这里 createResponse。 */
	Response response;

	int getFileName(const EffectiveRoute& eff);
	int getFileData(const Request& request, const EffectiveRoute& eff);
	bool isValidContentLength(const std::string& contentLength);
	int createFile(const std::string& body, const EffectiveRoute& eff);
	bool fileExists(const std::string fullPath) const;
	std::string generateUniqueFilename(const EffectiveRoute& eff);
	void createDeleteResponse(const int err, std::map<int, std::string> error_pages);
	int checkContentType(const std::string &contentType) const;
};

Response buildResponse(const Request& request);
std::string getStatusMessage(int statusCode);
int checkRequestVersion(const std::string& version, Response& response, std::map<int, std::string> error_pages);
bool isErrorStatusCode(int statusCode);

#endif