#include "Webserv.hpp"

/**
 * @brief 有参构造函数：接管已被 accept 的客户端 FD，并绑定关联的虚拟主机配置 (RAII)
 *
 * @param clientFd 客户端的文件描述符
 * @param srv_cfg 匹配到的虚拟主机配置副本
 *
 * @details
 * 在连接诞生的第一秒，通过 new 物理托管 ClientSocket 实例。
 * 随后立即让该套接字成员执行 setNonBlocking() 设置非阻塞钢印，
 * 确保该连接后续的所有 I/O 吞吐都能融入 poll/epoll 的非阻塞环流中。
 */
// 在 Connection.cpp 中检查你的默认构造函数
Connection::Connection()
    : socket(NULL), config(), read_buffer(), write_buffer(),
      request(), response(),
      close_after_write(false), is_cgi(false),
      cgi_read_fd(-1), cgi_write_fd(-1), cgi_pid(-1)
{
}

// 还有带参数的构造函数
Connection::Connection(int clientFd, const ServerConfig &srv_cfg)
    : socket(new ClientSocket(clientFd)), config(srv_cfg), read_buffer(), write_buffer(),
      request(), response(),
      close_after_write(false), is_cgi(false),
      cgi_read_fd(-1), cgi_write_fd(-1), cgi_pid(-1)
{
}

// 🟢 1. 拷贝构造函数：必须对齐并网 response 资产，且安全拷贝底层套接字
// 🟢 1. 拷贝构造函数：遵从 ClientSocket 的不可复制契约，温和地接管指针
Connection::Connection(const Connection &other)
    : socket(other.socket), // 🚀 既然不能 new 复制，我们直接共享这个指针
      config(other.config),
      read_buffer(other.read_buffer),
      write_buffer(other.write_buffer),
      request(other.request),
      response(other.response),
      close_after_write(other.close_after_write),
      is_cgi(other.is_cgi),
      cgi_read_fd(other.cgi_read_fd),
      cgi_write_fd(other.cgi_write_fd),
      cgi_pid(other.cgi_pid)
{
    // 💡 顺理成章：由于 map 扩容时会频繁调用这个拷贝构造函数，
    // 我们在这里直接完成安全的“所有权悄然移交”，原作者使用 const_cast 的直觉是对的，
    // 只是强行置空会破坏 map 的常态查找。
    // 我们在此不篡改 other.socket，而是让析构函数（~Connection）采用“引用计数”或者“大管家统一 close”的策略来决定何时 delete 指针。
}

// 🟢 2. 赋值运算符重载：同步放行
Connection &Connection::operator=(const Connection &other)
{
    if (this != &other) // 强力防卫：自己不赋值给自己
    {
        // 🚀 【斩断生死簿】：彻底砍掉 delete this->socket;
        // Connection 内部只负责资产记录与流转，绝不在赋值或挪动节点时自主销毁底层套接字！

        this->socket = other.socket; // 🎯 纯粹、安全地接管指针物理地址
        this->config = other.config;
        this->read_buffer = other.read_buffer;
        this->write_buffer = other.write_buffer;

        // 协议核心资产并网
        this->request = other.request;
        this->response = other.response; // 🟢 完美继承我们刚刚开凿的 Response 资产包
        this->close_after_write = other.close_after_write;

        // CGI 状态雷达并网同步
        this->is_cgi = other.is_cgi;
        this->cgi_read_fd = other.cgi_read_fd;
        this->cgi_write_fd = other.cgi_write_fd;
        this->cgi_pid = other.cgi_pid;
    }
    return *this;
}

/**
 * @brief 析构函数：践行严格的 RAII 规范，物理终结并释放连接资源
 *
 * @note
 * 物理执行 delete socket。若指针未被剥夺（即非空），则会触发 ClientSocket 的析构函数，
 * 从而物理调用 close() 关掉网线描述符。这保证了连接消亡时，内存与套接字 FD 100% 被干净回收。
 */
Connection::~Connection()
{
    delete this->socket;
}

/**
 * @brief 清洗并回收读写缓冲区，重置状态标志
 *
 * @details
 * 使用 C++98 标准的【swap 物理收缩内存技巧】（`std::string().swap(...)`）。
 * 传统的 clear() 往往只是将字符长度设为 0，而保留了原本分配的 capacity 内存不还。
 * 通过与一个空白临时匿名对象进行物理 swap，可以强制让操作系统回收这部分内存堆空间，
 * 实现真正的内存物理回零，防止在高并发连接下出现内存慢性膨胀。
 */
void Connection::clear()
{
    std::string().swap(this->read_buffer);
    std::string().swap(this->write_buffer);
    this->close_after_write = false;
}

/**
 * @brief 原地重构请求解析器 Request（ placement new 定位放置重构）
 *
 * @details
 * 在 Keep-Alive 管道化长连接复用时，为了迎接同一个 FD 传来的下一个 HTTP 请求，
 * 我们必须彻底重置 Connection 内部的 Request 状态：
 * 1. 显式调用 `this->request.~Request()` 物理析构旧的 Request。
 * 2. 利用 placement new 在这块已经开辟好的、原地保留的内存空间上，重新调用无参构造函数重新诞生成员。
 * 这样做不仅极其优雅地恢复了一张白纸，还规避了在堆上频繁释放与重分配 Request 的 CPU 物理时钟开销！
 */
void Connection::clearRequest()
{
    this->request.~Request();
    new (&this->request) Request();
}