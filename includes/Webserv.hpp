// includes/Webserv.hpp
#ifndef WEBSERV_HPP
#define WEBSERV_HPP

// 1. 全局状态契约的统一熔断
#define SUCCESS true
#define ERROR false
#define ERROR_PARSE_SIZE -1
#define DEFAULT_PORT "8080"

// 2. 网络底层必用的系统内核头文件（网络核心底座，总控统一引入）
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>

// 3. 业务平坦化组装
#include "Config.hpp"
#include "ServerConfig.hpp"
#include "LocationConfig.hpp"
#include "ConfigParser.hpp"
// ...
#endif