#include "Webserv.hpp"

Connection::Connection() : socket(NULL), close_after_write(false)
{
}

Connection::Connection(int clientFd, const ServerConfig &srv_cfg)
    : socket(new ClientSocket(clientFd)), config(srv_cfg), close_after_write(false)
{
    // 诞生的第一秒，让 socket 成员自己执行非阻塞自洗，大管家再也不用操心了！
    this->socket->setNonBlocking();
}

// 核心救星 1：拷贝构造函数（转移 socket 指针所有权，防止被临时对象析构销毁）
Connection::Connection(const Connection &other) : socket(other.socket), config(other.config), read_buffer(other.read_buffer), write_buffer(other.write_buffer), request(other.request), close_after_write(other.close_after_write)
{
    // 强制类型转换，将源对象的指针设为 NULL（放弃所有权）
    const_cast<Connection &>(other).socket = NULL;
}

// 核心救星 2：赋值运算符
Connection &Connection::operator=(const Connection &other)
{
    if (this != &other)
    {
        delete this->socket; // 释放旧的
        this->socket = other.socket;
        this->config = other.config;
        this->read_buffer = other.read_buffer;
        this->write_buffer = other.write_buffer;
        this->request = other.request;
        this->close_after_write = other.close_after_write;

        // 剥夺被拷贝对象的所有权
        const_cast<Connection &>(other).socket = NULL;
    }
    return *this;
}

Connection::~Connection()
{
    // 销毁时，指针被 delete，ClientSocket 析构函数自动执行物理 close()，绝无泄漏！
    delete this->socket;
}

void Connection::clear()
{
    std::string().swap(this->read_buffer);
    std::string().swap(this->write_buffer);
    this->close_after_write = false;
}

void Connection::clearRequest() {
    // 原地析构旧的，重新 placement new 构造一个新的白纸 Request
    this->request.~Request();
    new (&this->request) Request();
}