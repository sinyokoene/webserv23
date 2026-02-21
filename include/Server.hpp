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
		// from config file
		uint16_t							_port = 8080;
		uint64_t							_bodySize = 4096, _timeOut = 5000;
		std::string							_name, _host, _path;
		std::map<uint16_t, std::string>		_errorTable;
		std::map<std::string, t_location>	_locations;

		// for sockets
		int						_serverFD, _opt, _epfd;

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