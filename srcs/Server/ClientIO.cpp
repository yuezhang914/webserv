#include "Webserv.hpp"

/**
 * 函数：ClientIO::ClientIO
 * 用途：默认构造函数，用于在尚未分配物理客户端套接字时，在账本或 map 容器中安全占位。
 * 参数来源：当在 std::map<int, ClientIO> 中以操作符 [] 首次开辟空间时，由系统自发呼叫。
 * 变量解释：
 *     - _fd：类的私有成员，初始化为 -1，代表此时通道是一个处于离线或无效状态的空壳。
 *     - _write_buf：类的私有成员，显式调用 std::string 的默认构造，确保其物理长度为 0 的空字符串。
 * 实现逻辑：
 *     1. 物理降维安全初始化。通过成员初始化列表将 _fd 锁死在 -1 安全线。
 *     2. 显式触发写蓄水池的构造，绝不留下任何未经初始化的内存野数据（C++98 标配防线）。
 * 后续影响：在内存中产生一个干净、安全的离线网络搬运工节点，防范任何误触。
 */
ClientIO::ClientIO() : _fd(-1), _write_buf()
{
}

/**
 * 函数：ClientIO::ClientIO
 * 用途：有参构造函数，专门用来在 accept() 成功捕捉到客人时，为其量身打造一个客户端专属的底层网络传输车间。
 * 参数来源：来自 ServerManager::acceptNewConnection()。当内核成功诞生一个新的客户端套接字时，将其物理 fd 作为参数传入。
 * 变量解释：
 *     - fd：由操作系统物理分配的、代表该浏览器与服务器通信公路的有效文件描述符（File Descriptor）。
 *     - _fd：类私有资产，用于永久锁死并保管传入的 fd，供后续读写函数持续调用。
 * 实现逻辑：
 *     1. 物理资产移交。在初始化列表中将外部传入的物理套接字 fd 永久登记进本搬运工的私有变量 `_fd` 中。
 *     2. 同步将本客户端专属的私有写蓄水池 `_write_buf` 初始化为空，干净清爽地准备承接该客人的首次数据交互。
 * 后续影响：该套接字被物理绑定给当前 ClientIO 搬运工。大管家此后只要拿着对应的 clientFd，就能随时操控其物理吞吐。
 */
ClientIO::ClientIO(int fd) : _fd(fd), _write_buf()
{
}

/**
 * 函数：ClientIO::~ClientIO
 * 用途：析构函数，负责本搬运工生命周期结束、从服务器大账本中彻底抹除时的资产资产善后。
 * 参数来源：当大管家呼叫 _ios.erase(clientFd) 销毁这名客人的账本时，系统自动触发。
 * 实现逻辑：
 *     1. 清空本节点由于 std::string 扩容在堆内存中申请的所有缓冲占位符。
 *     2. 保持底层物理纯净：注意，此处绝不写 close(_fd)！物理 FD 的拔线与注销属于战略安全动作，由大管家 ServerManager::closeConnection 亲手统一执行，此处只负责本类内存资产的优雅卸载。
 * 后续影响：属于该客户端的网络车间在堆栈内存中被干净、无痕地彻底销毁，不发生任何内存泄漏。
 */
ClientIO::~ClientIO()
{
}

