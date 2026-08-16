#ifndef CONNECTION_HPP
#define CONNECTION_HPP
#include "ClientSocket.hpp"
#include "Request.hpp"
#include "Response.hpp"
class Connection
{
public:
    ClientSocket *socket;
    ServerConfig config;
    std::string read_buffer;
    std::string write_buffer;
    Request request;
    Response response;
    bool close_after_write;
    bool drain_input_before_close;
    bool chunk_scan_active;
    size_t chunk_scan_pos;
    size_t chunk_decoded_size;
    unsigned long chunk_body_limit;
    bool large_cgi_slot_acquired;
    bool large_cgi_waiting;
    // Creates a new Connection object.
    Connection();
    // Creates a new Connection object.
    Connection(int clientFd, const ServerConfig &srv_cfg);
    // Cleans up this object and its owned resources.
    ~Connection();
    // Clears the connection state for reuse or cleanup.
    void clear();
    // Clears request.
    void clearRequest();
    // Closes fd.
    void closeFd();
private:
    // Creates a new Connection object.
    Connection(const Connection &other);
    // Copies data from another object.
    Connection &operator=(const Connection &other);
};
#endif
