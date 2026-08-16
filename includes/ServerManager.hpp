#ifndef SERVER_MANAGER_HPP
#define SERVER_MANAGER_HPP
#include "ServerConfig.hpp"
#include "Connection.hpp"
#include "RequestParser.hpp"
#include "ServerSocket.hpp"
#include "CgiManager.hpp"
class ServerManager
{
private:
    std::vector<ServerConfig> _server_configs;
    std::vector<struct pollfd> _poll_fds;
    std::vector<ServerSocket *> _listen_sockets;
    std::map<int, ServerConfig> _listen_socket_map;
    std::map<int, Connection *> _connections;
    CgiManager _cgiManager;
    std::map<int, int> _cgi_read_fd_to_client_map;
    std::map<int, int> _cgi_write_fd_to_client_map;
    std::vector<struct pollfd> _fds_to_add;
    size_t _active_buffered_large_cgi;
    std::deque<int> _waiting_buffered_large_cgi_clients;
    // Reserves a safe slot for one large CGI request.
    bool ensureLargeCgiSlot(Connection *conn, int clientFd);
    // Releases the large CGI slot used by one client.
    void releaseLargeCgiSlot(int clientFd);
    // Restarts large CGI clients that were waiting for a slot.
    void resumeWaitingLargeCgiClients();
    // Checks whether the file descriptor is a CGI read pipe.
    bool isCgiReadFd(int fd) const;
    // Checks whether the file descriptor is a CGI write pipe.
    bool isCgiWriteFd(int fd) const;
    // Creates and prepares the server listening sockets.
    void setupSockets();
    // Checks whether the file descriptor is a listening socket.
    bool isListenFd(int fd);
    // Accepts a new client connection.
    void acceptNewConnection(int listenFd);
    // Reads socket data to buffer.
    bool readSocketDataToBuffer(Connection *conn, int clientFd, size_t pollIndex);
    // Starts the CGI task for one parsed request.
    void dispatchCgiTask(Connection *conn, int clientFd, const Response &res);
    // Processes parsed request.
    void processParsedRequest(Connection *conn, int clientFd);
    // Handles client read.
    void handleClientRead(int clientFd, size_t pollIndex);
    // Handles client write.
    void handleClientWrite(int clientFd, size_t pollIndex);
    // Closes connection.
    void closeConnection(int clientFd, size_t pollIndex);
    // Removes closed file descriptors before the next poll call.
    void prePollCleanup();
    // Runs poll.
    int executePoll(int &retries);
    // Handles all file descriptor events returned by poll.
    void dispatchEvents();
    // Handles cgi read.
    void handleCgiRead(int cgiReadFd);
    // Handles cgi write.
    void handleCgiWrite(int cgiWriteFd);
    // Removes the CGI write pipe for one client.
    void cleanupClientWritePipe(int clientFd);
    // Stops the server and cleans its open resources.
    void stop();
public:
    // Sets client events.
    void setClientEvents(int clientFd, short events);
    // Adds one file descriptor to the poll list.
    void registerFdToPoll(int fd, short events);
    // Removes fd from poll.
    void eraseFdFromPoll(int targetFd);
    // Creates a new ServerManager object.
    ServerManager(const std::vector<ServerConfig> &configs);
    // Cleans up this object and its owned resources.
    ~ServerManager();
    // Creates the listening sockets and prepares the server manager.
    void init();
    // Runs the main server event loop.
    void run();
private:
    // Creates a new ServerManager object.
    ServerManager(const ServerManager &src);
    // Copies data from another object.
    ServerManager &operator=(const ServerManager &rhs);
};
#endif
