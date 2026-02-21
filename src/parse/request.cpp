#include "webserv.hpp"

namespace
{
const char*	kHeaderDelimiter = "\r\n\r\n";

bool	readPlainBody(HttpRequest& httpRequest, const std::string& input)
{
	const uint64_t headerPos = input.find(kHeaderDelimiter);
	if (headerPos == input.npos)
		return false;
	httpRequest.requestBody = input.substr(headerPos + strlen(kHeaderDelimiter));
	return true;
}

bool	readMultipartBody(HttpRequest& httpRequest, const std::string& input,
						 const std::string& boundary, HttpResponse& httpResponse)
{
	const size_t firstBoundary  = input.find(boundary);
	const size_t secondBoundary = (firstBoundary == std::string::npos)
		? std::string::npos
		: input.find(boundary, firstBoundary + boundary.length());

	if (firstBoundary == std::string::npos || secondBoundary == std::string::npos)
	{
		logger::addMsg("couldn't find boundary");
		httpResponse.statusCode = 400;
		return false;
	}

	httpRequest.requestBody = input.substr(secondBoundary + boundary.length());
	httpRequest.fileName    = extractSubstring(httpRequest.requestBody, "filename=\"",    "\"", strlen("filename=\""));
	httpRequest.contentType = extractSubstring(httpRequest.requestBody, "Content-Type: ", "\r\n", strlen("Content-Type: "));
	httpRequest.requestBody = extractSubstring(httpRequest.requestBody, kHeaderDelimiter, "--" + boundary, strlen(kHeaderDelimiter));
	return true;
}

void	parseBody(HttpRequest& httpRequest, HttpResponse& httpResponse, const std::string& input)
{
	const std::string boundary = extractSubstring(input, "boundary=", "\r\n", strlen("boundary="));

	if (boundary.empty())
	{
		if (readPlainBody(httpRequest, input) == false)
		{
			logger::addMsg("failed to read body");
			httpResponse.statusCode = 400;
		}
		return;
	}
	if (readMultipartBody(httpRequest, input, boundary, httpResponse) == false && httpResponse.statusCode == 200)
		httpResponse.statusCode = 400;
}

uint64_t	parseFileType(const std::string& path, std::string& outExtension)
{
	uint64_t dotPos = path.find('.');
	if (dotPos == path.npos)
		outExtension.clear();
	else
		outExtension = path.substr(dotPos);
	return dotPos;
}
} // namespace

void	parseRequest(std::string input, VirtualHost& virtualHost, HttpRequest& httpRequest, HttpResponse& httpResponse)
{
	if (input.empty())
	{
		httpResponse.statusCode = 400;
		return;
	}

	httpRequest.httpMethod = extractSubstring(input, "", " ");
	httpRequest.requestPath   = extractSubstring(input, "/", " ");
	if (httpRequest.requestPath.length() > 1 && httpRequest.requestPath.back() != '/' && isDirectory(virtualHost._documentRoot + httpRequest.requestPath))
		httpRequest.requestPath += "/";

	parseFileType(httpRequest.requestPath, httpResponse.mimeType);

	httpRequest.httpProtocol   = extractSubstring(input, "HTTP", "\r\n");
	httpRequest.serverName = extractSubstring(input, "Host:", ":", strlen("Host: "));

	std::string temp = extractSubstring(input, httpRequest.serverName, "\r\n",
								   strlen(httpRequest.serverName.c_str()) + 1);
	try {
		httpRequest.port = std::stoi(temp);
	} catch (std::exception& e) {
		logger::addMsg("no valid port in request:");
		httpRequest.port = 0;
	}

	temp = extractSubstring(input, "Content-Length:", "\r\n", strlen("Content-Length: "));
	try {
		httpRequest.contentLength = std::stoi(temp);
	} catch (std::exception& e) {
		httpRequest.contentLength = 0;
	}

	if (httpRequest.httpMethod != "GET" && httpRequest.httpMethod != "POST" && httpRequest.httpMethod != "DELETE")
		httpResponse.statusCode = 501;
	if (httpRequest.httpProtocol != "HTTP/1.1")
		httpResponse.statusCode = 505;
	if (httpResponse.statusCode == 200 && httpRequest.contentLength > 0)
		parseBody(httpRequest, httpResponse, input);
}
