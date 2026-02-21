# Webserv

## Project Overview
A lightweight HTTP/1.1 web server written in C++20. Built from scratch using `epoll` for I/O multiplexing, it supports virtual hosting, CGI execution, file uploads, and custom error pages — all driven by a simple configuration file.

## Features
- **HTTP/1.1** — `GET`, `POST`, and `DELETE` methods
- **Virtual Hosts** — run multiple independent sites from a single process
- **CGI** — execute Python, PHP, and other scripts via the [Common Gateway Interface](https://en.wikipedia.org/wiki/Common_Gateway_Interface)
- **Non-blocking I/O** — `epoll`-based event loop for efficient connection handling
- **File Uploads** — configurable upload location and max body size
- **Directory Listing** — optional auto-indexing per route
- **Redirects** — per-route HTTP redirects (e.g. `302`)
- **Custom Error Pages** — per-server pages for `201`, `400`, `403`, `404`, `405`, `413`, `500`, `501`
- **Logging** — timestamped request/response logging to file
- **macOS Support** — builds on macOS via Homebrew `epoll-shim`

## Building

```bash
make        # build
make re     # rebuild from scratch
make clean  # remove object files
make fclean # remove object files and binary
```

The binary is compiled as `webserv` with `-std=c++20 -Wall -Wextra -Werror`.

## Usage

```bash
./webserv                    # uses default config: conf/default.conf
./webserv path/to/config     # uses a custom config file
```

## Project Structure

```
├── conf/                    # configuration files
│   └── default.conf
├── include/                 # header files
│   ├── webserv.hpp          # central header, constants, logger, function declarations
│   ├── WebServerCore.hpp    # epoll event loop
│   ├── ServerManager.hpp    # manages multiple Server instances
│   ├── Server.hpp           # single listening socket
│   └── VirtualHost.hpp      # per-host config, request/response structs
├── src/
│   ├── main.cpp
│   ├── classes/             # Server, ServerManager, VirtualHost, WebServerCore
│   ├── execute/             # request handlers: get, post, delete, cgi, run
│   ├── parse/               # HTTP request parser
│   └── tools/               # logging, MIME lookup, string utilities
├── server_1/                # document root for webserv-alpha (port 8080)
│   ├── cgi-bin/             # CGI scripts (Python, PHP)
│   ├── errorPages/          # custom error pages
│   └── pages/               # extra static pages
├── server_2/                # document root for webserv-beta  (port 9090)
├── layoutPage.html          # HTML template used for generated pages
├── test_webserver.py        # integration test suite
└── Makefile
```

## Configuration

Server behavior is defined in a plain-text config file. The default configuration (`conf/default.conf`) sets up two virtual hosts:

| Server | Host | Port | Document Root |
|---|---|---|---|
| `webserv-alpha` | `127.0.0.1` | `8080` | `./server_1` |
| `webserv-beta` | `127.0.0.1` | `9090` | `./server_2` |

### Config syntax

```
ENABLE_LOG  true

SERVER  webserv-alpha
{
    HOST        127.0.0.1
    PORT        8080
    PATH        ./server_1
    BODY_SIZE   4096
    TIME_OUT    1000              # milliseconds

    LOCATION    /
    [
        INDEX       index.html
        PERMISSIONS get
        TEMP_FILE   uploadedFile
    ]

    LOCATION    /pages
    [
        AUTO_INDEX  true
        PERMISSIONS get
    ]

    LOCATION    /cgi-bin
    [
        PERMISSIONS get
    ]

    LOCATION    /www
    [
        PERMISSIONS get, post, delete
    ]

    LOCATION    /redirect
    [
        PERMISSIONS get
        REDIRECT    302 /dashboard.html
    ]

    LOCATION    /errorPages
    [
        PERMISSIONS get
    ]

    PAGE_201    errorPages/201.html
    PAGE_400    errorPages/400.html
    PAGE_403    errorPages/403.html
    PAGE_404    errorPages/404.html
    PAGE_405    errorPages/405.html
    PAGE_413    errorPages/413.html
    PAGE_500    errorPages/500.html
    PAGE_501    errorPages/501.html
}
```

### Config directives

| Directive | Scope | Description |
|---|---|---|
| `ENABLE_LOG` | global | Enable or disable request logging (`true` / `false`) |
| `SERVER` | global | Declare a named virtual host block |
| `HOST` | server | Bind address |
| `PORT` | server | Listening port |
| `PATH` | server | Document root directory |
| `BODY_SIZE` | server | Maximum request body size in bytes |
| `TIME_OUT` | server | CGI / connection timeout in milliseconds |
| `LOCATION` | server | Define a route |
| `INDEX` | location | Default file to serve |
| `PERMISSIONS` | location | Allowed methods (`get`, `post`, `delete`) |
| `AUTO_INDEX` | location | Enable directory listing (`true` / `false`) |
| `TEMP_FILE` | location | Filename prefix for uploaded files |
| `REDIRECT` | location | HTTP redirect: `<code> <target>` |
| `PAGE_*` | server | Custom error page path (e.g. `PAGE_404`) |

## Logging

Set `ENABLE_LOG true` in your config to write a timestamped log to `log.txt`. Example output:

```
Started at 2025-01-14 15:22:10
Running servers:
- webserv-alpha @ 127.0.0.1:8080
- webserv-beta  @ 127.0.0.1:9090

webserv-alpha @ 15:22:19:
  client[3]
  GET /
  -> 200 OK

webserv-alpha @ 15:22:25:
  client[3]
  GET /cgi-bin/infinite.py
  ERROR: script timed out
  WARNING: using generic errorpage
  -> 500 Internal Server Error
```
