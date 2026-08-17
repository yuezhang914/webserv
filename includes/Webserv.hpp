#ifndef WEBSERV_HPP
#define WEBSERV_HPP

#define DEBUG_MODE 1
#ifdef DEBUG_MODE
    #define DEBUG_LOG(msg) std::cout << "[DEBUG] " << msg << std::endl
#else
    #define DEBUG_LOG(msg)
#endif
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
#include <unistd.h>
#include <sys/wait.h>
#include <ctime>
#include <csignal>
#include <deque>
#include <netdb.h>
#include <utility>
#include "Config.hpp"
#include "ServerConfig.hpp"
#include "LocationConfig.hpp"
#include "ServerManager.hpp"
#include "Signal.hpp"
#include "Connection.hpp"
#include "ServerSocket.hpp"
#include "ClientSocket.hpp"
#include "CgiHandler.hpp"
#include "CgiManager.hpp"
#include "SessionCookie.hpp"
#include "Request.hpp"
#include "RequestParser.hpp"
#include "Response.hpp"
#include "Signal.hpp"
#include <limits.h>
#include <sstream>
extern volatile sig_atomic_t g_loop_running;
#endif
