#pragma once

#include <map>
#include <string>
#include <cstdint>
#include <iostream>

union t_IP
{
	uint8_t		val[4];
	uint32_t	total;
};

struct t_location
{
	bool		allowGet, allowPost, allowDelete, autoIndex;
	std::string	index, tempFile;
};

class Server
{
	public:
		static constexpr uint64_t			DEFAULT_BODY_SIZE = 4096;
		static constexpr uint64_t			DEFAULT_TIMEOUT_MS = 5000;
		// from config file
		uint16_t							_port = 8080;
		uint64_t							_bodySize = DEFAULT_BODY_SIZE, _timeOut = DEFAULT_TIMEOUT_MS;
		std::string							_name, _host, _path;
		std::map<uint16_t, std::string>		_errorTable;
		std::map<std::string, t_location>	_locations;

		// for sockets
		int						_serverFD = -1, _opt = -1, _epfd = -1;

		Server(const std::string& config);
		~Server();
		void	setUp();
		class ParseError : public std::exception
		{
			public:
				virtual const char *what() const throw();
		};
		class InitError : public std::exception
		{
			public:
				virtual const char *what() const throw();
		};
};

std::ostream&	operator<<(std::ostream& out, Server& server);