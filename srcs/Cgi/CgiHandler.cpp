#include "Webserv.hpp"


CgiHandler::CgiHandler(const Request &request, const std::string &script_path)
    : _request(request), _script_path(script_path) {}

CgiHandler::~CgiHandler() {}

std::string CgiHandler::execute() {
    // 1. 让 CgiEnv 装箱环境变量（RAII 机制：离开作用域后堆内存自动释放，极度防弹！）
    CgiEnv env(_request, _script_path);

    // 2. 扔给 CgiProcess 多进程管道执行官去拼杀，拉回 RAW 输出数据
    CgiProcess process(_script_path, _request.getBody());
    std::string raw_output = process.run(env);

    return raw_output;
}

std::string CgiHandler::buildHttpResponse(const std::string &cgi_output) const {
    // 3. 扔给 CgiResponse 报文包装厂，套上完好的 HTTP 外壳
    return CgiResponse::serialize(cgi_output);
}
