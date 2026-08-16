#include "Webserv.hpp"
#include "SessionStore.hpp"
#include "Signal.hpp"
#include "ConfigRouteUtils.hpp"

// Creates a new ServerManager object.
ServerManager::ServerManager(const std::vector<ServerConfig> &configs) : _server_configs(configs), _active_buffered_large_cgi(0)
{
    std::cout << "[ServerManager] WebServ engine pre-loaded with " << _server_configs.size() << " virtual servers." << std::endl;
}

// Cleans up this object and its owned resources.
ServerManager::~ServerManager()
{
    for (size_t i = 0; i < this->_listen_sockets.size(); ++i)
    {
        if (this->_listen_sockets[i] != NULL)
        {
            delete this->_listen_sockets[i];
        }
    }
    this->_listen_sockets.clear();
    for (std::map<int, Connection *>::iterator it = this->_connections.begin(); it != this->_connections.end(); ++it)
    {
        if (it->second != NULL)
        {
            delete it->second;
        }
    }
    this->_connections.clear();
    std::cout << "[ServerManager] Engine completely shutdown and memory released." << std::endl;
}

// Creates the listening sockets and prepares the server manager.
void ServerManager::init()
{
    std::cout << "[ServerManager] Initializing network sockets..." << std::endl;
    this->setupSockets();
    ::signal(SIGPIPE, SIG_IGN);
}

// Creates and prepares the server listening sockets.
void ServerManager::setupSockets()
{
    std::vector<std::pair<std::string, int> > handled_endpoints;
    for (size_t i = 0; i < _server_configs.size(); ++i)
    {
        int port = _server_configs[i].port;
        std::string host = _server_configs[i].host;
        bool is_duplicate = false;
        for (size_t p = 0; p < handled_endpoints.size(); ++p)
        {
            if (handled_endpoints[p].first == host && handled_endpoints[p].second == port)
            {
                is_duplicate = true;
                break;
            }
        }
        if (is_duplicate)
        {
            std::cout << "[ServerManager] Multi-server configuration detected for " << host << ":" << port << " (Skipping duplicate bind)" << std::endl;
            continue;
        }
        ServerSocket *srv_sock = new ServerSocket(host, port);
        srv_sock->setup();
        int listenFd = srv_sock->getFd();
        std::cout << "[ServerManager] Successfully listening on " << host << ":" << port << " (FD: " << listenFd << ")" << std::endl;
        this->_listen_sockets.push_back(srv_sock);
        handled_endpoints.push_back(std::make_pair(host, port));
        _listen_socket_map[listenFd] = _server_configs[i];
        struct pollfd pfd;
        pfd.fd = listenFd;
        pfd.events = POLLIN;
        pfd.revents = 0;
        _poll_fds.push_back(pfd);
    }
}

// Accepts a new client connection.
void ServerManager::acceptNewConnection(int listenFd)
{
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int clientFd = ::accept(listenFd, (struct sockaddr *)&client_addr, &client_len);
    if (clientFd < 0)
        return;
    ClientSocket *p_socket = NULL;
    Connection *conn = NULL;
    try
    {
        p_socket = new ClientSocket(clientFd);
        conn = new Connection();
        conn->socket = p_socket;
        std::map<int, ServerConfig>::iterator config_it = this->_listen_socket_map.find(listenFd);
        if (config_it != this->_listen_socket_map.end())
            conn->config = config_it->second;
        this->_connections[clientFd] = conn;
        this->registerFdToPoll(clientFd, POLLIN);
        std::cout << "[ServerManager] Accepted new connection -> Allocated Client FD: " << clientFd << " (SUCCESSFULLY SET O_NONBLOCK!)" << std::endl;
    }
    catch (const std::exception &e)
    {
        std::cerr << "[Acceptor] Critical allocation error: " << e.what() << std::endl;
        this->_connections.erase(clientFd);
        if (conn != NULL)
            delete conn;
        else if (p_socket != NULL)
            delete p_socket;
        else::close(clientFd);
    }
}

