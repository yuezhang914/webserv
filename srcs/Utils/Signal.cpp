/*
文件：srcs/Utils/Signal.cpp
信号处理实现。用于 Ctrl+C 时通知 serverLoop 退出。
*/
#include "Signal.hpp"

volatile sig_atomic_t g_stop = 0;

/*
函数：signalHandler
用途：处理 SIGINT 等信号。
参数来源：操作系统在用户按 Ctrl+C 时调用该函数，并传入信号编号。
实现逻辑：
    1. (void)sig 表示当前不需要具体信号编号，避免未使用参数警告。
    2. 把全局 g_stop 设置为 1。
    3. serverLoop 的 while (!g_stop) 下一轮检查时会退出循环。
为什么用 sig_atomic_t：它适合在信号处理函数中安全地读写简单标志位。
*/
void signalHandler(int sig) {
    if (sig == SIGINT) {
        std::cout << "\n[Signal] Caught CTRL+C (SIGINT)" << std::endl;
        g_stop = 1;
    }
}
