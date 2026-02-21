# Webserv

## Project Overview 🚀
This project is a custom implementation of an HTTP server written in C++. It was designed to provide a deep understanding of the HTTP 1.1 protocol, socket programming, and asynchronous I/O operations. By building this from the ground up, we explored the intricacies of network communication and efficient resource management using standard C++ containers.

## Key Features ✨
- **HTTP 1.1 Compliance:** Handles standard HTTP requests and responses.
- **Method Support:** Fully functional `GET`, `POST`, and `DELETE` methods.
- **CGI Support:** Executes external scripts (Python, PHP, etc.) via the [Common Gateway Interface](https://en.wikipedia.org/wiki/Common_Gateway_Interface).
- **I/O Multiplexing:** Uses non-blocking sockets for high-performance connection handling.
- **Custom Configuration:** Flexible setup for hosting multiple domains and ports.
- **Directory Listing:** Optional auto-indexing for directories.
- **Error Handling:** Custom error pages for various HTTP status codes.

## Configuration ⚙️
The server behavior is defined by a configuration file. This allows you to set up virtual hosts, define routes, and manage permissions.

Here is a sample configuration that sets up a server on port 8080 with CGI and file upload capabilities:

```
ENABLE_LOG	true

SERVER	server1
{
	HOST			127.0.0.1
	PORT			8080
	PATH			./s1
	BODY_SIZE		4096
	TIME_OUT		1000 # milliseconds

	# Root Directory
	LOCATION		/
	[
		INDEX			index.html
		PERMISSIONS		get
		TEMP_FILE		tempfile
	]

	# Auto-indexing enabled
	LOCATION		/pages
	[
		AUTO_INDEX		true
		PERMISSIONS		get
	]

	# CGI Scripts
	LOCATION		/cgi-bin
	[
		INDEX			index.html
		PERMISSIONS		get
	]

	# Upload Directory
	LOCATION		/www
	[
		INDEX			index.html
		PERMISSIONS		get, post, delete
	]

	# Custom Error Pages
	LOCATION		/errorPages
	[
		PERMISSIONS		get
	]

	PAGE_201		errorPages/201.html
	PAGE_404		errorPages/404.html
	PAGE_413		errorPages/413.html
	PAGE_405		errorPages/405.html
}
```

## Logging System 📝
You can monitor server activity by setting `ENABLE_LOG` to `true` in your config file. The logger provides real-time feedback on:
- Server startup status (ports and addresses).
- Incoming HTTP requests and methods.
- Response status codes.
- Errors and warnings (e.g., timeouts, missing files).

**Sample Log Output:**
```
Started at 2025-01-14 15:22:10
Running servers:
- server1 @ localhost:8080
- server2 @ 127.0.0.2:9090

server1 @ 15:22:19:
  client[3]
  GET /
  -> 200 OK

server1 @ 15:22:25:
  client[3]
  GET /cgi-bin/infinite.py
  ERROR: script timed out
  WARNING: using generic errorpage
  -> 500 Internal Server Error

server2 @ 15:23:04:
  client[3]
  GET /
  WARNING: using generic errorpage
  -> 404 Not Found
```
