# Codebase Review: webserv23

## Overview

This is a C++20 HTTP/1.1 web server built around `epoll` for async I/O, supporting virtual hosting, CGI execution, file uploads, auto-indexing, and redirect rules. The architecture is clean and the event loop is well-structured.

---

## Architecture

The project has two parallel class hierarchies:
- **Legacy**: `Server` / `ServerManager` (`Server.hpp`, `ServerManager.hpp`, `Server.cpp`, `ServerManager.cpp`)
- **Current**: `VirtualHost` / `WebServerCore` (`VirtualHost.hpp`, `WebServerCore.hpp`, corresponding `.cpp` files)

Only `WebServerCore`/`VirtualHost` are used by `main.cpp`. The legacy classes are dead code.

**Recommendation**: Exclude legacy sources from the build (or remove them) and verify zero references before deleting.

---

## Bugs and Correctness Issues

### 1. Path Traversal via POST Upload Filename (High Severity)
**File**: `src/execute/post.cpp:21`

The multipart `filename=` field from the request body is stored in `httpRequest.fileName` and directly concatenated into the file path in `resolvePostPath`:
```cpp
std::string absolutePath = virtualHost._documentRoot + httpRequest.requestPath + httpRequest.fileName;
```
An attacker can craft a multipart upload with `filename="../../etc/cron.d/evil"` to write files outside the document root. The `fileName` is extracted from the request body without any `..` normalization or path validation.

**Fix**: Sanitize `httpRequest.fileName` — strip directory components (use only the basename) and reject paths containing `..` segments.

### 2. Path Traversal via CGI Path Construction (High Severity)
**File**: `src/execute/cgi.cpp:96-97`

CGI script path is constructed as:
```cpp
const std::string scriptPath = std::filesystem::absolute(
    virtualHost._documentRoot + httpRequest.requestPath).string();
```
While `std::filesystem::absolute` resolves the path, it does not prevent `..` segments from escaping the document root. A request to `/cgi-bin/../../etc/passwd` passes route resolution (since `/cgi-bin` is checked via `requestPath.find("/cgi-bin") == 0` at `request_io.cpp:146`, not via `resolveRoute`), and the resulting path could point outside `_documentRoot`.

**Fix**: After resolving the absolute path, verify it starts with the canonical `_documentRoot` prefix before executing.

### 3. XSS in Auto-Index (Medium Severity)
**File**: `src/execute/get.cpp:68-71`

Directory entry names are inserted into HTML without escaping. A file named `<script>alert(1)</script>.txt` would execute JavaScript in the browser.

**Fix**: HTML-escape all user-controlled strings before embedding in HTML.

### 4. XSS in Redirect Page (Low Severity)
**File**: `src/execute/request_io.cpp:12`

The redirect target is inserted into an HTML anchor without encoding. Lower severity since the value comes from the config file (not user input).

### 5. `checkTimeoutExpired` Uses Static Variable (Dead Code)
**File**: `src/tools/ParsingUtils.cpp:79`

Uses a single `static` variable, so it can only track one timer at a time. Currently dead code — the event loop correctly uses per-session `lastActivityAt` instead. Can be removed.

### 6. `getErrorPage` Edge Case on 500
**File**: `src/execute/get.cpp:48-52`

If the configured 500 error page file can't be opened, `statusCode` is set to 500 and the function returns without setting a body. Response serialization via `buildSerializedResponse` will still emit a valid HTTP response with `Content-Length: 0`, so this is not a protocol violation — but the user sees an empty page with no error information.

### 7. `std::remove` Return Value (Style/Portability)
**File**: `src/execute/delete.cpp:24`

`std::remove` returns non-zero on failure. Checking `< 0` works on POSIX (returns `-1` on failure) but `!= 0` is more portable and idiomatic C/C++.

---

## Resource Management

### 8. Raw `new`/`delete` for VirtualHost Pointers
**File**: `src/classes/WebServerCore.cpp:32-33`

