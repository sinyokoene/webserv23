#include "webserv.hpp"

WebServerCore::WebServerCore(const std::string& configPath)
{
	try
	{
		std::ifstream configFile(configPath, std::ios::in);
		if (configFile.is_open() == false)
		{
			std::cerr << "failed to open file: " << configPath << std::endl;
			throw ConfigFileNotFound();
		}
		std::string configContent;
		std::getline(configFile, configContent, '\0');
		if (configContent.empty() == true)
		{
			std::cerr << "failed to open file: " << configPath << std::endl;
			throw ConfigFileNotFound();
		}
		
		uint64_t blockStart = 0;
		uint64_t blockEnd = 0;

		logger::setEnable(extractConfigValue(configContent, "ENABLE_LOG", false) == "true");
		while (true)
		{
			blockStart = blockEnd;
			blockEnd = configContent.find('}', blockEnd);
			if (blockEnd == configContent.npos)
				break;
			blockEnd += 1;
			VirtualHost*	virtualHost = new VirtualHost(configContent.substr(blockStart, blockEnd - blockStart));
			_virtualHosts.push_back(virtualHost);
		}
		if (_virtualHosts.size() == 0)
		{
			std::cerr << "No servers found\n";
			throw VirtualHost::ConfigParseError();
		}
		_epollFd = epoll_create1(0);
		for (auto hostIter : _virtualHosts)
		{
			_epollEvent.events = EPOLLIN;
			_epollEvent.data.fd = hostIter->_socketFd;
			epoll_ctl(_epollFd, EPOLL_CTL_ADD, hostIter->_socketFd, &_epollEvent);
		}
		logger::open("log.txt", *this);
	}
	catch(const std::exception& e)
	{
		for (auto hostIter : _virtualHosts)
		{
			delete hostIter;
		}
		std::cerr << e.what() << "\n";
		throw std::exception();
	}
}

WebServerCore::~WebServerCore()
{
	for (auto hostIter : _virtualHosts)
	{
		if (hostIter->_socketFd != -1)
		{
			if (setsockopt(hostIter->_socketFd, SOL_SOCKET, SO_REUSEADDR, &hostIter->_socketOpt, sizeof(hostIter->_socketOpt)) < 0)
			{
				perror("setsockopt failed");
			}
			close(hostIter->_socketFd);
		}
	}
	for (auto hostIter : _virtualHosts)
	{
		delete hostIter;
	}
	logger::close();
}

const char* WebServerCore::ConfigFileNotFound::what() const throw()
{
	return ("Error opening/reading");
}

std::ostream&	operator<<(std::ostream& out, WebServerCore& webServerCore)
{
	for (VirtualHost* hostIter : webServerCore._virtualHosts)
	{
		out << *hostIter;
	}
	return out;
}

