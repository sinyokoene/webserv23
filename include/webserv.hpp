#pragma once

#ifdef __APPLE__
// Prevent epoll-shim from replacing close/read/write with macros; we only need
// the epoll symbols, not the libc wrappers, to avoid clashes with iostreams.
#define EPOLL_SHIM_DISABLE_WRAPPER_MACROS
#endif

#include <string>
#include <chrono>
#include <fstream>
#include <cstring>
#include <csignal>
#include <sstream>
#include <iostream>
#include <filesystem>

#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>

#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <arpa/inet.h>
#include <netinet/in.h>

#include "VirtualHost.hpp"
#include "WebServerCore.hpp"

static constexpr bool	debug = false;

namespace app_paths
{
	inline constexpr const char* LAYOUT_PAGE     = "layoutPage.html";
	inline constexpr const char* DEFAULT_CONFIG  = "conf/default.conf";
	inline constexpr const char* DEFAULT_TEMP_FILE = "tempfile";
}

namespace logger
{
	inline std::ofstream	FD;
	inline std::string		errorMsg;
	inline bool				enable;

	void	setEnable(bool value);
	void	open(std::string name, WebServerCore& webServerCore);
	void	addMsg(std::string msg, bool error = true);
	void	write(VirtualHost& virtualHost, HttpRequest& httpRequest, HttpResponse& httpResponse, int clientFd);
	void	close();
}

// time
int64_t		getMilliseconds();
bool		checkTimeoutExpired(int64_t durationMs = 0);

// tools
void			setFdFlag(int fd, int flag);
bool			isDirectory(std::string filePath);
std::string		extractSubstring(std::string inputStr, std::string startDelim, std::string endDelim, uint64_t searchOffset = 0);
std::string		formatTimestamp(std::string formatPattern);
std::string		extractConfigValue(const std::string& configText, const std::string& keyName, bool throwOnMissing);
void			layoutPage(HttpResponse& httpResponse, std::string title, std::string text);

// lookup
std::string		getFullFileType(std::string fileType);
std::string 	getErrorCode(uint16_t code);

// run
void		handleGet(VirtualHost& virtualHost, HttpRequest& httpRequest, HttpResponse& httpResponse);
void		handlePost(VirtualHost& virtualHost, HttpRequest& httpRequest, HttpResponse& httpResponse);
void		handleDelete(VirtualHost& virtualHost, HttpRequest& httpRequest, HttpResponse& httpResponse);
void		handleCGI(VirtualHost& virtualHost, HttpRequest& httpRequest, HttpResponse& httpResponse);
void		getErrorPage(VirtualHost& virtualHost, HttpResponse& httpResponse);
void		parseRequest(std::string input, VirtualHost& virtualHost, HttpRequest& httpRequest, HttpResponse& httpResponse);

std::map<std::string, RouteConfig>::iterator	resolveRoute(VirtualHost& virtualHost, std::string fullPath);