`_virtualHosts` uses `vector<VirtualHost*>` with raw `new`. Use `std::unique_ptr<VirtualHost>` for safer ownership.

### 9. Pointless `setsockopt` in Destructor
**File**: `src/classes/WebServerCore.cpp:77`

Setting `SO_REUSEADDR` right before `close()` has no effect. It must be set before `bind()` (already done in `initializeSocket`). This code is misleading and should be removed.

### 10. Missing `epoll_ctl` Error Checking
**File**: `src/classes/WebServerCore.cpp:45`

The return value of `epoll_ctl` when adding server sockets is not checked.

---

## Design and Code Quality

### 11. Duplicated Code
`Server.cpp`/`VirtualHost.cpp` and `tools.cpp`/`ParsingUtils.cpp` are near-duplicates. Safer first step: exclude legacy sources from the Makefile build and verify zero references, then delete.

### 12. Fixed-Size Event Buffer
**File**: `include/WebServerCore.hpp:60`

`_eventBuffer[10]` limits the server to processing 10 events per `epoll_wait` call. Consider increasing to 64 or 128 for better throughput under load.

### 13. No HTTP Keep-Alive
**File**: `src/execute/response.cpp:15`

`Connection: close` is always sent, forcing a new TCP connection per request.

### 14. Config Parser Is Position-Dependent
**File**: `src/tools/ParsingUtils.cpp:90-112`

`extractConfigValue` searches from the start of the block. Duplicate keywords could match the wrong value.

### 15. Logger Cross-Request State Bleed
**File**: `include/webserv.hpp:56-66`

The `logger` namespace uses `inline` globals. The server is single-threaded so thread safety is not a concern, but the real issue is that `errorMsg` accumulates across requests. If a code path calls `logger::addMsg` but the corresponding `logger::write` is never called (e.g., early return on error), stale messages from a previous request can bleed into the next one.

### 16. `operator<<` Writes to `std::cout` Directly
**File**: `src/classes/VirtualHost.cpp:276-282`

The `operator<<` overload writes to both the passed `ostream` and `std::cout` directly, which is a bug when output is redirected.

### 17. Missing MIME Types
**File**: `src/tools/lookup.cpp:19-23`

Missing common types: `.jpg`/`.jpeg`, `.gif`, `.ico`, `.json`, `.pdf`, `.mp4`, `.woff2`. Unknown types incorrectly default to `text/html` instead of `application/octet-stream`.

---

## Build System

### 18. Legacy Files Compiled
`Makefile` line 1 uses `find ./src -iname "*.cpp"`, picking up all files including unused legacy code, bloating the binary.

### 19. Undefined `$(LIBFT)`
`Makefile` line 32 references `$(LIBFT)` which is never defined. Harmless (expands to empty) but should be removed.

---

## Positive Aspects

- Async CGI pipeline with epoll-monitored pipes is well implemented
- Chunked transfer-encoding parsing is correct and handles edge cases
- Non-blocking event loop properly handles partial reads/writes
- Good separation of concerns between parsing, routing, and response building
- `resolveRoute` prefix-matching effectively limits GET/DELETE to configured route prefixes
- Clean, readable config format
- Thorough error handling during socket setup with proper cleanup

---

## Top Recommendations (Priority Order)

1. **Fix POST upload path traversal** — Sanitize `httpRequest.fileName` to basename only, reject `..` segments
2. **Fix CGI path traversal** — Canonicalize and verify the script path stays within `_documentRoot`
3. **HTML-escape user-controlled output** — Auto-index entry names in `get.cpp:68-71`
4. **Remove dead code** — Exclude legacy `Server`/`ServerManager` and `tools.cpp` from build, then delete
5. **Use `unique_ptr`** for `VirtualHost` ownership in `WebServerCore`
6. **Add more MIME types** and default unknown to `application/octet-stream`
7. **Fix `operator<<`** to not write to `std::cout` directly