/**
 * 函数：ClientIO::readFromNet
 * 用途：纯物理层面的输入吞噬通道。包裹标准的 recv() 系统调用，只负责极其纯粹的体力活——从操作系统的网卡写缓冲区拉取裸字节流，并如实汇报。
 * 参数来源：来自大管家 ServerManager::handleClientRead() 里的吞噬 while 循环。
 * 变量解释：
 *     - temp_buf：由主循环在栈内存中临时铺设的大水盆（字符数组指针），用来盛放刚捞上来的冷水（裸数据）。
 *     - buf_size：由大管家硬性规定的临时水盆物理容量最大上限（通常为 4096 字节），用来卡死 recv() 的最大吞噬跨度。
 * 实现逻辑：
 *     1. 直接呼叫 Linux 内核系统调用 recv(this->_fd, temp_buf, buf_size, 0)。
 *     2. 不掺杂任何应用层协议分析（如不看是不是 HTTP 报文），能捞出来多少，就原封不动地返回实际捞到的字节计数。
 * 返回值解读：
 *     - 若返回正数：实际吸入的合法裸字节数，交由上层进行 += 字符串拼接。
 *     - 若返回 0：代表浏览器由于刷新或关闭网页，主动挥手发出了 EOF（断开连接信号）。
 *     - 若返回 -1：代表发生抖动（非阻塞满或被信号中断），需由大管家深入核验 errno 进行分流。
 */
// 🚀 在 ClientIO.cpp 中：
ssize_t ClientIO::readFromNet(char *buffer, size_t max_len)
{
    // 1. 发起接收请求
    ssize_t bytes_read = recv(this->_fd, buffer, max_len, 0);

    if (bytes_read == 0)
    {
        return 0; // 物理 EOF
    }

    if (bytes_read < 0)
    {
        // 🎯 秘密武器：无 errno 测谎仪（窥探读状态）
        char c;
        ssize_t r = recv(this->_fd, &c, 1, MSG_PEEK | MSG_DONTWAIT);

        if (r == 0)
        {
            return -2; // 🔴 对端彻底死机/强行拔网线（真实的物理死局）
        }

        // 🟢 其他情况，说明连接活着，仅仅是数据读空了（EAGAIN/EWOULDBLOCK 等）
        return -1;
    }

    return bytes_read; // 正常读到的字节数
}

/**
 * 函数：ClientIO::writeToNet
 * 用途：【硬核异步非阻塞切片断点续发核心】。负责向操作系统的网卡发送缓冲区倾倒写蓄水池里的数据，并能太极借力般解决非阻塞限流问题。
 * 参数来源：来自大管家 ServerManager::handleClientWrite()。当大循环被 poll() 告知该连接“可写（POLLOUT）”时触发调用。
 * 变量解释：
 *     - _write_buf：属于该客人的本地专属写蓄水池（发送缓冲区），里面装着等待被推向互联网的 HTTP 响应报文。
 *     - bytes_sent：单次由 send() 系统调用返回的、内核实际成功吞下并在内核队列落地的物理字节数。
 * 实现逻辑：
 *     1. 安全阻击：若发现本地 `_write_buf` 空空如也，说明没有物资可发，立刻无伤返回 0，绝不调用 send 浪费系统调用开销。
 *     2. 物理倾倒：呼叫 send(this->_fd, _write_buf.c_str(), _write_buf.size(), 0) 尝试全量倾倒。
 *     3. **【判别非阻塞网络抖动】**：若 bytes_sent 小于 0，核验 errno。若为 EAGAIN、EWOULDBLOCK 或 EINTR，代表此时内核缓冲区满或者是无辜被打断。它绝不死循环强攻！而是极其优雅地原地返回 0，通知大管家“本轮限流，我们先撤，维持写关注，下轮再发”。
 *     4. **【判别致命断线】**：若 bytes_sent 小于 0 且为其他错误，说明浏览器由于断电拔网线突然暴毙，直接返回 -1 向上级呼救。
 *     5. **【高能切片断点断尾（场景 B）】**：若 bytes_sent 成功发出大于 0 的字节：利用 `this->_write_buf = this->_write_buf.substr(bytes_sent)` 瞬间剁掉已经到岸的“头部字符串”，在写蓄水池里**仅仅保留没发完的“数据尾巴”**。
 * 后续影响：实现了真正的非阻塞增量续发。随着每一轮可写事件的触发，本地蓄水池的尾巴会不断被 substr 割短，直至归零（isWriteFinished == true），彻底完成大闭环。
 */
