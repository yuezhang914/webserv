#include "Webserv.hpp"

// Creates a new Connection object.
Connection::Connection() : socket(NULL), config(), read_buffer(), write_buffer(), request(), response(), close_after_write(false), drain_input_before_close(false), chunk_scan_active(false), chunk_scan_pos(0), chunk_decoded_size(0), chunk_body_limit(0), large_cgi_slot_acquired(false), large_cgi_waiting(false)
{
}

// Creates a new Connection object.
Connection::Connection(int clientFd, const ServerConfig &srv_cfg) : socket(new ClientSocket(clientFd)), config(srv_cfg), read_buffer(), write_buffer(), request(), response(), close_after_write(false), drain_input_before_close(false), chunk_scan_active(false), chunk_scan_pos(0), chunk_decoded_size(0), chunk_body_limit(0), large_cgi_slot_acquired(false), large_cgi_waiting(false)
{
}

// Closes fd.
void Connection::closeFd()
{
    this->socket->closeFd();
}

// Cleans up this object and its owned resources.
Connection::~Connection()
{
    delete this->socket;
}

// Clears the connection state for reuse or cleanup.
void Connection::clear()
{
    std::string().swap(this->read_buffer);
    std::string().swap(this->write_buffer);
    this->close_after_write = false;
    this->drain_input_before_close = false;
    this->chunk_scan_active = false;
    this->chunk_scan_pos = 0;
    this->chunk_decoded_size = 0;
    this->chunk_body_limit = 0;
    this->large_cgi_slot_acquired = false;
    this->large_cgi_waiting = false;
    this->clearRequest();
    this->response.~Response();
    new (&this->response) Response(this->request);
}

// Clears request.
void Connection::clearRequest()
{
    this->request.~Request();
    new (&this->request) Request();
}