// Reads socket data to buffer.
bool ServerManager::readSocketDataToBuffer(Connection *conn, int clientFd, size_t pollIndex)
{
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read = conn->socket->read(buffer, BUFFER_SIZE - 1);
    if (bytes_read > 0)
    {
        conn->read_buffer.append(buffer, static_cast<size_t>(bytes_read));
        return true;
    }
    if (bytes_read == 0)
        DEBUG_LOG("[ServerManager] Client FD " << clientFd << " closed connection (EOF).");
    else DEBUG_LOG("[ServerManager] recv failed on ready Client FD " << clientFd << ". Closing connection.");
    this->closeConnection(clientFd, pollIndex);
    return false;
}

// Starts the CGI task for one parsed request.
void ServerManager::dispatchCgiTask(Connection *conn, int clientFd, const Response &res)
{
    std::string script_path;
    std::string interpreter_path;
    std::string root;
    int outReadFd = -1;
    int outWriteFd = -1;
    res.getHeader("X-Internal-CGI-Path", script_path);
    res.getHeader("X-Internal-CGI-Interpreter", interpreter_path);
    std::map<std::string, std::string> cgiHeaders = conn->request.getHeaders();
    std::string scriptNameVal, pathInfoVal;
    if (res.getHeader("X-Internal-CGI-Script-Name", scriptNameVal))
        cgiHeaders["X-Internal-CGI-Script-Name"] = scriptNameVal;
    if (res.getHeader("X-Internal-CGI-Path-Info", pathInfoVal))
        cgiHeaders["X-Internal-CGI-Path-Info"] = pathInfoVal;
    if (res.getHeader("X-Internal-CGI-Document-Root", root))
        cgiHeaders["X-Internal-CGI-Document-Root"] = root;
    else
    {
        DEBUG_LOG("[CGI] Error: Missing document root for client " << clientFd);
        conn->response.createResponse(500, "CGI Missing Document Root", conn->config.error_pages);
        conn->write_buffer = conn->response.responseToString();
        conn->close_after_write = true;
        this->setClientEvents(clientFd, POLLOUT);
        return;
    }
    std::string host = "localhost";
    std::string port = "8080";
    std::map<std::string, std::string>::const_iterator it = conn->request.getHeaders().find("Host");
    if (it != conn->request.getHeaders().end())
    {
        std::string hostHeader = it->second;
        size_t pos = hostHeader.find(':');
        if (pos != std::string::npos)
        {
            host = hostHeader.substr(0, pos);
            port = hostHeader.substr(pos + 1);
        }
        else host = hostHeader;
    }
    bool launched = this->_cgiManager.launchTask(clientFd, script_path, interpreter_path, conn->request.getMethod(), conn->request.getQuery(), conn->request.getPath(), cgiHeaders, conn->request.getBody(), host, port, root, outReadFd, outWriteFd);
    if (!launched)
    {
        DEBUG_LOG("[CGI] Error: Failed to spawn CGI process for client " << clientFd);
        conn->response.createResponse(500, "CGI Spawn Failed", conn->config.error_pages);
        conn->write_buffer = conn->response.responseToString();
        conn->close_after_write = true;
        this->setClientEvents(clientFd, POLLOUT);
        return;
    }
    if (outReadFd != -1)
    {
        this->_cgi_read_fd_to_client_map[outReadFd] = clientFd;
        this->registerFdToPoll(outReadFd, POLLIN);
    }
    if (outWriteFd != -1)
    {
        this->_cgi_write_fd_to_client_map[outWriteFd] = clientFd;
        this->registerFdToPoll(outWriteFd, POLLOUT);
    }
    this->setClientEvents(clientFd, 0);
    DEBUG_LOG("[⚡ WebServ Core] Client " << clientFd << " successfully split into CGI pipeline, client read paused.");
}

