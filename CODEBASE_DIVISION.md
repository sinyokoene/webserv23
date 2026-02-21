# Codebase Division for Study

This document divides the webserv codebase into three logical parts for three team members to study and understand.

## Overview
This is a C++ HTTP/1.1 web server that supports:
- Multiple server instances
- GET, POST, DELETE methods
- CGI script execution
- Config file-based server configuration
- Epoll-based event loop for efficient I/O

---

## 📦 PART 1: Configuration & Server Initialization ANASS
**Focus:** How servers are configured, parsed, and initialized

### Files to Study:
- `header/Server.hpp` - Server class definition
- `src/classes/Server.cpp` - Server implementation
- `header/ServerManager.hpp` - ServerManager class definition (constructor only)
- `src/classes/ServerManager.cpp` - ServerManager constructor (lines 16-71)
- `src/tools/tools.cpp` - Utility functions for parsing (`findKeyword`, `findSegment`)
- `configs/default.conf` - Example configuration file

### Key Concepts:
1. **Config File Parsing:**
   - How the config file is read and parsed
   - Extracting server parameters (HOST, PORT, PATH, BODY_SIZE, TIME_OUT)
   - Parsing LOCATION blocks with permissions, index, auto-index settings
   - Parsing error pages (PAGE_404, PAGE_413, etc.)

2. **Server Class:**
   - Constructor that parses config string
   - `setUp()` method for socket initialization
   - Data structures: `_locations`, `_errorTable`
   - Error handling: `ParseError`, `InitError`

3. **Socket Setup:**
   - `getaddrinfo()` for address resolution
   - `socket()`, `bind()`, `listen()` system calls
   - `setsockopt()` for socket reuse
   - Error handling during initialization

4. **ServerManager Initialization:**
   - Reading config file
   - Creating multiple Server instances
   - Setting up epoll file descriptor
   - Logging initialization

### Study Questions:
- How does the config file format work?
- What happens if a config file has invalid syntax?
- How are multiple servers created from one config file?
- What is the purpose of `validateLocations()`?
- How does socket binding work?

---

## 🔄 PART 2: Event Loop & Connection Management KARIM
**Focus:** How the server handles connections, I/O events, and manages client connections

### Files to Study:
- `header/ServerManager.hpp` - ServerManager class (especially `run()` and connection management)
- `src/classes/ServerManager.cpp` - ServerManager destructor (lines 73-91)
- `src/execute/run.cpp` - Event loop and connection handling
- `src/tools/tools.cpp` - Timer functions (`timer()`, `getTimeMS()`)
- `src/tools/log.cpp` - Logging system

### Key Concepts:
1. **Epoll Event Loop:**
   - `epoll_create1()` for creating epoll instance
   - `epoll_ctl()` for adding/modifying/removing file descriptors
   - `epoll_wait()` for waiting on events
   - Event types: `EPOLLIN` (read), `EPOLLOUT` (write)

2. **Connection Lifecycle:**
   - `acceptNewConnection()` - Accepting new client connections
   - `getHttpRequest()` - Reading HTTP requests from clients
   - `readRequest()` - Reading request with timeout handling
   - `sendHttpResponse()` - Sending HTTP responses
   - Connection cleanup and resource management

3. **Client Table Management:**
   - `_clientTable` map structure (clientFD -> Connection)
   - Connection struct containing Server, Request, Response
   - Adding/removing connections from epoll

4. **Request Reading:**
   - Reading HTTP headers (until `\r\n\r\n`)
   - Reading request body based on `Content-Length`
   - Timeout handling with `timer()`
   - Body size validation (413 error)

5. **Response Sending:**
   - Building HTTP response headers
   - Sending response body
   - Connection cleanup after sending

6. **Signal Handling:**
   - `SIGINT` handling for graceful shutdown
   - `isRunning` flag for loop control

### Study Questions:
- How does epoll work and why is it efficient?
- What happens when a new client connects?
- How are timeouts handled during request reading?
- What is the difference between `EPOLLIN` and `EPOLLOUT` events?
- How does the server handle multiple concurrent connections?
- What happens if a client disconnects unexpectedly?

