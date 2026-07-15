#ifndef CLIENTIO_HPP
#define CLIENTIO_HPP

#include <string>
#include <sys/types.h>

class ClientIO
{
private:
    int _fd;                // 客人的物理套接字标识符
    std::string _write_buf; // 专属写蓄水池：存放内核缓冲区满了以后，暂时被卡住的未发送“数据尾巴”

public:
    // 42 标配：默认构造与有参构造
    ClientIO();
    ClientIO(int fd);
    ~ClientIO();

    // 底层输入通道：从物理网卡里强吞裸字节
    ssize_t readFromNet(char *temp_buf, size_t buf_size);

    // 底层输出通道：向物理网卡倾倒裸字节（硬核非阻塞切片断点续发核心）
    ssize_t writeToNet();

    // 外部注资接口：允许外部把完全体 HTTP 响应报文灌进这个客人的待发队列中
    void pushWriteBuffer(const std::string &response_str);

    // 状态哨兵：检查该客人的积压数据是不是已经 100% 吐干净了
    bool isWriteFinished() const;

    void clear(); // 清空写蓄水池

    // 资产暴露：获取底层物理 FD
    int getFd() const;

    const std::string& getWriteBuf() const;
    std::string& getWriteBuf();


};

#endif