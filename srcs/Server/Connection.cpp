#include "Webserv.hpp"

/**
 * @brief 默认构造函数
 */
Connection::Connection()
    : socket(NULL), config(), read_buffer(), write_buffer(),
      request(), response(),
      close_after_write(false), drain_input_before_close(false),
      chunk_scan_active(false),
      chunk_scan_pos(0), chunk_decoded_size(0), chunk_body_limit(0),
      large_cgi_slot_acquired(false), large_cgi_waiting(false)
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
      close_after_write(false), drain_input_before_close(false),
      chunk_scan_active(false),
      chunk_scan_pos(0), chunk_decoded_size(0), chunk_body_limit(0),
      large_cgi_slot_acquired(false), large_cgi_waiting(false)
{
}

void Connection::closeFd()
{
    this->socket->closeFd();
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
    std::string().swap(this->read_buffer);
    std::string().swap(this->write_buffer);

    this->close_after_write = false;
    this->drain_input_before_close = false;

    this->chunk_scan_active = false;
    this->chunk_scan_pos = 0;
    this->chunk_decoded_size = 0;
    this->chunk_body_limit = 0;

    this->large_cgi_slot_acquired = false;
    this->large_cgi_waiting = false;

    this->clearRequest();

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