// Processes parsed request.
void ServerManager::processParsedRequest(Connection *conn, int clientFd)
{
    static SessionStore sessionStore;
    std::string contentLength;
    std::string script_path;
    DEBUG_LOG("\n================ [DEBUG REQUEST] ================");
    DEBUG_LOG("[REQ] Method: " << conn->request.getMethod() << " | URI: " << conn->request.getPath() << " | Body Size in Request: " << conn->request.getBody().size() << " bytes");
    Response res = buildResponse(conn->request, sessionStore);
    res.getHeader("Content-Length", contentLength);
    DEBUG_LOG("[RES] Status: " << res.getStatusCode() << " | Header Content-Length: " << contentLength);
    if (res.getHeader("X-Internal-CGI-Path", script_path))
    {
        DEBUG_LOG("[DEBUG CGI Task] Dispatching CGI -> scriptPath = " << script_path);
        DEBUG_LOG("=================================================\n");
        this->dispatchCgiTask(conn, clientFd, res);
    }
    else
    {
        DEBUG_LOG("[DEBUG Normal Task] No CGI header, sending direct response.");
        DEBUG_LOG("=================================================\n");
        conn->write_buffer = res.responseToString();
        std::string connHeader;
        if (res.getHeader("Connection", connHeader))
        {
            std::string temp = connHeader;
            for (size_t i = 0; i < temp.length(); ++i)
                temp[i] = std::tolower(temp[i]);
            if (temp == "close")
                conn->close_after_write = true;
        }
        this->setClientEvents(clientFd, POLLOUT);
    }
}

// Checks whether the request body uses chunked transfer encoding.
static bool requestUsesChunkedBody(const Request &request)
{
    std::string value;
    if (!request.getHeader("transfer-encoding", value))
        return false;
    size_t start = 0;
    while (start < value.size() && (value[start] == ' ' || value[start] == '\t'))
        ++start;
    size_t end = value.size();
    while (end > start && (value[end - 1] == ' ' || value[end - 1] == '\t'))
        --end;
    std::string normalized = value.substr(start, end - start);
    size_t i = 0;
    while (i < normalized.size())
    {
        if (normalized[i] >= 'A' && normalized[i] <= 'Z')
            normalized[i] = static_cast<char>(normalized[i] - 'A' + 'a');
        ++i;
    }
    return normalized == "chunked";
}

// Scans more chunked data stored in one connection.
static int advanceConnectionChunkScan(Connection *conn, size_t &consumed)
{
    if (!conn->chunk_scan_active)
    {
        size_t headerEnd = conn->read_buffer.find("\r\n\r\n");
        if (headerEnd == std::string::npos)
            return REQUEST_INCOMPLETE;
        conn->chunk_scan_active = true;
        conn->chunk_scan_pos = headerEnd + 4;
        conn->chunk_decoded_size = 0;
        conn->chunk_body_limit = getEffectiveBodyLimit(&conn->config, conn->request.getPath());
    }
    return RequestParser::advanceChunkedScan(conn->read_buffer, conn->chunk_scan_pos, conn->chunk_body_limit, conn->chunk_decoded_size, consumed);
}

// Checks whether the request path is configured as CGI.
static bool requestTargetsConfiguredCgi(const Connection *conn)
{
    if (conn == NULL || conn->request.getMethod() != "POST")
        return false;
    const LocationConfig *location = findMatchingLocation(conn->request.getPath(), conn->config.locations);
    if (location == NULL)
        return false;
    std::map<std::string, std::string>::const_iterator it = location->cgi_extensions.begin();
    while (it != location->cgi_extensions.end())
    {
        const std::string &extension = it->first;
        const std::string &path = conn->request.getPath();
        if (!extension.empty() && path.size() >= extension.size() && path.compare(path.size() - extension.size(), extension.size(), extension) == 0)
            return true;
        ++it;
    }
    return false;
}

// Reserves a safe slot for one large CGI request.
bool ServerManager::ensureLargeCgiSlot(Connection *conn, int clientFd)
{
    if (conn == NULL)
        return false;
    if (conn->large_cgi_slot_acquired)
        return true;
    if (conn->large_cgi_waiting)
        return false;
    if (this->_active_buffered_large_cgi < MAX_BUFFERED_LARGE_CGI_TASKS)
    {
        conn->large_cgi_slot_acquired = true;
        ++this->_active_buffered_large_cgi;
        return true;
    }
    conn->large_cgi_waiting = true;
    this->_waiting_buffered_large_cgi_clients.push_back(clientFd);
    this->setClientEvents(clientFd, 0);
    DEBUG_LOG("[ServerManager] Large CGI client " << clientFd << " paused by memory admission control.");
    return false;
}

