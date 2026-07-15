#include "Connection.hpp"

Connection::Connection()
    : fd(-1), io(-1), close_after_write(false)
{
}

Connection::Connection(int clientFd, const ServerConfig &srv_cfg)
    : fd(clientFd), config(srv_cfg), io(clientFd), close_after_write(false)
{
}

Connection::~Connection()
{
}

void Connection::clear()
{
    // 1.【内存防爆】：清空读缓冲区，并用 C++98 swap 物理释放写缓冲区
    std::string().swap(this->read_buffer); // 读缓冲区也用 swap释放
    this->io.clear();                      // 写缓冲区释放

    // 2.【逻辑洗白】：物理洗干净自毁标记，防止误杀下一个 Keep-Alive 请求
    this->close_after_write = false; 

    // 3. 【状态归零】
    // this->request.clear(); 
}