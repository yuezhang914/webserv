#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <poll.h>

#define PORT 8080

int setup_server(int port)
{
    int listen_fd;
    struct sockaddr_in addr;
    int opt = 1;

    // 1. 创建 Socket (IPv4, TCP)
    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0)
        return -1;

    // 2. 允许端口复用（避免重启报错）
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // 3. 设置为非阻塞模式 (Webserv 核心要求)
    fcntl(listen_fd, F_SETFL, O_NONBLOCK);

    // 4. 绑定地址
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY; // 监听所有网卡
    addr.sin_port = htons(port);       // 转换字节序

    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        close(listen_fd);
        return -1;
    }

    // 5. 开始监听
    if (listen(listen_fd, 10) < 0)
    {
        close(listen_fd);
        return -1;
    }

    return listen_fd;
}

int main()
{
    int listen_fd = setup_server(PORT);
    if (listen_fd < 0)
    {
        std::cerr << "Error setting up server" << std::endl;
        return 1;
    }

    std::cout << "Server started on port " << PORT << "..." << std::endl;

    std::vector<struct pollfd> fds;
    struct pollfd main_fd;
    main_fd.fd = listen_fd;
    main_fd.events = POLLIN; // 只关心是否有新连接进来
    fds.push_back(main_fd);

    while (true)
    {
        int ret = poll(fds.data(), fds.size(), -1); // 永久阻塞直到有事发生
        if (ret < 0)
            break;

        for (size_t i = 0; i < fds.size(); i++)
        {
            if (fds[i].revents & POLLIN)
            {
                if (fds[i].fd == listen_fd)
                {
                    // 情况 A: 监听端口有新客人
                    int client_fd = accept(listen_fd, NULL, NULL);
                    if (client_fd >= 0)
                    {
                        fcntl(client_fd, F_SETFL, O_NONBLOCK);
                        struct pollfd new_client;
                        new_client.fd = client_fd;
                        new_client.events = POLLIN;
                        fds.push_back(new_client);
                        std::cout << "New client connected! FD: " << client_fd << std::endl;
                    }
                }
                else
                {
                    // 情况 B: 已经连上的旧客人发来了请求
                    char input_buffer[1024] = {0};
                    int bytes_read = recv(fds[i].fd, input_buffer, 1024, 0);
                    //如果一次性读取完整请求的话， 就转换pollin 为 pollout
                    fds[i].events = POLLOUT; // 读取完毕后，准备发送响应
                    if (bytes_read <= 0)
                    {
                        // 客户端断开连接
                        std::cout << "Client disconnected FD: " << fds[i].fd << std::endl;
                        close(fds[i].fd);
                        fds.erase(fds.begin() + i);
                        i--;
                    }

                }
            }
            if (fds[i].revents & POLLOUT)
            {
                // 收到请求，直接回复一个简单的 HTTP 200
                std::string response = "HTTP/1.1 200 OK\r\nContent-Length: 14\r\n\r\nHello Webserv!";
                send(fds[i].fd, response.c_str(), response.size(), 0);
            }
            if(fds[i].revents & (POLLERR | POLLHUP | POLLNVAL))
            {
                // 出现错误或客户端断开连接
                std::cout << "Client error or disconnect FD: " << fds[i].fd << std::endl;
                close(fds[i].fd);
                fds.erase(fds.begin() + i);
                i--;
            }
        }
    }
    close(listen_fd);
    return 0;
}