// Restarts large CGI clients that were waiting for a slot.
void ServerManager::resumeWaitingLargeCgiClients()
{
    while (this->_active_buffered_large_cgi < MAX_BUFFERED_LARGE_CGI_TASKS && !this->_waiting_buffered_large_cgi_clients.empty())
    {
        int clientFd = this->_waiting_buffered_large_cgi_clients.front();
        this->_waiting_buffered_large_cgi_clients.pop_front();
        std::map<int, Connection *>::iterator it = this->_connections.find(clientFd);
        if (it == this->_connections.end() || it->second == NULL || !it->second->large_cgi_waiting)
            continue;
        Connection *conn = it->second;
        conn->large_cgi_waiting = false;
        conn->large_cgi_slot_acquired = true;
        ++this->_active_buffered_large_cgi;
        this->setClientEvents(clientFd, POLLIN);
        DEBUG_LOG("[ServerManager] Large CGI client " << clientFd << " resumed.");
    }
}

// Releases the large CGI slot used by one client.
void ServerManager::releaseLargeCgiSlot(int clientFd)
{
    std::map<int, Connection *>::iterator it = this->_connections.find(clientFd);
    if (it == this->_connections.end() || it->second == NULL)
        return;
    Connection *conn = it->second;
    bool released = conn->large_cgi_slot_acquired;
    if (released)
    {
        conn->large_cgi_slot_acquired = false;
        if (this->_active_buffered_large_cgi > 0)
            --this->_active_buffered_large_cgi;
    }
    conn->large_cgi_waiting = false;
    if (released)
        this->resumeWaitingLargeCgiClients();
}

// Handles client read.
void ServerManager::handleClientRead(int clientFd, size_t pollIndex)
{
    std::map<int, Connection *>::iterator connIt = this->_connections.find(clientFd);
    if (connIt == this->_connections.end() || connIt->second == NULL)
    {
        std::cerr << "[ServerManager] Error: Client FD " << clientFd << " is NULL or unmapped in handleClientRead!" << std::endl;
        this->closeConnection(clientFd, pollIndex);
        return;
    }
    Connection *conn = connIt->second;
    if (conn->drain_input_before_close)
    {
        char discard[BUFFER_SIZE];
        ssize_t bytesRead = conn->socket->read(discard, sizeof(discard));
        if (bytesRead > 0)
            return;
        this->closeConnection(clientFd, pollIndex);
        return;
    }
    if (!this->readSocketDataToBuffer(conn, clientFd, pollIndex))
        return;
    size_t consumed = 0;
    int status = REQUEST_INCOMPLETE;
    if (conn->chunk_scan_active)
    {
        if (requestTargetsConfiguredCgi(conn) && conn->read_buffer.size() >= LARGE_CGI_BUFFER_THRESHOLD && !this->ensureLargeCgiSlot(conn, clientFd))
            return;
        status = advanceConnectionChunkScan(conn, consumed);
        if (status == REQUEST_OK)
        {
            conn->chunk_scan_active = false;
            status = RequestParser::parseBuffer(conn->read_buffer, conn->request, &conn->config, consumed);
        }
    }
    else
    {
        status = RequestParser::parseBuffer(conn->read_buffer, conn->request, &conn->config, consumed);
        if (status == REQUEST_INCOMPLETE && requestUsesChunkedBody(conn->request))
        {
            if (requestTargetsConfiguredCgi(conn) && conn->read_buffer.size() >= LARGE_CGI_BUFFER_THRESHOLD && !this->ensureLargeCgiSlot(conn, clientFd))
                return;
            status = advanceConnectionChunkScan(conn, consumed);
            if (status == REQUEST_OK)
            {
                conn->chunk_scan_active = false;
                status = RequestParser::parseBuffer(conn->read_buffer, conn->request, &conn->config, consumed);
            }
        }
    }
    if (status == REQUEST_OK)
    {
        DEBUG_LOG("[ServerManager] Request parsed successfully for FD " << clientFd);
        conn->chunk_scan_active = false;
        conn->chunk_scan_pos = 0;
        conn->chunk_decoded_size = 0;
        conn->chunk_body_limit = 0;
        conn->read_buffer.erase(0, consumed);
        if (conn->read_buffer.empty())
            std::string().swap(conn->read_buffer);
        this->processParsedRequest(conn, clientFd);
    }
    else if (status == REQUEST_INCOMPLETE)
        return;
    else if (status == REQUEST_BODY_TOO_LARGE)
    {
        std::cerr << "[ServerManager] Request body too large on FD " << clientFd << ". Sending 413, then draining remaining input before close." << std::endl;
        conn->close_after_write = false;
        conn->drain_input_before_close = true;
        conn->chunk_scan_active = false;
        conn->chunk_scan_pos = 0;
        conn->chunk_decoded_size = 0;
        conn->chunk_body_limit = 0;
        std::string().swap(conn->read_buffer);
        conn->write_buffer = "HTTP/1.1 413 Payload Too Large\r\n" "Content-Length: 0\r\nConnection: close\r\n\r\n";
        this->setClientEvents(clientFd, POLLOUT);
    }
    else
    {
        std::cerr << "[ServerManager] Request error (" << status << ") on FD " << clientFd << ". Pre-writing 400 response." << std::endl;
        conn->close_after_write = true;
        conn->write_buffer = "HTTP/1.1 400 Bad Request\r\n" "Content-Length: 0\r\nConnection: close\r\n\r\n";
        this->setClientEvents(clientFd, POLLOUT);
    }
}

