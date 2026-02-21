#include "webserv.hpp"

namespace
{
std::string	trimSpaces(const std::string& input)
{
	const size_t start = input.find_first_not_of(" \t");
	if (start == std::string::npos)
		return "";
	const size_t end = input.find_last_not_of(" \t");
	return input.substr(start, end - start + 1);
}

void	parseRedirectRule(const std::string& configBlock, RouteConfig& routeConfig)
{
	const std::string redirectValue = trimSpaces(extractConfigValue(configBlock, "REDIRECT", false));
	if (redirectValue.empty())
		return;

	size_t splitPos = redirectValue.find_first_of(" \t");
	const std::string firstToken = trimSpaces(redirectValue.substr(0, splitPos));
	const std::string rest = splitPos == std::string::npos ? "" : trimSpaces(redirectValue.substr(splitPos + 1));
	try
	{
		if (rest.empty())
			throw std::invalid_argument("missing redirect target");
		const int code = std::stoi(firstToken);
		if (code < 300 || code > 399)
			throw std::out_of_range("redirect status out of range");
		routeConfig.redirectCode = static_cast<uint16_t>(code);
		routeConfig.redirectTarget = rest;
	}
	catch (const std::exception&)
	{
		routeConfig.redirectCode = 302;
		routeConfig.redirectTarget = redirectValue;
	}
	if (routeConfig.redirectTarget.empty())
	{
		std::cerr << "invalid REDIRECT value\n";
		throw VirtualHost::ConfigParseError();
	}
	routeConfig.hasRedirect = true;
}
} // namespace

std::map<uint16_t, std::string>	parseErrorPages(const std::string& configText)
{
	std::map<uint16_t, std::string>	errorPageMap;
	const std::string				searchKey = "PAGE_";
	uint64_t						startPos = 0;
	uint64_t						endPos;

	while (true)
	{
		startPos = configText.find(searchKey, startPos);
		endPos = configText.find_first_of(" \t", startPos);
		if (startPos == configText.npos || endPos == configText.npos)
			break;
		startPos += searchKey.length();
		uint16_t errorCode = (uint16_t)std::stoi(configText.substr(startPos, endPos - startPos));
		startPos = configText.find_first_not_of(" \t", endPos);
		endPos = configText.find_first_of("\n", startPos);
		if (startPos == configText.npos || endPos == configText.npos)
		{
			std::cerr << "couldn't find path to error page " << errorCode << std::endl;
			throw VirtualHost::ConfigParseError();
		}
		std::string pagePath = configText.substr(startPos, endPos - startPos);
		if (errorPageMap.find(errorCode) != errorPageMap.end())
		{
			std::cerr << "duplicate page for " << errorCode << "\n";
			throw VirtualHost::ConfigParseError();
		}
		errorPageMap.insert(std::pair<uint16_t, std::string>(errorCode, pagePath));
	}
	return errorPageMap;
}

void	validateRouteTable(std::map<std::string, RouteConfig>& routeTable)
{
	auto	rootRoute = routeTable.find("/");
	if (rootRoute == routeTable.end())
	{
		std::cerr << "Root \"/\" not found in config file\n";
		throw VirtualHost::ConfigParseError();
	}

	if (rootRoute->second.indexFile == "")
		rootRoute->second.indexFile = app_paths::LAYOUT_PAGE;
	if (rootRoute->second.tempFilePath == "")
		rootRoute->second.tempFilePath = app_paths::DEFAULT_TEMP_FILE;

	for (auto& routeEntry : routeTable)
	{
		if (routeEntry.second.indexFile == "")
			routeEntry.second.indexFile = rootRoute->second.indexFile;
		if (routeEntry.second.tempFilePath == "")
			routeEntry.second.tempFilePath = rootRoute->second.tempFilePath;
	}
}

std::map<std::string, RouteConfig>	parseRouteConfigs(const std::string& configText)
{
	std::map<std::string, RouteConfig>	routeTable;
	uint64_t							startPos = 0;
	uint64_t							endPos = 0;
	std::string							configBlock;
	RouteConfig							routeConfig;
	std::string							tempValue;

	while (true)
	{
		startPos = configText.find("LOCATION", endPos);
		endPos = configText.find_first_of("]", endPos + 1);
		if ((startPos == configText.npos) ^ (endPos == configText.npos))
		{
			std::cerr << "invalid location syntax\n";
			throw VirtualHost::ConfigParseError();
		}
		if (startPos == configText.npos || endPos == configText.npos)
		{
			break;
		}
		configBlock = configText.substr(startPos, endPos - startPos);
		routeConfig = RouteConfig();

		tempValue = extractConfigValue(configBlock, "PERMISSIONS", false);
		routeConfig.allowGet    = tempValue.find("get")    != tempValue.npos;
		routeConfig.allowPost   = tempValue.find("post")   != tempValue.npos;
		routeConfig.allowDelete = tempValue.find("delete") != tempValue.npos;
		routeConfig.autoIndex = extractConfigValue(configBlock, "AUTO_INDEX", false) == "true";
		routeConfig.indexFile     = extractConfigValue(configBlock, "INDEX", false);
		routeConfig.tempFilePath  = extractConfigValue(configBlock, "TEMP_FILE", false);
		parseRedirectRule(configBlock, routeConfig);
		tempValue = extractConfigValue(configBlock, "LOCATION", true);
		if (tempValue == "")
		{
			std::cerr << "location is empty\n";
			throw VirtualHost::ConfigParseError();
		}
		if (tempValue.length() > 1)
			tempValue += '/';

		if (routeTable.find(tempValue) != routeTable.end())
		{
			std::cerr << "duplicate location for " << tempValue << "\n";
			throw VirtualHost::ConfigParseError();
		}
		routeTable.insert(std::pair<std::string, RouteConfig>(tempValue, routeConfig));
	}
	validateRouteTable(routeTable);
	return routeTable;
}

