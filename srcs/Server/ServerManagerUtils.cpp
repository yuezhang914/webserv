#include "Webserv.hpp"
#include "SessionStore.hpp"
#include "CgiHandler.hpp"

// Checks whether the file descriptor is a listening socket.
bool ServerManager::isListenFd(int fd)
{
    if (this->_listen_socket_map.count(fd) > 0)
        return true;
    return false;
}

// Checks whether the file descriptor is a CGI read pipe.
bool ServerManager::isCgiReadFd(int fd) const
{
    return this->_cgi_read_fd_to_client_map.find(fd) != this->_cgi_read_fd_to_client_map.end();
}

// Checks whether the file descriptor is a CGI write pipe.
bool ServerManager::isCgiWriteFd(int fd) const
{
    return this->_cgi_write_fd_to_client_map.find(fd) != this->_cgi_write_fd_to_client_map.end();
}

// Sets client events.
void ServerManager::setClientEvents(int clientFd, short events)
{
    for (size_t i = 0; i < this->_poll_fds.size(); ++i)
    {
        if (this->_poll_fds[i].fd == clientFd)
        {
            this->_poll_fds[i].events = events;
            return;
        }
    }
}

// Removes fd from poll.
void ServerManager::eraseFdFromPoll(int targetFd)
{
    if (targetFd == -1)
        return;
    for (size_t i = 0; i < this->_poll_fds.size(); ++i)
    {
        if (this->_poll_fds[i].fd == targetFd)
        {
            this->_poll_fds[i].fd = -1;
            this->_poll_fds[i].events = 0;
            this->_poll_fds[i].revents = 0;
            break;
        }
    }
}

// Closes connection.
void ServerManager::closeConnection(int clientFd, size_t pollIndex)
{
    this->releaseLargeCgiSlot(clientFd);
    this->_cgiManager.removeTaskByClientFd(clientFd);
    std::map<int, int>::iterator readIt = this->_cgi_read_fd_to_client_map.begin();
    while (readIt != this->_cgi_read_fd_to_client_map.end())
    {
        if (readIt->second == clientFd)
        {
            this->eraseFdFromPoll(readIt->first);
            this->_cgi_read_fd_to_client_map.erase(readIt++);
        }
        else ++readIt;
    }
    std::map<int, int>::iterator writeIt = this->_cgi_write_fd_to_client_map.begin();
    while (writeIt != this->_cgi_write_fd_to_client_map.end())
    {
        if (writeIt->second == clientFd)
        {
            this->eraseFdFromPoll(writeIt->first);
            this->_cgi_write_fd_to_client_map.erase(writeIt++);
        }
        else ++writeIt;
    }
    if (pollIndex < this->_poll_fds.size() && this->_poll_fds[pollIndex].fd == clientFd)
    {
        this->_poll_fds[pollIndex].fd = -1;
        this->_poll_fds[pollIndex].events = 0;
        this->_poll_fds[pollIndex].revents = 0;
    }
    else
    {
        this->eraseFdFromPoll(clientFd);
    }
    std::map<int, Connection *>::iterator it = this->_connections.find(clientFd);
    if (it != this->_connections.end())
    {
        Connection *connection = it->second;
        delete connection;
        this->_connections.erase(it);
    }
    DEBUG_LOG("[ServerManager] Client FD " << clientFd << " successfully closed and cleaned up.");
}

// Removes closed file descriptors before the next poll call.
void ServerManager::prePollCleanup()
{
    for (size_t i = 0; i < this->_poll_fds.size();)
    {
        if (this->_poll_fds[i].fd == -1)
        {
            this->_poll_fds.erase(this->_poll_fds.begin() + i);
        }
        else ++i;
    }
}

// Runs poll.
int ServerManager::executePoll(int &retries)
{
    int ret = ::poll(&this->_poll_fds[0], this->_poll_fds.size(), 1000);
    if (ret < 0)
    {
        retries++;
        std::cerr << "[executePoll] Warning: poll() returned " << ret << ", retrying (" << retries << "/5)..." << std::endl;
        if (retries >= 5)
        {
            std::cerr << "[executePoll] Fatal: poll() failed consecutively over limit! Terminating main loop." << std::endl;
            return -1;
        }
        return 0;
    }
    if (ret > 0)
        retries = 0;
    return ret;
}

// Adds one file descriptor to the poll list.
void ServerManager::registerFdToPoll(int fd, short events)
{
    if (fd < 0)
    {
        std::cerr << "[ServerManager] Error: Attempted to register invalid negative FD: " << fd << std::endl;
        return;
    }
    for (size_t i = 0; i < this->_poll_fds.size(); ++i)
    {
        if (this->_poll_fds[i].fd == fd)
        {
            DEBUG_LOG("[ServerManager] Notice: FD " << fd << " already registered in poll tree. Updating events instead.");
            this->_poll_fds[i].events = events;
            this->_poll_fds[i].revents = 0;
            return;
        }
    }
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = events;
    pfd.revents = 0;
    this->_poll_fds.push_back(pfd);
    DEBUG_LOG("[ServerManager] FD " << fd << " successfully registered to poll tree with events: " << events);
}

// Removes the CGI write pipe for one client.
void ServerManager::cleanupClientWritePipe(int clientFd)
{
    std::map<int, int>::iterator it = this->_cgi_write_fd_to_client_map.begin();
    while (it != this->_cgi_write_fd_to_client_map.end())
    {
        if (it->second == clientFd)
        {
            int writeFd = it->first;
            this->eraseFdFromPoll(writeFd);
            this->_cgi_write_fd_to_client_map.erase(it++);
            return;
        }
        else ++it;
    }
}