// Handles client write.
void ServerManager::handleClientWrite(int clientFd, size_t pollIndex)
{
    std::map<int, Connection *>::iterator it = this->_connections.find(clientFd);
    if (it == this->_connections.end() || it->second == NULL)
        return;
    Connection *conn = it->second;
    if (conn->write_buffer.empty())
    {
        if (conn->close_after_write)
            this->closeConnection(clientFd, pollIndex);
        else if (conn->drain_input_before_close)
            this->setClientEvents(clientFd, POLLIN);
        else
        {
            this->releaseLargeCgiSlot(clientFd);
            conn->clear();
            this->setClientEvents(clientFd, POLLIN);
        }
        return;
    }
    ssize_t bytes_sent = conn->socket->write(conn->write_buffer);
    if (bytes_sent > 0)
    {
        conn->write_buffer.erase(0, bytes_sent);
        if (conn->write_buffer.empty())
        {
            if (conn->close_after_write)
            {
                DEBUG_LOG("[ServerManager] Sent response completely to FD " << clientFd << ". Closing connection per policy.");
                this->closeConnection(clientFd, pollIndex);
            }
            else if (conn->drain_input_before_close)
            {
                DEBUG_LOG("[ServerManager] Sent 413 completely to FD " << clientFd << ". Draining remaining request input before close.");
                this->setClientEvents(clientFd, POLLIN);
            }
            else
            {
                DEBUG_LOG("[ServerManager] Sent response completely to FD " << clientFd << ". Resetting event to POLLIN.");
                this->releaseLargeCgiSlot(clientFd);
                conn->clear();
                this->setClientEvents(clientFd, POLLIN);
            }
        }
    }
    else
    {
        if (bytes_sent == 0)
        {
            DEBUG_LOG("[ServerManager] send made no progress on ready Client FD " << clientFd << ". Closing connection.");
        }
        else
        {
            DEBUG_LOG("[ServerManager] send failed on ready Client FD " << clientFd << ". Closing connection.");
        }
        this->closeConnection(clientFd, pollIndex);
    }
}

// Handles cgi read.
void ServerManager::handleCgiRead(int cgiReadFd)
{
    CgiEventResult res = this->_cgiManager.handlePipeRead(cgiReadFd);
    if (res.status == CGI_FINISHED || res.status == CGI_ERROR)
    {
        this->_cgi_read_fd_to_client_map.erase(cgiReadFd);
        this->eraseFdFromPoll(cgiReadFd);
        this->cleanupClientWritePipe(res.clientFd);
        std::map<int, Connection *>::iterator it = this->_connections.find(res.clientFd);
        if (it != this->_connections.end() && it->second != NULL)
        {
            Connection *conn = it->second;
            if (res.status == CGI_FINISHED)
            {
                Response cgiResponse = buildCgiResponse(conn->request, res.rawOutput);
                conn->response = cgiResponse;
            }
            else conn->response.createResponse(res.statusCode, "CGI Output Error", conn->config.error_pages);
            conn->write_buffer = conn->response.responseToString();
            this->setClientEvents(res.clientFd, POLLOUT);
        }
    }
}

