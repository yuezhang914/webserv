#ifndef CONNECTION_HPP
#define CONNECTION_HPP

#include "ClientSocket.hpp"
#include "Request.hpp"
#include "Response.hpp"

class Connection
{
public:
    // -------------------------------------------------------------
    // 1. 基础物理数据区 (Pod/Data Layer): 允许 Handler 直接追加与擦除 Buffer
    // -------------------------------------------------------------
    ClientSocket *socket;
    ServerConfig config;
    std::string read_buffer;
    std::string write_buffer;

    Request request;
    Response response;
    bool close_after_write;

    // 413 提前响应后的输入排空状态：响应已发送，但在客户端停止写入并关闭前不主动 reset socket。
    bool drain_input_before_close;

    // chunked 大请求的增量扫描状态：只记录下一段边界，不保存额外 body 副本。
    bool chunk_scan_active;
    size_t chunk_scan_pos;
    size_t chunk_decoded_size;
    unsigned long chunk_body_limit;

    // 大型 chunked CGI 的内存准入状态：acquired 表示占用处理槽，waiting 表示已暂停在等待队列。
    bool large_cgi_slot_acquired;
    bool large_cgi_waiting;

    Connection();
    Connection(int clientFd, const ServerConfig &srv_cfg);
    ~Connection();



    void clear();
    void clearRequest();
    void closeFd(); // 👈 添加此公有成员函数声明

private:

    Connection(const Connection &other);
    Connection &operator=(const Connection &other);
};

#endif
