#include "webserv.hpp"

void	setFdFlag(int fd, int flag)
{
	int currentFlags = fcntl(fd, F_GETFL, 0);
	if (currentFlags == -1 || fcntl(fd, F_SETFL, currentFlags | flag) == -1)
	{
		perror("setFdFlag failed");
		return;
	}
}

bool	isDirectory(std::string filePath)
{
	struct stat		pathInfo;

	// Get information about the path
	if (stat(filePath.c_str(), &pathInfo) == -1)
	{
		return false;
	}
	// Check if it's a directory
	return S_ISDIR(pathInfo.st_mode);
}

std::map<std::string, RouteConfig>::iterator	resolveRoute(VirtualHost& virtualHost, std::string fullPath)
{
	auto exactMatch = virtualHost._routeTable.find(fullPath);
	if (exactMatch != virtualHost._routeTable.end())
		return exactMatch;
	if (fullPath.empty() == false && fullPath.back() != '/')
	{
		exactMatch = virtualHost._routeTable.find(fullPath + "/");
		if (exactMatch != virtualHost._routeTable.end())
			return exactMatch;
	}
	uint64_t	lastSlashPos = fullPath.find_last_of("/");
	std::string	routePath = fullPath.substr(0, lastSlashPos + 1);

	return virtualHost._routeTable.find(routePath);
}

std::string	extractSubstring(std::string inputStr, std::string startDelim, std::string endDelim, uint64_t searchOffset)
{
	uint64_t	foundPos = inputStr.find(startDelim);
	if (foundPos >= inputStr.npos)
		return "";
	searchOffset += foundPos;

	u_int64_t	endIndex = inputStr.find(endDelim, searchOffset);
	if (endIndex == inputStr.npos)
		return "";
	
	return inputStr.substr(searchOffset, endIndex - searchOffset);
}

std::string	formatTimestamp(std::string formatPattern)
{
	auto				currentTime = std::chrono::system_clock::now();
	std::time_t			timeValue = std::chrono::system_clock::to_time_t(currentTime);
	std::tm				localTimeInfo = *std::localtime(&timeValue);
	std::ostringstream	formattedStream;

	formattedStream << std::put_time(&localTimeInfo, formatPattern.c_str());
	return formattedStream.str();
}

int64_t	getMilliseconds()
{
	using namespace std::chrono;

	return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

// Set by calling with `durationMs` set to the desired duration
// Will otherwise return true when time has passed
bool	checkTimeoutExpired(int64_t durationMs)
{
	static int64_t targetTimeMs;
	int64_t currentTimeMs = getMilliseconds();

	if (durationMs != 0)
	{
		targetTimeMs = currentTimeMs + durationMs;
		return false;
	}
	return targetTimeMs <= currentTimeMs;
}

std::string	extractConfigValue(const std::string& configText, const std::string& keyName, bool throwOnMissing)
{
	uint64_t startPos = configText.find(keyName);
	uint64_t endPos = configText.find_first_of("\n#", startPos);
	if ((startPos == configText.npos || endPos == configText.npos))
	{
		if (throwOnMissing == true)
		{
			std::cerr << "Couldn't find " << keyName << std::endl;
			throw VirtualHost::ConfigParseError();
		}
		return "";
	}
	startPos += keyName.length();
	startPos = configText.find_first_not_of(" \t", startPos);
	while (endPos > 0 && (configText[endPos - 1] == ' ' || configText[endPos - 1] == '\t'))
		endPos--;
	if (startPos > endPos)
	{
		std::cerr << "extractConfigValue: start > end\ncheck if " << keyName << " has a value\n";
		throw VirtualHost::ConfigParseError();
	}
	return configText.substr(startPos, endPos - startPos);
}

