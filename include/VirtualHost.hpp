#pragma once

#include <map>
#include <string>
#include <cstdint>
#include <iostream>

union IPAddress
{
	uint8_t		octets[4];
	uint32_t	combined;
};

struct RouteConfig
{
	bool		allowGet, allowPost, allowDelete, autoIndex;
	bool		hasRedirect = false;
	uint16_t	redirectCode = 302;
	std::string	indexFile, tempFilePath, redirectTarget;
};

class VirtualHost
{
	public:
		static constexpr int				DEFAULT_CONNECTION_TIMEOUT_MS = 5000;
		static constexpr int				DEFAULT_CGI_TIMEOUT_MS = 10000;
		static constexpr uint64_t			DEFAULT_MAX_BODY_SIZE = 4096;
		// from config file
		int									_connectionTimeoutMs = DEFAULT_CONNECTION_TIMEOUT_MS;
		int									_cgiTimeoutMs = DEFAULT_CGI_TIMEOUT_MS;
		uint16_t							_listenPort = 8080;
		uint64_t							_maxBodySize = DEFAULT_MAX_BODY_SIZE;
		std::string							_hostName, _bindAddress, _documentRoot;
		std::map<uint16_t, std::string>		_errorPageMap;
		std::map<std::string, RouteConfig>	_routeTable;

		// for sockets
		int						_socketFd = -1, _socketOpt = -1, _epollFd = -1;

		VirtualHost(const std::string& configBlock);
		~VirtualHost();
		void	initializeSocket();
		class ConfigParseError : public std::exception
		{
			public:
				virtual const char *what() const throw();
		};
		class SocketInitError : public std::exception
		{
			public:
				virtual const char *what() const throw();
		};
};

std::ostream&	operator<<(std::ostream& out, VirtualHost& virtualHost);

