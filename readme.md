*This project has been created as part of the 42 curriculum by yzhang2, weiyang.*
 
 
## Description

**Webserv** is a 42 School project whose goal is to write a custom, non-blocking HTTP/1.1 web server in **C++98**. The project focuses on understanding low-level network programming, asynchronous event loops using I/O multiplexing (`select`, `poll`, `epoll`, or `kqueue`), HTTP/1.1 protocol parsing, Static File Serving, Autoindex generation, File Uploads, Session & Cookie Management, and CGI (Common Gateway Interface) execution.

The behavior of Webserv aims to be compliant with *RFC 7230–7235 (HTTP/1.1)* and *RFC 3875 (CGI/1.1)*, tested and verified against modern web browsers and `curl`.

---

## Instructions

### Compilation

To compile the project, run in the terminal:
```bash
    make 
```
This will generate the file executable :
```bash
    ./webserv
```
Launch the program  with:
```bash
    ./webserv [path_to_config.conf]
```
Example usage:
```bash
./webserv config/default.conf
```
Once running, access the server via your browser or terminal:

```
    curl -i http://localhost:[port]/
```
### Features

* Non-blocking I/O & Event Loop: Built on I/O multiplexing (poll) to handle multiple concurrent client connections asynchronously without thread/process blocking.

* Robust HTTP/1.1 Parsing: Request line, headers, and body parsing with support for chunked Transfer-Encoding and Content-Length.

* Static File Serving & Autoindex: Serves static assets (.html, .css, .js, images, etc.) and dynamically generates directory listings when autoindex is enabled.

* File Uploads & Deletion: Supports POST file uploads and DELETE requests for managing server storage.

* CGI Execution: Executes external dynamic scripts (Python, Bash, PHP, etc.) following the RFC 3875 CGI standard.

* Session & Cookie Management: Full session persistence support (Set-Cookie, WEBSERV_SESSION), Session ID rotation on login (preventing session fixation), sliding expiration, and explicit logout destruction.

* Custom Error Pages: Customizable HTTP error status responses (e.g., 400, 403, 404, 405, 413, 500, 502, 504).


### Configuration File Syntax
Webserv uses an Nginx-style configuration structure, for example :
```
server {
    listen 8080;
    server_name localhost example.com;
    root /var/www/html;
    index index.html;
    max_body_size 10M;

    error_page 404 /errors/404.html;

    location / {
        allow_methods GET POST;
        autoindex on;
    }

    location /upload/ {
        allow_methods GET POST DELETE;
        upload_path /var/www/uploads;
    }

    location /cgi/ {
        allow_methods GET POST;
        cgi_extension .py /usr/bin/python3;
        cgi_extension .sh /bin/bash;
    }
}
```
### CGI Implementation & Non-Blocking Design
#### Our CGI system provides safe execution of dynamic scripts without hanging the server:
* Asynchronous Execution: CGI execution runs in spawned child processes (fork + execve). Pipe File Descriptors are set to non-blocking mode to prevent the main server loop from stalling during I/O reads/writes.

* RFC 3875 Environment Setup: Environment variables (REQUEST_METHOD, QUERY_STRING, CONTENT_LENGTH, CONTENT_TYPE, SCRIPT_FILENAME, HTTP_COOKIE, etc.) are accurately populated.

* Timeout & Leak Protection: Implements activity-based execution timeouts (returns 504 Gateway Timeout if a script hangs or freezes) and guarantees child process reaping via waitpid to prevent zombie processes.

### Session & Cookie Architecture
#### Webserv implements standard stateful HTTP session handling:
* Session ID Storage: Sensitve data is kept entirely server-side in a SessionStore map, passing only a 64-character hex ID via WEBSERV_SESSION cookies.

* Session Fixation Defense: Re-generates and replaces the Session ID upon login (/session/login) to prevent credential hijacking.

* Sliding Expiration: Resets the inactivity timer on valid requests (/session/counter) to keep active users logged in seamlessly.

* Logout Destruction: Issuing a logout (/session/logout) erases the session from server memory and returns Set-Cookie with Max-Age=0 to immediately expire the browser cookie.

### Error Handling & HTTP Status Codes

| Status Code | Meaning | Cause |
| :--- | :--- | :--- |
| `200 OK` | Success | Request succeeded |
| `201 Created` | Created | Resource successfully uploaded/created |
| `204 No Content` | Deleted | Resource successfully deleted |
| `400 Bad Request` | Syntax Error | Malformed HTTP request line or headers |
| `403 Forbidden` | Access Denied | File permissions restricted or autoindex off for directory |
| `404 Not Found` | Not Found | Requested file or route does not exist |
| `405 Method Not Allowed` | Method Blocked | HTTP method not permitted by location rule |
| `413 Payload Too Large` | Limit Exceeded | Body size exceeds `max_body_size` |
| `500 Internal Error` | Server Error | CGI crash or internal failure |
| `502 Bad Gateway` | Invalid CGI Response | CGI output missing valid HTTP headers |
| `504 Gateway Timeout` | Execution Timeout | CGI script timed out without output progress |

### Testing
We can thoroughly test Webserv features through Browser Front-end Panel, cURL CLI, Integration Test Suite, and Stress Testing Tools.
#### 1. Interactive Front-end Test Panel
Webserv includes a built-in single-page interactive test panel. Once the server is running, open your browser and navigate to:
```
    http://127.0.0.1:8080/index.html

```
The panel is split into 4 testing sections:

* Section 1 (GET & Autoindex): Test file retrieval, directory listing, and customized error pages.

* Section 2 (File Manager & Drag Upload): Drag-and-drop file uploading (POST) and file deletion (DELETE).

* Section 3 (CGI Test Suite): Execute Python/Shell CGI scripts, test STDIN/STDOUT piping, streaming responses, and error triggers (500 crash, 504 activity timeout).

* Section 4 (Session & Cookie Management): Interactive 3-step test for login rotation (/session/login), visit counter with sliding renewal (/session/counter), and session destruction (/session/logout).

#### 2. Manual Testing via curl
##### Static Files & Headers:
```
# Basic GET request
curl -i http://127.0.0.1:8080/

# Test Chunked Transfer
``curl -i -X POST -H "Transfer-Encoding: chunked" -H "Content-Length:" -H "Connection: close" -H "Content-Type: text/plain" --data-binary $'5\r\nHello\r\n0\r\n\r\n' http://127.0.0.1:8080/upload
```
##### File Upload & Delete:
```
# Upload a file
```
printf "5\r\nHello\r\n0\r\n\r\n" | curl -i -X POST -H "Transfer-Encoding: chunked" -H "Content-Length:" -H "Connection: close" -H "Content-Type: text/plain" --data-binary @- http://127.0.0.1:8080/upload/sample.txt
```
# Delete a file
curl -i -X DELETE http://127.0.0.1:8080/upload/sample.txt
```

##### CGI Execution:
```
# POST payload to CGI script
curl -i -X POST -d "user=student&msg=hello" http://127.0.0.1:8080/cgi/echo.py
```
##### Session & Cookie Workflow:
```
# 1. Login and save cookie
curl -i -c cookies.txt -X POST http://127.0.0.1:8080/session/login -d "user=student"

# 2. Visit counter with cookie
curl -i -b cookies.txt -c cookies.txt http://127.0.0.1:8080/session/counter

# 3. Logout (returns Max-Age=0)
curl -i -b cookies.txt -c cookies.txt -X POST http://127.0.0.1:8080/session/logout
```
#### 3. Load & Stress Testing (siege)
o evaluate high concurrency, memory stability, non-blocking I/O multiplexing, and availability under heavy load, use siege:
##### Concurrency & Availability Test:

Run 250 concurrent users sending requests for 10 seconds:
```
siege -c250 -t10S http://127.0.0.1:8080/index.html
```
##### HTTP Keep-Alive & High Concurrency:
Verify persistent connection handling with heavy throughput:
```
siege -b -c100 -t15S http://127.0.0.1:8080/
```
##### POST Load Testing:
Benchmark file uploads/POST handling under stress:
```
siege -c50 -t10S 'http://127.0.0.1:8080/upload/test.txt POST payload_data'
```

#### 4. Memory & File Descriptor (FD) Leak Prevention Testing
##### Memory Leak Check (valgrind):
Run Webserv under Valgrind to ensure all allocated memory is properly freed upon shutdown and during continuous client handling:
```
valgrind --leak-check=full --show-leak-kinds=all --track-fds=yes ./webserv config/default.conf
```
* Success Criteria: definitely lost: 0 bytes in 0 blocks and indirectly lost: 0 bytes in 0 blocks.
##### File Descriptor (FD) Leak Check (lsof / /proc):
Verify that sockets, pipe FDs from CGI execution, and opened file descriptors are closed immediately after request completion:
```
# 1. Get the PID of the running webserv process
PID=$(pgrep webserv)

# 2. Monitor open File Descriptors before testing
lsof -p $PID

# 3. Run high-concurrency requests or multiple CGI calls in another terminal
siege -c50 -t5S http://127.0.0.1:8080/cgi/echo.py

# 4. Check open File Descriptors again to ensure count returns to baseline
lsof -p $PID | wc -l
```
* Success Criteria: Open FD count must return to its initial idle baseline after all requests finish.

#### 5. Edge Cases & Error Testing
* 413 Payload Too Large: Try uploading a file larger than max_body_size configured in default.conf.

* 405 Method Not Allowed: Send a POST request to a route with allow_methods GET only.

* 504 Gateway Timeout: Request /cgi/slow_timeout.py to verify that inactive CGI execution is interrupted after 10 seconds.

### Limitations

* HTTP/1.1 Only: HTTP/2, HTTP/3, and HTTPS (SSL/TLS) are not supported.

* Chunked Upload Limits: Transfer-Encoding: chunked requests are processed but constrained by configured max body limits.

* No Database Driver: Native persistent database connections are not included; storage relies on file system and server memory maps.

## Resources
### Technical References

* RFC 7230–7235: Hypertext Transfer Protocol (HTTP/1.1)

* RFC 3875: The Common Gateway Interface (CGI) Version 1.1

* RFC 6265: HTTP State Management Mechanism (Cookies)

* Advanced Programming in the UNIX Environment – W. Richard Stevens

* Linux man pages: man poll, man epoll, man select, man kqueue, man socket, man bind, man listen

### AI Usage Disclosure

#### AI tools (such as ChatGPT and Google Gemini) were used for:

* Clarifying edge cases in HTTP/1.1 and CGI specification details.

* Structuring interactive test suite tools (index.html frontend test panel).

* Refining technical documentation (README formatting and clarity).

All architectural design decisions and code implementation were written, debugged, and validated by the project authors.
