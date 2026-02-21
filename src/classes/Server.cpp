#include "Server.hpp"
#include "webserv.hpp"

std::map<uint16_t, std::string>	getErrorPages(const std::string& configContent)
{
	std::map<uint16_t, std::string>	errorPagesMap;
	const std::string				searchKey = "PAGE_";
	uint64_t						searchStart = 0;
	uint64_t						searchEnd;

	while (true)
	{
		searchStart = configContent.find(searchKey, searchStart);
		searchEnd = configContent.find_first_of(" \t", searchStart);
		if (searchStart == configContent.npos || searchEnd == configContent.npos)
			break;
		searchStart += searchKey.length();
		uint16_t code = (uint16_t)std::stoi(configContent.substr(searchStart, searchEnd - searchStart));
		searchStart = configContent.find_first_not_of(" \t", searchEnd);
		searchEnd = configContent.find_first_of("\n", searchStart);
		if (searchStart == configContent.npos || searchEnd == configContent.npos)
		{
			std::cerr << "couldn't find path to error page " << code << std::endl;
			throw Server::ParseError();
		}
		std::string pagePath = configContent.substr(searchStart, searchEnd - searchStart);
		if (errorPagesMap.find(code) != errorPagesMap.end())
		{
			std::cerr << "duplicate page for " << code << "\n";
			throw Server::ParseError();
		}
		errorPagesMap.insert(std::pair<uint16_t, std::string>(code, pagePath));
	}
	return errorPagesMap;
}

void	validateLocations(std::map<std::string, t_location>& locationMap)
{
	auto	rootLocation = locationMap.find("/");
	if (rootLocation == locationMap.end())
	{
		std::cerr << "Root \"/\" not found in config file\n";
		throw Server::ParseError();
	}

	if (rootLocation->second.index == "")
		rootLocation->second.index = "index.html";
	if (rootLocation->second.tempFile == "")
		rootLocation->second.tempFile = app_paths::DEFAULT_TEMP_FILE;

	for (auto& locationIter : locationMap)
	{
		if (locationIter.second.index == "")
			locationIter.second.index = rootLocation->second.index;
		if (locationIter.second.tempFile == "")
			locationIter.second.tempFile = rootLocation->second.tempFile;
	}
}

std::map<std::string, t_location>	getLocations(const std::string& configContent)
{
	std::map<std::string, t_location>	locationMap;
	uint64_t							blockStart = 0;
	uint64_t							blockEnd = 0;
	std::string							locationBlock;
	t_location							currentLocation;
	std::string							tempStr;

	while (true)
	{
		blockStart = configContent.find("LOCATION", blockEnd);
		blockEnd = configContent.find_first_of("]", blockEnd + 1);
		if ((blockStart == configContent.npos) ^ (blockEnd == configContent.npos))
		{
			std::cerr << "invalid location syntax\n";
			throw Server::ParseError();
		}
		if (blockStart == configContent.npos || blockEnd == configContent.npos)
		{
			break;
		}
		locationBlock = configContent.substr(blockStart, blockEnd - blockStart);

		tempStr = extractConfigValue(locationBlock, "PERMISSIONS", false);
		currentLocation.allowGet    = tempStr.find("get")    != tempStr.npos;
		currentLocation.allowPost   = tempStr.find("post")   != tempStr.npos;
		currentLocation.allowDelete = tempStr.find("delete") != tempStr.npos;
		currentLocation.autoIndex = extractConfigValue(locationBlock, "AUTO_INDEX", false) == "true";
		currentLocation.index     = extractConfigValue(locationBlock, "INDEX", false);
		currentLocation.tempFile  = extractConfigValue(locationBlock, "TEMP_FILE", false);
		tempStr = extractConfigValue(locationBlock, "LOCATION", true);
		if (tempStr == "")
		{
			std::cerr << "location is empty\n";
			throw Server::ParseError();
		}
		if (tempStr.length() > 1)
			tempStr += '/';

		if (locationMap.find(tempStr) != locationMap.end())
		{
			std::cerr << "duplicate location for " << tempStr << "\n";
			throw Server::ParseError();
		}
		locationMap.insert(std::pair<std::string, t_location>(tempStr, currentLocation));
	}
	validateLocations(locationMap);
	return locationMap;
}

void	throwError(const char* errorMessage, int socketDescriptor, addrinfo* addrInfo)
{
	if (addrInfo != nullptr)
		freeaddrinfo(addrInfo);
	perror(errorMessage);
	close(socketDescriptor);
	throw Server::InitError();
}

