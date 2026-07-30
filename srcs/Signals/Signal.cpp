/*
文件：srcs/Utils/Signal.cpp
信号处理实现。用于 Ctrl+C 时通知 serverLoop 退出。
*/
#include "Webserv.hpp"

volatile sig_atomic_t g_loop_running;

static void handleSignal(int sig)
{
    // 捕获 SIGINT (Ctrl+C)、SIGTERM (kill pid) 以及 SIGQUIT (Ctrl+\)
    if (sig == SIGINT || sig == SIGTERM || sig == SIGQUIT)
    {
        // 异步信号安全操作：仅修改全局标志位，通知主循环退出
        g_loop_running = 0;
    }
}

void setupSignalHandlers()
{
    struct sigaction sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handleSignal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    /*
        捕获 Ctrl+C、kill 命令以及 Ctrl+\
    */ 
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGQUIT, &sa, NULL); 

    // 2. 忽略 SIGPIPE（防止向断开的 socket 写入时崩溃）
    struct sigaction sa_pipe;
    std::memset(&sa_pipe, 0, sizeof(sa_pipe));
    sa_pipe.sa_handler = SIG_IGN;
    sigemptyset(&sa_pipe.sa_mask);
    sa_pipe.sa_flags = 0;
    sigaction(SIGPIPE, &sa_pipe, NULL);
}
