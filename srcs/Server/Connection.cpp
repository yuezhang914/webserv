#include "Connection.hpp"

Connection::Connection() : fd(-1), config(), read_buffer(), io()
{
}

Connection::Connection(int clientFd, const ServerConfig &srv_cfg)
    : fd(clientFd), config(srv_cfg), read_buffer(), io(clientFd)
{
}

Connection::~Connection()
{
}

// 一键重置长连接状态（Keep-Alive 复位）
void Connection::clear()
{
    this->read_buffer.clear();
    this->io.clear(); // 物理释放 5MB 的 string 虚胖容量
    // 未来可以在这里重置队友的 HTTPParser 状态机
}

const ServerConfig& Connection::getConfig()const
{
    return this->config;
}
