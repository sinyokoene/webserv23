#include "webserv.hpp"

void	layoutPage(HttpResponse& httpResponse, std::string title, std::string text)
{
	std::ifstream		infile(app_paths::LAYOUT_PAGE, std::ios::in);
	std::stringstream	buffer;
	uint64_t			startPos;

	if (infile.is_open() == false)
	{
		httpResponse.statusCode = 500;
		return;
	}
	buffer << infile.rdbuf();
	httpResponse.responseBody = buffer.str();
	
	startPos = httpResponse.responseBody.find("REPLACE1");
	if (startPos != httpResponse.responseBody.npos)
	{
		httpResponse.responseBody.replace(startPos, strlen("REPLACE1"), title);
	}
	startPos = httpResponse.responseBody.find("REPLACE2");
	if (startPos != httpResponse.responseBody.npos)
	{
		httpResponse.responseBody.replace(startPos, strlen("REPLACE2"), text);
	}

	httpResponse.contentLength = httpResponse.responseBody.length();
	httpResponse.mimeType = ".html";
}

void	getErrorPage(VirtualHost& virtualHost, HttpResponse& httpResponse)
{
	auto				pageEntry = virtualHost._errorPageMap.find(httpResponse.statusCode);
	std::string			filePath;
	std::ifstream		infile;
	std::stringstream	buffer;

	if (pageEntry == virtualHost._errorPageMap.end())
	{
		logger::addMsg("using generic errorpage", false);
		std::string	errorCode =  getErrorCode(httpResponse.statusCode);
		layoutPage(httpResponse, errorCode, "<h1>" + errorCode + "</h1>");
		return;
	}
	filePath = virtualHost._documentRoot + "/" +  pageEntry->second;
	infile.open(filePath, std::ios::in);
	if (infile.is_open() == false)
	{
		httpResponse.statusCode = 500;
		return;
	}
	buffer << infile.rdbuf();
	httpResponse.responseBody = buffer.str();
	httpResponse.contentLength = httpResponse.responseBody.length();
	httpResponse.mimeType = ".html";
}

void	autoIndex(VirtualHost& virtualHost, HttpRequest& httpRequest, HttpResponse& httpResponse)
{
	namespace fs = std::filesystem;

	logger::addMsg("using autoindex", false);
	std::string	directoryList = "<h1>directory: " + httpRequest.requestPath + "</h1>\r\n";
	fs::path	listedDir(virtualHost._documentRoot + httpRequest.requestPath);
	for (const auto& entry : fs::directory_iterator(listedDir))
	{
		std::string entryName(entry.path().c_str());
		std::string linkPath(entryName.c_str() + virtualHost._documentRoot.length());
		std::string displayName(entryName.c_str() + virtualHost._documentRoot.length() + httpRequest.requestPath.length());
		directoryList += "<a href=\"" + linkPath + "\"><h2>" + displayName + "</h2></a>\r\n";
	}
	layoutPage(httpResponse, "Directory Listing", directoryList);
}

void	handleGet(VirtualHost& virtualHost, HttpRequest& httpRequest, HttpResponse& httpResponse)
{
	auto			routeEntry = resolveRoute(virtualHost, httpRequest.requestPath);
	std::string		filePath;
	std::ifstream	infile;
	bool			useLayoutPage = false;

	if (routeEntry == virtualHost._routeTable.end())
	{
		httpResponse.statusCode = 403;
	}
	else if (routeEntry->second.allowGet == false)
	{
		httpResponse.statusCode = 405;
	}
	if (httpResponse.statusCode == 200) // get path and open file
	{
		if (routeEntry->first.length() == httpRequest.requestPath.length())
		{
			useLayoutPage = true;
			filePath = virtualHost._documentRoot + routeEntry->first + routeEntry->second.indexFile;
		}
		else
		{
			filePath = virtualHost._documentRoot + httpRequest.requestPath;
		}
		infile.open(filePath, std::ios::binary | std::ios::in);
		if (infile.is_open() == false)
		{
			if (useLayoutPage == true && routeEntry->second.autoIndex == true)
				return autoIndex(virtualHost, httpRequest, httpResponse);
			httpResponse.statusCode = 404;
		}	
	}

	if (httpResponse.statusCode == 200) // read file if file is found
	{
		infile.seekg(0, std::ios::end);
		httpResponse.contentLength = infile.tellg();
		std::string	fileContent(httpResponse.contentLength, '\0');
		infile.seekg(0, std::ios::beg);
		infile.read(&fileContent[0], httpResponse.contentLength);
		httpResponse.responseBody = fileContent;
		infile.close();
		httpResponse.contentLength = httpResponse.responseBody.length();
	}
}