ssize_t ClientIO::writeToNet()
{
    if (this->_write_buf.empty())
        return 0;

    // 🚀 1. 传入 MSG_NOSIGNAL 防崩溃，安全发送
    ssize_t bytes_sent = send(this->_fd, this->_write_buf.c_str(), this->_write_buf.size(), MSG_NOSIGNAL);

    if (bytes_sent < 0)
    {
        // 🚀 2. 秘密武器：窥探（Peek）测试
        // 尝试非阻塞、不消耗缓冲区地读 1 个字节
        char c;
        ssize_t r = recv(this->_fd, &c, 1, MSG_PEEK | MSG_DONTWAIT);

        if (r == 0)
        {
            // 🎯 如果 recv 返回 0，说明对端已经彻底关闭了物理连接（EOF）！
            // 这时我们终于有十足的把握，安全、自信地返回 -1 销毁连接
            return -1;
        }

        // 🎯 否则，说明连接依然活着，只是发送缓冲区满了（EAGAIN），返回 0 等待下次可写
        return 0;
    }

    if (bytes_sent > 0)
    {
        this->_write_buf = this->_write_buf.substr(bytes_sent);
    }
    return bytes_sent;
}

/**
 * 函数：ClientIO::pushWriteBuffer
 * 用途：外部物资注入闸口。允许后续将刚刚拼装完毕的完全体 HTTP 报文字符串灌入搬运工的发射总库中。
 * 参数来源：来自 ServerManager 中处理完业务逻辑、确定要向客户端回包的交接点。
 * 变量解释：
 *     - response_str：队友生成的包含完整状态行、Header、Content-Length 以及 HTML 身体的裸字符串物资。
 * 实现逻辑：
 *     1. 物理流向追加：执行 `this->_write_buf += response_str`。将外部扔过来的新物资，极其安全地焊接到属于该客人的专属本地写蓄水池末尾。
 * 后续影响：本地写蓄水池的 size() 被扩充，`isWriteFinished()` 自动变为 false 状态，从而激活后续大管家的 POLLOUT 写关注轮询流。
 */
void ClientIO::pushWriteBuffer(const std::string &response_str)
{
    this->_write_buf += response_str;
}

/**
 * 函数：ClientIO::isWriteFinished
 * 用途：写传输大功告成状态哨兵。负责向外部刺探：当前该客户端的响应数据，是否已经全部在互联网对岸安全登陆？
 * 参数来源：来自handleClientWrite() 分流逻辑的终极判定处。
 * 实现逻辑：
 *     1. 查验私有 `_write_buf` 是否为空。若 empty() 为真，说明不管是长传还是断点续发，堆积的数据已经被蚕食切割完毕，全量送达。
 * 返回值解读：
 *     - 返回 true：所有字节成功交割。大管家可以安全重置读缓存，并调转枪头重新等读（POLLIN）。
 *     - 返回 false：池子里依然挂着被非阻塞截断的残存数据尾巴，大管家必须咬紧连接，维持 POLLOUT，不准改动关注事件。
 */
bool ClientIO::isWriteFinished() const
{
    return this->_write_buf.empty();
}

void ClientIO::clear()
{
    std::string().swap(this->_write_buf);
}

/**
 * 函数：ClientIO::getFd
 * 用途：私有资产物理指针暴露接口。允许大管家在需要核对 pollfd 账本或者进行底层标识符核验时，获取本搬运工守护的物理 FD 身份。
 * 参数来源：由外部大管家通过对象指针或引用直接点名呼叫。
 * 返回值解读：返回当前实例所绑定的、独一无二的客户端文件描述符（int）。
 */
int ClientIO::getFd() const
{
    return this->_fd;
}

const std::string &ClientIO::getWriteBuf() const
{
    return _write_buf;
}

// 🟢 通道 2：可写版本（当大管家确实需要动手修改、裁剪写缓冲区时调用）
std::string &ClientIO::getWriteBuf()
{
    return _write_buf;
}
