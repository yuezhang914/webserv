// includes/Webserv.hpp
#ifndef WEBSERV_HPP
#define WEBSERV_HPP



// 2. 网络底层必用的系统内核头文件（网络核心底座，总控统一引入）
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <vector>
#include <set>
#include <map>
#include <string>
#include <poll.h>
#include <iostream>
#include <cstring>
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <sys/types.h>
#include <unistd.h>   // 物理引入 fork, pipe, dup2, close 等系统调用
#include <sys/wait.h> // 物理引入 waitpid


// 3. 业务平坦化组装
#include "Config.hpp"
#include "ServerConfig.hpp"
#include "LocationConfig.hpp"
#include "ConfigParser.hpp"
#include "ServerManager.hpp"

#include "Signal.hpp"

#include "Connection.hpp"
#include "ServerSocket.hpp"
#include "ClientSocket.hpp"

#include "CgiHandler.hpp"
#include "CgiEnv.hpp"
#include "CgiProcess.hpp"
#include "CgiResponse.hpp"

#include "Response.hpp"
// ...
#endif