#ifndef SERVERSOCKET_HPP
#define SERVERSOCKET_HPP
class ServerSocket
{
private:
    int _fd;
    std::string _host;
    int _port;
    // Creates a new ServerSocket object.
    ServerSocket(const ServerSocket &);
    // Copies data from another object.
    ServerSocket &operator=(const ServerSocket &);
    // Sets non blocking.
    void setNonBlocking();
public:
    // Creates a new ServerSocket object.
    ServerSocket(const std::string &host, int port);
    // Cleans up this object and its owned resources.
    ~ServerSocket();
    // Creates, binds, and starts the listening socket.
    void setup();
    // Closes fd.
    void closeFd();
    // Returns fd.
    int getFd() const;
    // Returns host.
    const std::string &getHost() const;
    // Returns port.
    int getPort() const;
};
#endif