void	Server::setUp()
{
	addrinfo	socketHints{};
	addrinfo*	addrInfoResult;

	_opt = 1;
	socketHints.ai_flags = AI_PASSIVE;
	socketHints.ai_family = AF_INET;
	socketHints.ai_socktype = SOCK_STREAM;
	if (getaddrinfo(_host.c_str(), std::to_string(_port).c_str(), &socketHints, &addrInfoResult) < 0)
	{
		throwError("getaddrinfo", _serverFD, nullptr);
	}
	_serverFD = socket(addrInfoResult->ai_family, addrInfoResult->ai_socktype, addrInfoResult->ai_protocol);
	if (_serverFD < 0)
	{
		throwError("socket", _serverFD, addrInfoResult);
	}
	// allows server to immideatly be reused
	if (setsockopt(_serverFD, SOL_SOCKET, SO_REUSEADDR, &_opt, sizeof(int)))
	{
		throwError("setsockopt", _serverFD, addrInfoResult);
	}
	if (bind(_serverFD, addrInfoResult->ai_addr, addrInfoResult->ai_addrlen) < 0)
	{
		throwError("bind", _serverFD, addrInfoResult);
	}
	if (listen(_serverFD, 10) < 0)
	{
		throwError("listen", _serverFD, addrInfoResult);
	}
	freeaddrinfo(addrInfoResult);
}

template <typename T>
void	findVal(const std::string& configContent, const std::string& keyName, T& targetValue)
{
	try
	{
		std::string	valueStr = extractConfigValue(configContent, keyName, false);
		if (valueStr != "")
		{
			int64_t	numericValue = std::stol(valueStr);
			targetValue = (T)numericValue;
			if (numericValue < 0 || (int64_t)targetValue != numericValue)
			{
				throw std::bad_cast();
			}
		}
	}
	catch(const std::exception& e)
	{
		std::cerr << "Failed to parse " << keyName << " number: " << e.what() << '\n';
		throw Server::ParseError();
	}
}

Server::Server(const std::string& configContent)
{
	static	uint32_t instanceCount = 0;

	findVal(configContent, "PORT", _port);
	findVal(configContent, "BODY_SIZE", _bodySize);
	findVal(configContent, "TIME_OUT", _timeOut);
	_name = extractConfigValue(configContent, "SERVER", true);
	if (_name == "")
		_name = "server" + std::to_string(++instanceCount);
	_host = extractConfigValue(configContent, "HOST", true);
	_path = extractConfigValue(configContent, "PATH", false);
	if (_path == "")
		_path = ".";
	_errorTable = getErrorPages(configContent);
	_locations = getLocations(configContent);
	setUp();
}

Server::~Server(){}

const char* Server::ParseError::what() const throw()
{
	return ("Error parsing configuration file");
}

const char* Server::InitError::what() const throw()
{
	return ("Error while initialising connection");
}

std::ostream&	operator<<(std::ostream& outputStream, Server& serverInstance)
{
	outputStream << "\n\nSERVER\t" << serverInstance._name << "\n{" <<
	"\n\tHOST\t\t" << serverInstance._host <<
	"\n\tPORT\t\t" << serverInstance._port <<
	"\n\tBODY_SIZE\t" << serverInstance._bodySize <<
	"\n\tPATH\t\t" << serverInstance._path << 
	"\n\tTIME_OUT\t" << serverInstance._timeOut << "ms\n\n";

	for (auto iterator : serverInstance._locations)
	{
		outputStream << "\tLOCATION\t" << iterator.first <<
		"\n\t[\n\t\tINDEX\t\t" << iterator.second.index <<
		"\n\t\tAUTO_INDEX\t" << (iterator.second.autoIndex == true ? "true" : "false") <<
		"\n\t\tPERMISSIONS\t" << 
		(iterator.second.allowGet    == true ? "get " : "") <<
		(iterator.second.allowPost   == true ? "post " : "") <<
		(iterator.second.allowDelete == true ? "delete " : "") <<
		"\n\t\tTEMP_FILE\t" << iterator.second.tempFile << "\n\t]\n";
	}
	std::cout << "\n";
	for (auto iterator : serverInstance._errorTable)
	{
		outputStream << "\tPAGE_" << iterator.first << "\t" << iterator.second << "\n";
	}
	std::cout << "}\n";
	return outputStream;
}