// Handles cgi write.
void ServerManager::handleCgiWrite(int cgiWriteFd)
{
    CgiEventResult res = this->_cgiManager.handlePipeWrite(cgiWriteFd);
    if (res.status == CGI_ERROR)
    {
        this->_cgi_write_fd_to_client_map.erase(cgiWriteFd);
        this->eraseFdFromPoll(cgiWriteFd);
        std::map<int, int>::iterator readIt = this->_cgi_read_fd_to_client_map.begin();
        while (readIt != this->_cgi_read_fd_to_client_map.end())
        {
            if (readIt->second == res.clientFd)
            {
                this->eraseFdFromPoll(readIt->first);
                this->_cgi_read_fd_to_client_map.erase(readIt++);
            }
            else ++readIt;
        }
        std::map<int, Connection *>::iterator it = this->_connections.find(res.clientFd);
        if (it != this->_connections.end() && it->second != NULL)
        {
            Connection *conn = it->second;
            conn->response.createResponse(res.statusCode, "CGI Input Write Error", conn->config.error_pages);
            conn->write_buffer = conn->response.responseToString();
            conn->close_after_write = true;
            this->setClientEvents(res.clientFd, POLLOUT);
        }
    }
    else
    {
        if (!this->_cgiManager.hasWriteTask(cgiWriteFd))
        {
            this->_cgi_write_fd_to_client_map.erase(cgiWriteFd);
            this->eraseFdFromPoll(cgiWriteFd);
        }
    }
}

// Handles all file descriptor events returned by poll.
void ServerManager::dispatchEvents()
{
    for (size_t i = this->_poll_fds.size(); i > 0; --i)
    {
        size_t idx = i - 1;
        if (idx >= this->_poll_fds.size() || this->_poll_fds[idx].fd == -1 || this->_poll_fds[idx].revents == 0)
            continue;
        int activeFd = this->_poll_fds[idx].fd;
        short revents = this->_poll_fds[idx].revents;
        if (revents == 0)
            continue;
        if (this->isCgiReadFd(activeFd))
        {
            if (revents & POLLNVAL)
            {
                this->_cgi_read_fd_to_client_map.erase(activeFd);
                this->eraseFdFromPoll(activeFd);
                continue;
            }
            if (revents & (POLLIN | POLLHUP))
            {
                this->handleCgiRead(activeFd);
            }
            else if (revents & POLLERR)
            {
                std::map<int, int>::iterator it = this->_cgi_read_fd_to_client_map.find(activeFd);
                if (it != this->_cgi_read_fd_to_client_map.end())
                {
                    int clientFd = it->second;
                    this->_cgi_read_fd_to_client_map.erase(it);
                    this->_cgiManager.removeTaskByClientFd(clientFd);
                    std::map<int, Connection *>::iterator connIt = this->_connections.find(clientFd);
                    if (connIt != this->_connections.end() && connIt->second != NULL)
                    {
                        Connection *conn = connIt->second;
                        conn->response.createResponse(500, "CGI Read Pipe Error", conn->config.error_pages);
                        conn->write_buffer = conn->response.responseToString();
                        conn->close_after_write = true;
                        this->setClientEvents(clientFd, POLLOUT);
                    }
                }
                this->eraseFdFromPoll(activeFd);
            }
            continue;
        }
        if (this->isCgiWriteFd(activeFd))
        {
            if (revents & POLLNVAL)
            {
                this->_cgi_write_fd_to_client_map.erase(activeFd);
                this->eraseFdFromPoll(activeFd);
                continue;
            }
            if (revents & POLLOUT)
            {
                this->handleCgiWrite(activeFd);
            }
            else if (revents & (POLLERR | POLLHUP))
            {
                std::map<int, int>::iterator it = this->_cgi_write_fd_to_client_map.find(activeFd);
                if (it != this->_cgi_write_fd_to_client_map.end())
                {
                    int clientFd = it->second;
                    this->_cgi_write_fd_to_client_map.erase(it);
                    this->_cgiManager.removeTaskByClientFd(clientFd);
                    std::map<int, Connection *>::iterator connIt = this->_connections.find(clientFd);
                    if (connIt != this->_connections.end() && connIt->second != NULL)
                    {
                        Connection *conn = connIt->second;
                        conn->response.createResponse(500, "CGI Write Pipe Error", conn->config.error_pages);
                        conn->write_buffer = conn->response.responseToString();
                        conn->close_after_write = true;
                        this->setClientEvents(clientFd, POLLOUT);
                    }
                }
                this->eraseFdFromPoll(activeFd);
            }
            continue;
        }
        if (revents & POLLNVAL)
        {
            this->eraseFdFromPoll(activeFd);
            continue;
        }
        if (revents & (POLLERR | POLLHUP))
        {
            if (this->isListenFd(activeFd))
            {
                std::cerr << "[ServerManager] CRITICAL: Fatal event (" << revents << ") on Listen FD " << activeFd << "!" << std::endl;
                this->_poll_fds[idx].fd = -1;
            }
            else this->closeConnection(activeFd, idx);
            continue;
        }
        if (revents & POLLIN)
        {
            if (this->isListenFd(activeFd))
                this->acceptNewConnection(activeFd);
            else this->handleClientRead(activeFd, idx);
        }
        if (revents & POLLOUT)
        {
            if (idx < this->_poll_fds.size() && this->_poll_fds[idx].fd == activeFd)
                this->handleClientWrite(activeFd, idx);
        }
    }
}