---

## 🌐 PART 3: HTTP Request Processing & Method Handlers SINYO
**Focus:** How HTTP requests are parsed and processed, method handlers, and CGI execution

### Files to Study:
- `src/parse/request.cpp` - HTTP request parsing
- `src/execute/get.cpp` - GET method handler
- `src/execute/post.cpp` - POST method handler
- `src/execute/delete.cpp` - DELETE method handler
- `src/execute/cgi.cpp` - CGI script execution
- `src/tools/lookup.cpp` - File type and error code lookups
- `src/tools/tools.cpp` - Utility functions (`isDir()`, `layoutPage()`, etc.)

### Key Concepts:
1. **Request Parsing:**
   - `parseRequest()` - Parsing HTTP request line and headers
   - Extracting: method, path, protocol, server name, port
   - Parsing `Content-Length` header
   - `fillBody()` - Parsing request body (including multipart/form-data)
   - Protocol validation (HTTP/1.1)
   - Method validation (GET, POST, DELETE)

2. **GET Handler:**
   - `handleGet()` - Processing GET requests
   - File serving
   - Auto-indexing (`autoIndex()`) for directories
   - Index file handling
   - Error page retrieval (`getErrorPage()`)

3. **POST Handler:**
   - `handlePost()` - Processing POST requests
   - File upload handling
   - Temporary file management
   - Form data processing

4. **DELETE Handler:**
   - `handleDelete()` - Processing DELETE requests
   - File deletion
   - Error handling for non-existent files

5. **CGI Execution:**
   - `handleCGI()` - Executing CGI scripts
   - Process forking (`fork()`)
   - Pipe creation for stdin/stdout
   - Environment variable setup
   - Timeout handling for CGI scripts
   - Reading CGI output

6. **Error Handling:**
   - Error code mapping (`getErrorCode()`)
   - Error page retrieval
   - Default error pages
   - HTTP status codes (200, 400, 404, 413, 500, 501, 504, 505)

7. **Utilities:**
   - File type detection (`getFullFileType()`)
   - Directory checking (`isDir()`)
   - Location matching (`smartFindLocation()`)
   - Default page generation

### Study Questions:
- How is an HTTP request parsed into its components?
- What happens when a GET request is made for a directory?
- How does auto-indexing work?
- How are file uploads handled in POST requests?
- How does CGI script execution work?
- What happens if a CGI script times out?
- How are error pages selected and served?
- How does multipart/form-data parsing work?

---

## 🔗 Interconnections Between Parts

### Part 1 → Part 2:
- Server instances created in Part 1 are used in Part 2's event loop
- Server configuration (ports, timeouts) affects connection handling

### Part 2 → Part 3:
- Part 2 reads requests and calls Part 3's `parseRequest()`
- Part 2 sends responses built by Part 3's handlers

### Part 3 → Part 1:
- Part 3 uses server configuration (locations, error pages) from Part 1
- Location matching uses `_locations` map from Server class

---

## 📚 Recommended Study Order

1. **Start with Part 1** - Understand how servers are configured
2. **Then Part 2** - Understand how connections are managed
3. **Finally Part 3** - Understand how requests are processed

Or, follow the execution flow:
1. `main.cpp` → `ServerManager` constructor (Part 1)
2. `ServerManager::run()` (Part 2)
3. `getHttpRequest()` → `parseRequest()` → method handlers (Part 3)

---

## 🛠️ Testing Each Part

### Part 1 Testing:
- Create/modify config files
- Test invalid config syntax
- Verify server initialization

### Part 2 Testing:
- Monitor connection acceptance
- Test timeout behavior
- Test concurrent connections

### Part 3 Testing:
- Test GET requests for files and directories
- Test POST file uploads
- Test DELETE operations
- Test CGI script execution
- Test error scenarios (404, 413, etc.)

---

## 📝 Notes

- All parts share common headers: `webserv.hpp`, `Server.hpp`, `ServerManager.hpp`
- The `Request` and `Response` structs are defined in `ServerManager.hpp`
- Logging is used throughout all parts (see `log.cpp`)
- Error handling is consistent across all parts using exceptions

