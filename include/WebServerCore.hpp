#pragma once

#ifdef __APPLE__
#define EPOLL_SHIM_DISABLE_WRAPPER_MACROS
#endif

#include <map>
#include <vector>
#include <string>
#include <cstdint>
#include <sys/types.h>

#include <sys/epoll.h>

#include "VirtualHost.hpp"

struct	HttpRequest
{
	uint16_t	port = 0;
	uint64_t	contentLength = 0;
	std::string	requestPath, queryString, fileName, contentType, httpMethod, httpProtocol, serverName, requestBody;
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
	bool			chunkedTransfer = false;
	int64_t			expectedBodySize = -1;
	size_t			headerEndPos = 0;
	int64_t			lastActivityAt = 0;
	bool			cgiActive = false;
	pid_t			cgiPid = -1;
	int				cgiInputPipeFd = -1;
	int				cgiOutputPipeFd = -1;
	size_t			cgiWriteOffset = 0;
	std::string		cgiOutputBuffer;
	int64_t			cgiStartTime = 0;

	ClientSession(VirtualHost& hostRef) : virtualHost(hostRef) {}
};

class WebServerCore
{
	private:
		int								_epollFd = -1;
		struct epoll_event				_epollEvent, _eventBuffer[10];
		std::string						_responseData;
		std::map<int, ClientSession>	_sessionMap;
		std::map<int, int>				_cgiPipeToClient;

		VirtualHost*	locateVirtualHost(int eventIdx);
		bool			establishConnection(VirtualHost& virtualHost);
		void			cleanupSession(int clientFd);
		void			handleCgiPipeEvent(int pipeFd, uint32_t events);
		bool			beginCgiForSession(ClientSession& session, int clientFd, HttpResponse& httpResponse);
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