// Stops the server and cleans its open resources.
void ServerManager::stop()
{
    std::cout << "\n[Server] Shutting down gracefully..." << std::endl;
    this->_cgiManager.stopAllTasks();
    for (std::map<int, Connection *>::iterator it = _connections.begin(); it != _connections.end(); ++it)
    {
        if (it->second != NULL)
        {
            delete it->second;
        }
    }
    _connections.clear();
    for (size_t i = 0; i < _listen_sockets.size(); ++i)
    {
        if (_listen_sockets[i] != NULL)
            delete _listen_sockets[i];
    }
    _listen_sockets.clear();
    _listen_socket_map.clear();
    _poll_fds.clear();
    std::cout << "[Server] Cleaned up all sockets and CGI processes. Bye!" << std::endl;
}

// Runs the main server event loop.
void ServerManager::run()
{
    g_loop_running = true;
    if (this->_poll_fds.empty())
    {
        std::cerr << "[ServerManager] Error: No listening sockets in poll tree. Aborting run()." << std::endl;
        return;
    }
    std::cout << "[ServerManager] Main loop started. Entering the matrix..." << std::endl;
    int poll_error_retries = 0;
    setupSignalHandlers();
    while (g_loop_running)
    {
        this->prePollCleanup();
        int ret = this->executePoll(poll_error_retries);
        if (ret < 0)
        {
            std::cerr << "[ServerManager] Fatal poll failure limit reached. Breaking main loop." << std::endl;
            break;
        }
        if (ret > 0)
            this->dispatchEvents();
        std::vector<CgiEventResult> timeouts = this->_cgiManager.checkTimeout();
        for (size_t i = 0; i < timeouts.size(); ++i)
        {
            int clientFd = timeouts[i].clientFd;
            int statusCode = timeouts[i].statusCode;
            std::map<int, int>::iterator it = this->_cgi_read_fd_to_client_map.begin();
            while (it != this->_cgi_read_fd_to_client_map.end())
            {
                if (it->second == clientFd)
                {
                    this->eraseFdFromPoll(it->first);
                    this->_cgi_read_fd_to_client_map.erase(it++);
                }
                else ++it;
            }
            this->cleanupClientWritePipe(clientFd);
            std::map<int, Connection *>::iterator connIt = this->_connections.find(clientFd);
            if (connIt != this->_connections.end() && connIt->second != NULL)
            {
                Connection *conn = connIt->second;
                conn->response.createResponse(statusCode, "Gateway Timeout", conn->config.error_pages);
                conn->write_buffer = conn->response.responseToString();
                conn->close_after_write = true;
                this->setClientEvents(clientFd, POLLOUT);
            }
        }
        this->_cgiManager.reapChildren();
    }
    this->stop();
    std::cout << "[ServerManager] Main loop safely terminated." << std::endl;
}
