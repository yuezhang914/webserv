/*
文件：srcs/Signals/Signal.cpp
信号处理实现。用于 Ctrl+C、SIGTERM 或 Ctrl+\\ 时通知 serverLoop 退出，
并忽略 SIGPIPE，防止向已经断开的 socket 写入时导致程序退出。
*/
#include "Webserv.hpp"

volatile sig_atomic_t g_loop_running;

/*
函数：handleSignal
用途：处理需要结束服务器主循环的信号，只修改全局运行标志，不做其他复杂操作。
*/
static void handleSignal(int sig)
{
    // 捕获 SIGINT (Ctrl+C)、SIGTERM (kill pid) 以及 SIGQUIT (Ctrl+\)
    if (sig == SIGINT || sig == SIGTERM || sig == SIGQUIT)
    {
        // 异步信号安全操作：仅修改全局标志位，通知主循环退出
        g_loop_running = 0;
    }
}

/*
函数：setupSignalHandlers
用途：使用 subject 允许的 signal() 注册退出信号，并忽略 SIGPIPE。
*/
void setupSignalHandlers()
{
    // 捕获 Ctrl+C、kill 命令以及 Ctrl+\，统一交给 handleSignal 处理
    signal(SIGINT, handleSignal);
    signal(SIGTERM, handleSignal);
    signal(SIGQUIT, handleSignal);

    // 忽略 SIGPIPE，防止向已经断开的 socket 写入时导致服务器异常退出
    signal(SIGPIPE, SIG_IGN);
}
