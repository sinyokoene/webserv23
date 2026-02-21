#include "webserv.hpp"

void	handleDelete(VirtualHost& virtualHost, HttpRequest& httpRequest, HttpResponse& httpResponse)
{
	auto			routeEntry = resolveRoute(virtualHost, httpRequest.requestPath);
	const std::string	filePath = virtualHost._documentRoot + httpRequest.requestPath;

	if (routeEntry == virtualHost._routeTable.end())
	{
		httpResponse.statusCode = 404;
		return;
	}
	if (routeEntry->second.allowDelete == false)
	{
		httpResponse.statusCode = 405;
		return;
	}

	if (isDirectory(filePath) == true)
	{
		httpResponse.statusCode = 403;
		return;
	}
	if (std::remove(filePath.c_str()) < 0)
		httpResponse.statusCode = 404;
	else
		httpResponse.statusCode = 204;
}
