#include "webserv.hpp"

namespace
{
std::string	nextTempPath(const std::string& basePath)
{
	namespace fs = std::filesystem;

	for (uint16_t i = 1; i != 0; i++)
	{
		const std::string candidate = basePath + "_" + std::to_string(i);
		if (fs::exists(candidate) == false)
			return candidate;
	}
	return "";
}

std::string	resolvePostPath(VirtualHost& virtualHost, HttpRequest& httpRequest,
							std::map<std::string, RouteConfig>::iterator routeEntry)
{
	std::string absolutePath = virtualHost._documentRoot + httpRequest.requestPath + httpRequest.fileName;

	if (isDirectory(absolutePath) == false)
		return absolutePath;

	logger::addMsg("made tempfile", false);
	const std::string base = virtualHost._documentRoot + routeEntry->first + routeEntry->second.tempFilePath;
	return nextTempPath(base);
}
} // namespace

void	handlePost(VirtualHost& virtualHost, HttpRequest& httpRequest, HttpResponse& httpResponse)
{
	auto			routeEntry = resolveRoute(virtualHost, httpRequest.requestPath);
	std::string		filePath;

	if (routeEntry == virtualHost._routeTable.end())
	{
		httpResponse.statusCode = 404;
		return;
	}
	if (routeEntry->second.allowPost == false)
	{
		httpResponse.statusCode = 405;
		return;
	}

	filePath = resolvePostPath(virtualHost, httpRequest, routeEntry);
	if (filePath.empty())
	{
		logger::addMsg("no available tempfile slots");
		httpResponse.statusCode = 500;
		return;
	}
	std::ofstream	outfile(filePath);
	if (outfile.is_open() == false)
	{
		logger::addMsg("couldn't open " + filePath);
		httpResponse.statusCode = 404;
		return;
	}
	outfile << httpRequest.requestBody;
	outfile.close();
	httpResponse.statusCode = 201;
}
