#ifndef SIGNAL_HPP
#define SIGNAL_HPP

#include <csignal>
#include "Webserv.hpp"

/*
全局变量：g_stop
作用：程序停止标志。
来源：Signal.cpp 定义；signalHandler 在收到 SIGINT 时把它设为 1。
用法：serverLoop 的 while (!g_stop) 会因为它变成 1 而退出。
*/
extern volatile sig_atomic_t g_stop;

/*
函数：signalHandler
作用：处理 Ctrl+C 等信号，让 serverLoop 可以优雅退出。
*/
void signalHandler(int sig);

#endif
