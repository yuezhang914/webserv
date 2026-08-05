#ifndef DEFINES_HPP
#define DEFINES_HPP

// 1. 全局状态契约的统一熔断
#define SUCCESS true
#define ERROR false
#define ERROR_PARSE_SIZE -1
#define DEFAULT_PORT "8080"
#define MAX_BODY_SIZE 120 * 1024 * 1024
#define SOMAXCONN_BACKLOG 128
#define BUFFER_SIZE 4096
#define ERROR_MAX_BODY_LENGTH -42
#define CGI_MAX_OUTPUT_SIZE  120 * 1024 * 1024
// CGI 只在连续 10 秒没有任何 stdin/stdout 进展时超时。
// 不能使用 CGI 从启动到结束的总时长，否则学校 tester 的并发 100MB CGI 会被误判为 504。
#define CGI_INACTIVITY_TIMEOUT 10

// 当前 Response/CGI 链路仍会在内存中保存完整请求体和 CGI 输出。
// 对超过 1MB 的 chunked CGI 做准入控制，只允许 2 个同时进入完整缓冲阶段；
// 其余客户端暂时停止 POLLIN，由 TCP 接收窗口提供背压，防止 20×100MB 压测触发 OOM。
#define LARGE_CGI_BUFFER_THRESHOLD (1024UL * 1024UL)
#define MAX_BUFFERED_LARGE_CGI_TASKS 2
#define CLIENT_READ_BUDGET (64UL * 1024UL)

#endif
