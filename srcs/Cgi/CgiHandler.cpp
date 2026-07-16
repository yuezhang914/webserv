#include "Webserv.hpp"

/*
函数：CgiHandler带参数的构造函数
用途：实体化一个CgiHandler 类
参数：request 是 解析出的request 对象， script_path是解析出的cgi文件路径
*/
CgiHandler::CgiHandler(const Request &request, const std::string &script_path)
    : _request(request), _script_path(script_path) {}

/*
函数：CgiHandler的析构函数
用途：删除一个CgiHandler 对象
实现逻辑： 在对象离开实现范畴时自动调用
*/
CgiHandler::~CgiHandler() {}

/*
函数：CgiHandler::execute
用途：运行CGI， 获得CGI 运行后的原生response
实现逻辑： 通过CgiEnv 获得环境参数数列， 通过CgiProcess获得原生response
*/
std::string CgiHandler::execute()
{
    CgiEnv env(_request, _script_path);
    CgiProcess process(_script_path, _request.getBody());
    std::string raw_output = process.run(env);
    return raw_output;
}

/*
函数：CgiHandler::buildHttpResponse
用途：把原生CGI response 转变为符合规格的HTTP response
*/
std::string CgiHandler::buildHttpResponse(const std::string &cgi_output) const
{
    return CgiResponse::serialize(cgi_output);
}
