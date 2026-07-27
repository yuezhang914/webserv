#include "Webserv.hpp"

/**
 * @brief 默认构造函数
 */
Connection::Connection()
    : socket(NULL), config(), read_buffer(), write_buffer(),
      request(), response(),
      close_after_write(false)
      
      
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
      close_after_write(false)
    
{
}

/**
 * @brief 析构函数：践行严格的 RAII 规范，物理终结并释放连接资源
 */
Connection::~Connection()
{
    delete this->socket;
}



void Connection::clear()
{
    // 1. 使用 std::string().swap 物理回零收缩内存容量
    std::string().swap(this->read_buffer);
    std::string().swap(this->write_buffer);

    // 2. 🧹 统一调用原子化 resetCgi() 方法，彻底复位 CGI 状态并释放 cgi_output_buffer 内存
   

    this->close_after_write = false;

    // 3. 🧹 原地重构 Request 与 Response（彻底防长连接 Keep-Alive 上下文残余污染）
    this->clearRequest();

    // 如果 Response 需要清空，亦可以像 placement new 一样重置：
    this->response.~Response();
    new (&this->response) Response(this->request);
}


/**
 * @brief 原地重构请求解析器 Request（placement new 定位放置重构）
 */
void Connection::clearRequest()
{
    this->request.~Request();
    new (&this->request) Request();
}