#include "Webserv.hpp"

/**
 * @brief 默认构造函数：构造一个空的、未绑定物理连接的 Connection 安全哨兵
 */
Connection::Connection() : socket(NULL), close_after_write(false)
{
}

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
Connection::Connection(int clientFd, const ServerConfig &srv_cfg)
    : socket(new ClientSocket(clientFd)), config(srv_cfg), close_after_write(false)
{
    this->socket->setNonBlocking();
}

/**
 * @brief 核心救星 1：拷贝构造函数（C++98 独占所有权转移机制）
 * 
 * @param other 源连接对象的常引用
 * 
 * @details 
 * 💡【解决 STL 容器拷贝消亡暗礁】：
 * 在 C++98 中，std::vector 等容器在扩容或插入时，会频繁调用拷贝构造函数并销毁临时源对象。
 * 为了防止源对象析构时连带调用 `delete socket` 物理切断套接字，这里采用“独占所有权转移”机制：
 * 1. 接管源对象的 _socket 指针。
 * 2. 强行使用 const_cast 抹除 const 限制，将源对象的 socket 指针强制设为 NULL（剥夺其所有权）。
 * 3. 这样源对象在随后的析构中，`delete NULL` 将安全通过，连接资源的生命周期被完美转交给新对象。
 */
Connection::Connection(const Connection &other) 
    : socket(other.socket), config(other.config), read_buffer(other.read_buffer), 
      write_buffer(other.write_buffer), request(other.request), close_after_write(other.close_after_write)
{
    const_cast<Connection &>(other).socket = NULL;
}

/**
 * @brief 核心救星 2：赋值运算符重载（独占所有权转移与自赋值防御）
 * 
 * @param other 右侧源连接对象的常引用
 * @return Connection& 当前连接对象的引用
 * 
 * @details 
 * 物理执行安全而彻底的覆盖装配：
 * 1. 自赋值防御：若 &other == this，则直接返回。
 * 2. 安全自清理：物理 delete 掉当前持有的旧 socket，防止旧的 ClientSocket 泄露。
 * 3. 浅拷贝托管：承接 other 的属性及 socket 物理指针。
 * 4. 所有权剥夺：const_cast 将 other.socket 置空，完成指针独占权的“接力赛”。
 */
Connection &Connection::operator=(const Connection &other)
{
    if (this != &other)
    {
        delete this->socket;
        this->socket = other.socket;
        this->config = other.config;
        this->read_buffer = other.read_buffer;
        this->write_buffer = other.write_buffer;
        this->request = other.request;
        this->close_after_write = other.close_after_write;
        const_cast<Connection &>(other).socket = NULL;
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
void Connection::clearRequest() {
    this->request.~Request();
    new (&this->request) Request();
}