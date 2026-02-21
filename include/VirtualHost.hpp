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
		// from config file
		int									_connectionTimeoutMs = 5000;
		uint16_t							_listenPort = 8080;
		uint64_t							_maxBodySize = 4096;
		std::string							_hostName, _bindAddress, _documentRoot;
		std::map<uint16_t, std::string>		_errorPageMap;
		std::map<std::string, RouteConfig>	_routeTable;

		// for sockets
		int						_socketFd, _socketOpt, _epollFd;

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

