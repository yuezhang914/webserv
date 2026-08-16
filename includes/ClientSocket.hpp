#ifndef CLIENTSOCKET_HPP
#define CLIENTSOCKET_HPP
class ClientSocket
{
private:
    int _fd;
    // Creates a new ClientSocket object.
    ClientSocket(const ClientSocket &);
    // Copies data from another object.
    ClientSocket &operator=(const ClientSocket &);
public:
    // Creates a new ClientSocket object.
    ClientSocket();
    // Creates a new ClientSocket object.
    explicit ClientSocket(int fd);
    // Cleans up this object and its owned resources.
    ~ClientSocket();
    // Reads available data from the client socket.
    ssize_t read(char *buf, size_t size) const;
    // Sends response data to the client socket.
    ssize_t write(const std::string &data) const;
    // Sets non blocking.
    void setNonBlocking();
    // Closes fd.
    void closeFd();
    // Returns fd.
    int getFd() const;
};
#endif