void	reportSocketError(const char* errorMsg, int socketFd, addrinfo* addressInfo)
{
	if (addressInfo != nullptr)
		freeaddrinfo(addressInfo);
	perror(errorMsg);
	close(socketFd);
	throw VirtualHost::SocketInitError();
}

void	VirtualHost::initializeSocket()
{
	addrinfo	hints{};
	addrinfo*	addressInfo;

	_socketOpt = 1;
	hints.ai_flags = AI_PASSIVE;
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	if (getaddrinfo(_bindAddress.c_str(), std::to_string(_listenPort).c_str(), &hints, &addressInfo) < 0)
	{
		reportSocketError("getaddrinfo", _socketFd, nullptr);
	}
	_socketFd = socket(addressInfo->ai_family, addressInfo->ai_socktype, addressInfo->ai_protocol);
	if (_socketFd < 0)
	{
		reportSocketError("socket", _socketFd, addressInfo);
	}
	// allows server to immideatly be reused
	if (setsockopt(_socketFd, SOL_SOCKET, SO_REUSEADDR, &_socketOpt, sizeof(int)))
	{
		reportSocketError("setsockopt", _socketFd, addressInfo);
	}
	if (bind(_socketFd, addressInfo->ai_addr, addressInfo->ai_addrlen) < 0)
	{
		reportSocketError("bind", _socketFd, addressInfo);
	}
	if (listen(_socketFd, 10) < 0)
	{
		reportSocketError("listen", _socketFd, addressInfo);
	}
	freeaddrinfo(addressInfo);
}

template <typename T>
void	parseNumericValue(const std::string& configBlock, const std::string& keyword, T& outputValue)
{
	try
	{
		std::string	valueStr = extractConfigValue(configBlock, keyword, false);
		if (valueStr != "")
		{
			int64_t	parsedValue = std::stol(valueStr);
			outputValue = (T)parsedValue;
			if (parsedValue < 0 || (int64_t)outputValue != parsedValue)
			{
				throw std::bad_cast();
			}
		}
	}
	catch(const std::exception& e)
	{
		std::cerr << "Failed to parse " << keyword << " number: " << e.what() << '\n';
		throw VirtualHost::ConfigParseError();
	}
}

VirtualHost::VirtualHost(const std::string& configBlock)
{
	static	uint32_t hostCounter = 0;

	parseNumericValue(configBlock, "PORT", _listenPort);
	parseNumericValue(configBlock, "BODY_SIZE", _maxBodySize);
	parseNumericValue(configBlock, "TIME_OUT", _connectionTimeoutMs);
	parseNumericValue(configBlock, "CGI_TIME_OUT", _cgiTimeoutMs);
	_hostName = extractConfigValue(configBlock, "SERVER", true);
	if (_hostName == "")
		_hostName = "server" + std::to_string(++hostCounter);
	_bindAddress = extractConfigValue(configBlock, "HOST", true);
	_documentRoot = extractConfigValue(configBlock, "PATH", false);
	if (_documentRoot == "")
		_documentRoot = ".";
	_errorPageMap = parseErrorPages(configBlock);
	_routeTable = parseRouteConfigs(configBlock);
	initializeSocket();
}

VirtualHost::~VirtualHost(){}

const char* VirtualHost::ConfigParseError::what() const throw()
{
	return ("Error parsing configuration file");
}

const char* VirtualHost::SocketInitError::what() const throw()
{
	return ("Error while initialising connection");
}

std::ostream&	operator<<(std::ostream& out, VirtualHost& virtualHost)
{
	out << "\n\nSERVER\t" << virtualHost._hostName << "\n{" <<
	"\n\tHOST\t\t" << virtualHost._bindAddress <<
	"\n\tPORT\t\t" << virtualHost._listenPort <<
	"\n\tBODY_SIZE\t" << virtualHost._maxBodySize <<
	"\n\tPATH\t\t" << virtualHost._documentRoot << 
	"\n\tTIME_OUT\t" << virtualHost._connectionTimeoutMs << "ms" <<
	"\n\tCGI_TIME_OUT\t" << virtualHost._cgiTimeoutMs << "ms\n\n";

	for (auto routeEntry : virtualHost._routeTable)
	{
		out << "\tLOCATION\t" << routeEntry.first <<
		"\n\t[\n\t\tINDEX\t\t" << routeEntry.second.indexFile <<
		"\n\t\tAUTO_INDEX\t" << (routeEntry.second.autoIndex == true ? "true" : "false") <<
		"\n\t\tPERMISSIONS\t" << 
		(routeEntry.second.allowGet    == true ? "get " : "") <<
		(routeEntry.second.allowPost   == true ? "post " : "") <<
		(routeEntry.second.allowDelete == true ? "delete " : "") <<
		(routeEntry.second.hasRedirect == true ? ("\n\t\tREDIRECT\t" + std::to_string(routeEntry.second.redirectCode) + " " + routeEntry.second.redirectTarget) : "") <<
		"\n\t\tTEMP_FILE\t" << routeEntry.second.tempFilePath << "\n\t]\n";
	}
	std::cout << "\n";
	for (auto errorEntry : virtualHost._errorPageMap)
	{
		out << "\tPAGE_" << errorEntry.first << "\t" << errorEntry.second << "\n";
	}
	std::cout << "}\n";
	return out;
}

