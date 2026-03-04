# Codebase Review: webserv23

## Overview

This is a C++20 HTTP/1.1 web server built around `epoll` for async I/O, supporting virtual hosting, CGI execution, file uploads, auto-indexing, and redirect rules. The architecture is clean and the event loop is well-structured.

---

## Architecture

The project has two parallel class hierarchies:
- **Legacy**: `Server` / `ServerManager` (`Server.hpp`, `ServerManager.hpp`, `Server.cpp`, `ServerManager.cpp`)
- **Current**: `VirtualHost` / `WebServerCore` (`VirtualHost.hpp`, `WebServerCore.hpp`, corresponding `.cpp` files)

Only `WebServerCore`/`VirtualHost` are used by `main.cpp`. The legacy classes are dead code.

**Recommendation**: Remove `Server.hpp`, `ServerManager.hpp`, `Server.cpp`, `ServerManager.cpp`, and `src/tools/tools.cpp` (duplicated utility functions superseded by `ParsingUtils.cpp`).

---

## Bugs and Correctness Issues

### 1. Path Traversal Vulnerability (High Severity)
**Files**: `src/execute/get.cpp:100`, `src/execute/delete.cpp:6`, `src/execute/post.cpp`

The request path is directly concatenated with `_documentRoot` without sanitization. A request like `GET /../../../etc/passwd` could read arbitrary files outside the document root.

**Fix**: Canonicalize/resolve the path and verify it stays within `_documentRoot`.

### 2. XSS in Auto-Index (Medium Severity)
**File**: `src/execute/get.cpp:68-71`

Directory entry names are inserted into HTML without escaping. A file named `<script>alert(1)</script>.txt` would execute JavaScript.

**Fix**: HTML-escape all user-controlled strings before embedding in HTML.

### 3. XSS in Redirect Page (Low Severity)
**File**: `src/execute/request_io.cpp:12`

The redirect target is inserted into an HTML anchor without encoding. Lower severity since the value comes from the config file.

### 4. `checkTimeoutExpired` Uses Static Variable
**File**: `src/tools/ParsingUtils.cpp:79`

Uses a single `static` variable, so it can only track one timer at a time. Currently dead code (the event loop uses per-session `lastActivityAt`).

### 5. `getErrorPage` Edge Case on 500
**File**: `src/execute/get.cpp:48-52`

If the 500 error page file itself can't be opened, `statusCode` is set to 500 again but returns without a body, resulting in a malformed response.

### 6. `std::remove` Return Value
**File**: `src/execute/delete.cpp:24`

`std::remove` returns non-zero on failure. Checking `< 0` is not portable; should be `!= 0`.

---

## Resource Management

### 7. Raw `new`/`delete` for VirtualHost Pointers
**File**: `src/classes/WebServerCore.cpp:32-33`

`_virtualHosts` uses `vector<VirtualHost*>` with raw `new`. Use `std::unique_ptr<VirtualHost>` for safer ownership.

### 8. Pointless `setsockopt` in Destructor
**File**: `src/classes/WebServerCore.cpp:77`

Setting `SO_REUSEADDR` right before `close()` has no effect. It must be set before `bind()` (already done in `initializeSocket`). This code is misleading.

### 9. Missing `epoll_ctl` Error Checking
**File**: `src/classes/WebServerCore.cpp:45`

The return value of `epoll_ctl` when adding server sockets is not checked.

---

## Design and Code Quality

### 10. Duplicated Code
`Server.cpp`/`VirtualHost.cpp` and `tools.cpp`/`ParsingUtils.cpp` are near-duplicates. Remove the legacy versions.

### 11. Fixed-Size Event Buffer
**File**: `include/WebServerCore.hpp:60`

`_eventBuffer[10]` limits the server to processing 10 events per `epoll_wait` call. Consider increasing to 64 or 128 for better throughput.

### 12. No HTTP Keep-Alive
**File**: `src/execute/response.cpp:15`

`Connection: close` is always sent, forcing a new TCP connection per request.

### 13. Config Parser Is Position-Dependent
**File**: `src/tools/ParsingUtils.cpp:90-112`

`extractConfigValue` searches from the start of the block. Duplicate keywords could match the wrong value.

### 14. Logger Global State
**File**: `include/webserv.hpp:56-66`

The `logger` namespace uses `inline` globals that are not thread-safe. `errorMsg` accumulates across requests and could leak between sessions if `write()` is not called.

### 15. `operator<<` Writes to `std::cout` Directly
**File**: `src/classes/VirtualHost.cpp:276-282`

The `operator<<` overload writes to both the passed `ostream` and `std::cout` directly, which is a bug when output is redirected.

### 16. Missing MIME Types
**File**: `src/tools/lookup.cpp:19-23`

Missing common types: `.jpg`/`.jpeg`, `.gif`, `.ico`, `.json`, `.pdf`, `.mp4`, `.woff2`. Unknown types incorrectly default to `text/html` instead of `application/octet-stream`.

---

## Build System

### 17. Legacy Files Compiled
`Makefile` line 1 uses `find ./src -iname "*.cpp"`, picking up all files including unused legacy code, bloating the binary.

### 18. Undefined `$(LIBFT)`
`Makefile` line 32 references `$(LIBFT)` which is never defined. Harmless (expands to empty) but should be removed.

---

## Positive Aspects

- Async CGI pipeline with epoll-monitored pipes is well implemented
- Chunked transfer-encoding parsing is correct and handles edge cases
- Non-blocking event loop properly handles partial reads/writes
- Good separation of concerns between parsing, routing, and response building
- Clean, readable config format
- Thorough error handling during socket setup with proper cleanup

---

## Top Recommendations (Priority Order)

1. **Fix path traversal** — Canonicalize paths and ensure they stay within `_documentRoot`
2. **Remove dead code** — Delete `Server`/`ServerManager` classes and `tools.cpp`
3. **HTML-escape user-controlled output** — Auto-index entries and any reflected input
4. **Use `unique_ptr`** for `VirtualHost` ownership in `WebServerCore`
5. **Add more MIME types** and default unknown to `application/octet-stream`
6. **Fix `operator<<`** to not write to `std::cout` directly
