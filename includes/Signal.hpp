#ifndef SIGNAL_HPP
#define SIGNAL_HPP

#include <csignal>

/*
函数：signalHandler
作用：处理 Ctrl+C 等信号，让 serverLoop 可以优雅退出。
*/
void setupSignalHandlers();

#endif
