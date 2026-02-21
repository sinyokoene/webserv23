#pragma once

#ifdef __APPLE__
#define EPOLL_SHIM_DISABLE_WRAPPER_MACROS
#endif

#include <map>
#include <vector>
#include <string>
#include <cstdint>

#include <sys/epoll.h>

#include "VirtualHost.hpp"

struct	HttpRequest
{
	uint16_t	port;
	uint64_t	contentLength;
	std::string	requestPath, fileName, contentType, httpMethod, httpProtocol, serverName, requestBody;
};

struct HttpResponse
{
	uint16_t	statusCode = 200;
	uint64_t	contentLength = 0;
	std::string	responseBody, mimeType, extraHeaders;
};

struct	ClientSession
{
	VirtualHost&	virtualHost;
	HttpRequest		httpRequest;
	HttpResponse	httpResponse;
	std::string		rawRequest;
	std::string		responseBuffer;
	size_t			sentBytes = 0;
	bool			responseReady = false;
	bool			headerParsed = false;
	int64_t			expectedBodySize = -1;
	size_t			headerEndPos = 0;
	int64_t			lastActivityAt = 0;
};

class WebServerCore
{
	private:
		int								_epollFd;
		struct epoll_event				_epollEvent, _eventBuffer[10];
		std::string						_responseData;
		std::map<int, ClientSession>	_sessionMap;

		VirtualHost*	locateVirtualHost(int eventIdx);
		bool			establishConnection(VirtualHost& virtualHost);
		void			processIncomingRequest(std::map<int, ClientSession>::iterator sessionIter);
		uint16_t		receiveRequestData(ClientSession& session, int clientFd);
		void			dispatchResponse(std::map<int, ClientSession>::iterator sessionIter);

	public:
		std::vector<VirtualHost*>	_virtualHosts;

		WebServerCore(const std::string& configPath);
		~WebServerCore();

		void		startEventLoop();

		class ConfigFileNotFound : public std::exception
		{
			public:
				virtual const char *what() const throw();
		};
};

std::ostream&	operator<<(std::ostream& out, WebServerCore& webServerCore);

