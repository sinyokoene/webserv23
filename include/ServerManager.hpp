#pragma once

#ifdef __APPLE__
#define EPOLL_SHIM_DISABLE_WRAPPER_MACROS
#endif

#include <map>
#include <vector>
#include <string>
#include <cstdint>

#include <sys/epoll.h>

#include "Server.hpp"

struct	Request
{
	uint16_t	port;
	uint64_t	bodySize;
	std::string	path, fileName, contentType, method, protocol, serverName, body;
};

struct Response
{
	uint16_t	code = 200;
	uint64_t	bodySize = 0;
	std::string	body, fileType;
};

struct	Connection
{
	Server&			server;
	Request			request;
	Response		response;
};

class ServerManager
{
	private:
		int								_epFD;
		struct epoll_event				_ev, _events[10];
		std::string						_responseBody;
		std::map<int, Connection>		_clientTable;

		Server*		findServer(int eventIndex);
		bool		acceptNewConnection(Server& server);
		void		getHttpRequest(std::map<int, Connection>::iterator _clientTable);
		uint16_t	readRequest(std::string &str, Server &server, int clientFD);
		void		sendHttpResponse(std::map<int, Connection>::iterator client);

	public:
		std::vector<Server*>	_servers;

		ServerManager(const std::string& path);
		~ServerManager();

		void		run();

		class ConfFileNotFound : public std::exception
		{
			public:
				virtual const char *what() const throw();
		};
};

std::ostream&	operator<<(std::ostream& out, ServerManager& serverManager);
