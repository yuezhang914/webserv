#ifndef CLIENT_HPP
#define CLIENT_HPP

#include <string>

class Client {
private:
    std::string _request_buffer; // 业务层用来存请求数据的缓冲区

public:
    Client() {}
    ~Client() {}

    // 1. 底层读到一截数据就会调用这个，把数据塞给你们
    void appendRawBuffer(const std::string& segment) {
        this->_request_buffer += segment;
    }

    // 2. 底层每次读完都会问你：收全了吗？
    bool isRequestComplete() const {
        // 你们在这里判断请求边界是否完整
        if (this->_request_buffer.find("\r\n\r\n") != std::string::npos)
            return true;
        return false;
    }

    // 3. 当底层可以发数据时，会找你们要完整的 HTTP 回包字符串
    std::string buildResponse() {
        // 你们在这里去读文件、拼装完整的响应报文
        std::string body = "<h1>Hello from High-Level HTTP!</h1>";
        std::string response = "HTTP/1.1 200 OK\r\n"
                               "Content-Type: text/html\r\n"
                               "Content-Length: 36\r\n"
                               "Connection: keep-alive\r\n\r\n" + body;
        return response;
    }
    
    // 4. 一轮 Keep-Alive 交互彻底完成后，底层通知你们把上一轮的缓存擦干净
    void clear() {
        this->_request_buffer.clear();
    }
};

#endif