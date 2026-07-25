#include "Webserv.hpp"

/**
 * @brief 默认构造函数
 */
Connection::Connection()
    : socket(NULL), config(), read_buffer(), write_buffer(),
      request(), response(),
      close_after_write(false),
      cgi_output_buffer(), cgi_body_bytes_sent(0),
      _is_cgi(false), _cgi_read_fd(-1), _cgi_write_fd(-1), _cgi_pid(-1), _cgi_started_at(0)
{
}

/**
 * @brief 有参构造函数：接管已被 accept 的客户端 FD，并绑定关联的虚拟主机配置 (RAII)
 *
 * @param clientFd 客户端的文件描述符
 * @param srv_cfg 匹配到的虚拟主机配置副本
 */
Connection::Connection(int clientFd, const ServerConfig &srv_cfg)
    : socket(new ClientSocket(clientFd)), config(srv_cfg), read_buffer(), write_buffer(),
      request(), response(),
      close_after_write(false),
      cgi_output_buffer(), cgi_body_bytes_sent(0),
      _is_cgi(false), _cgi_read_fd(-1), _cgi_write_fd(-1), _cgi_pid(-1), _cgi_started_at(0)
{
}

/**
 * @brief 析构函数：践行严格的 RAII 规范，物理终结并释放连接资源
 */
Connection::~Connection()
{
    delete this->socket;
}

// -------------------------------------------------------------
// 2. CGI 状态区: 将易错的状态修改【收拢为原子方法】！
// -------------------------------------------------------------
// 💡 保持变量只读视角 (Read-only access)，通过内部统一控制
bool Connection::isCgi() const 
{ 
    return _is_cgi; 
}

int Connection::getCgiReadFd() const 
{ 
    return _cgi_read_fd; 
}

int Connection::getCgiWriteFd() const
{ 
    return _cgi_write_fd; 
}

pid_t Connection::getCgiPid() const 
{ 
    return _cgi_pid; 
}

std::time_t Connection::getCgiStartedAt() const 
{ 
    return _cgi_started_at; 
}

std::string cgi_output_buffer; // Buffer 依然允许外部直接 append
size_t cgi_body_bytes_sent;

void Connection::startCgi(int readFd, int writeFd, pid_t pid)
{
    this->_is_cgi = true;
    this->_cgi_read_fd = readFd;
    this->_cgi_write_fd = writeFd;
    this->_cgi_pid = pid;
    this->_cgi_started_at = std::time(NULL); // 自动防卡死打点！
}


void Connection::resetCgi()
{
    this->_is_cgi = false;
    this->_cgi_read_fd = -1;
    this->_cgi_write_fd = -1;
    this->_cgi_pid = -1;
    this->_cgi_started_at = 0;
    this->cgi_body_bytes_sent = 0;
    std::string().swap(this->cgi_output_buffer);
}

void Connection::clear()
{
    // 1. 使用 std::string().swap 物理回零收缩内存容量
    std::string().swap(this->read_buffer);
    std::string().swap(this->write_buffer);

    // 2. 🧹 统一调用原子化 resetCgi() 方法，彻底复位 CGI 状态并释放 cgi_output_buffer 内存
    this->resetCgi();

    this->close_after_write = false;

    // 3. 🧹 原地重构 Request 与 Response（彻底防长连接 Keep-Alive 上下文残余污染）
    this->clearRequest();

    // 如果 Response 需要清空，亦可以像 placement new 一样重置：
    this->response.~Response();
    new (&this->response) Response(this->request);
}


void Connection::closeWriteFd() {
    this->_cgi_write_fd = -1;
}

void Connection::closeReadFd() {
    this->_cgi_read_fd = -1;
}

void Connection::clearCgiPid() {
    this->_cgi_pid = -1;
}

/**
 * @brief 原地重构请求解析器 Request（placement new 定位放置重构）
 */
void Connection::clearRequest()
{
    this->request.~Request();
    new (&this->request) Request();
}