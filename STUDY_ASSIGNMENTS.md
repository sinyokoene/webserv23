# Study Assignments - Quick Reference

## 👤 Member 1: Configuration & Server Initialization ANASS

### Primary Files:
```
header/Server.hpp
src/classes/Server.cpp
src/classes/ServerManager.cpp (lines 16-71, constructor only)
src/tools/tools.cpp (findKeyword, findSegment functions)
configs/default.conf
```

### Main Responsibilities:
- Config file parsing logic
- Server class implementation
- Socket initialization
- ServerManager initialization

---

## 👤 Member 2: Event Loop & Connection Management KARIM

### Primary Files:
```
header/ServerManager.hpp (run(), connection management methods)
src/classes/ServerManager.cpp (lines 73-91, destructor)
src/execute/run.cpp (entire file)
src/tools/tools.cpp (timer, getTimeMS functions)
src/tools/log.cpp
```

### Main Responsibilities:
- Epoll event loop
- Connection acceptance and management
- Request reading with timeouts
- Response sending
- Signal handling

---

## 👤 Member 3: HTTP Request Processing & Method Handlers SINYO

### Primary Files:
```
src/parse/request.cpp
src/execute/get.cpp
src/execute/post.cpp
src/execute/delete.cpp
src/execute/cgi.cpp
src/tools/lookup.cpp
src/tools/tools.cpp (isDir, layoutPage, and other utilities)
```

### Main Responsibilities:
- HTTP request parsing
- GET/POST/DELETE method handlers
- CGI script execution
- Error page handling
- File type detection

---

## 📋 Shared Files (All Members Should Understand)

```
src/main.cpp - Entry point
header/webserv.hpp - Common includes and declarations
```

---

## 🔄 Data Flow Between Parts

```
[Part 1] Config File
    ↓
[Part 1] Server instances created
    ↓
[Part 2] Event loop starts
    ↓
[Part 2] Accept connection
    ↓
[Part 2] Read request
    ↓
[Part 3] Parse request
    ↓
[Part 3] Handle method (GET/POST/DELETE/CGI)
    ↓
[Part 2] Send response
    ↓
[Part 2] Close connection
```

---

## 🎯 Key Integration Points

1. **Part 1 → Part 2:**
   - `ServerManager::_servers` vector contains Server instances
   - Server's `_serverFD` is added to epoll in Part 2

2. **Part 2 → Part 3:**
   - `getHttpRequest()` calls `parseRequest()`
   - `parseRequest()` calls method handlers based on request method

3. **Part 3 → Part 1:**
   - Handlers use `Server::_locations` for routing
   - Handlers use `Server::_errorTable` for error pages
   - Handlers use `Server::_path` for file system access

---

## 📊 Code Statistics

- **Part 1:** ~240 lines (Server.cpp) + ~60 lines (ServerManager constructor)
- **Part 2:** ~150 lines (run.cpp) + ~20 lines (destructor)
- **Part 3:** ~90 lines (request.cpp) + ~200 lines (handlers) + ~50 lines (utilities)

---

## ✅ Study Checklist

### Member 1 Checklist:
- [ ] Understand config file format
- [ ] Understand Server constructor
- [ ] Understand socket setup (bind, listen)
- [ ] Understand location parsing
- [ ] Understand error page parsing

### Member 2 Checklist:
- [ ] Understand epoll mechanism
- [ ] Understand connection lifecycle
- [ ] Understand request reading with timeouts
- [ ] Understand response sending
- [ ] Understand signal handling

### Member 3 Checklist:
- [ ] Understand HTTP request structure
- [ ] Understand request parsing
- [ ] Understand GET handler (file serving, auto-index)
- [ ] Understand POST handler (file upload)
- [ ] Understand DELETE handler
- [ ] Understand CGI execution
- [ ] Understand error handling

