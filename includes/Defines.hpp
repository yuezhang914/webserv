#ifndef DEFINES_HPP
#define DEFINES_HPP

// 1. 全局状态契约的统一熔断
#define SUCCESS true
#define ERROR false
#define ERROR_PARSE_SIZE -1
#define DEFAULT_PORT "8080"
#define MAX_BODY_SIZE 1048576
#define SOMAXCONN_BACKLOG 128
#define BUFFER_SIZE 4096
#define ERROR_MAX_BODY_LENGTH -42
#define CGI_MAX_OUTPUT_SIZE  16 * 1024 * 1024

#endif