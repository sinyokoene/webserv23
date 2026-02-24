#include "webserv.hpp"

namespace
{
const char*	HEADER_DELIMITER = "\r\n\r\n";

bool	readPlainBody(HttpRequest& httpRequest, const std::string& input)
{
	const uint64_t headerPos = input.find(HEADER_DELIMITER);
	if (headerPos == input.npos)
		return false;
	httpRequest.requestBody = input.substr(headerPos + strlen(HEADER_DELIMITER));
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
	httpRequest.requestBody = extractSubstring(httpRequest.requestBody, HEADER_DELIMITER, "--" + boundary, strlen(HEADER_DELIMITER));
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

	const size_t lineEnd = input.find("\r\n");
	if (lineEnd == std::string::npos)
	{
		httpResponse.statusCode = 400;
		return;
	}
	const std::string requestLine = input.substr(0, lineEnd);
	std::istringstream lineStream(requestLine);
	std::string requestTarget;
	lineStream >> httpRequest.httpMethod >> requestTarget >> httpRequest.httpProtocol;
	if (httpRequest.httpMethod.empty() || requestTarget.empty())
	{
		httpResponse.statusCode = 400;
		return;
	}
	if (httpRequest.httpProtocol.empty())
	{
		// HTTP/0.9 style request line.
		httpRequest.httpProtocol = "HTTP/0.9";
	}
	if (requestTarget[0] != '/')
		requestTarget = "/" + requestTarget;
	const size_t queryPos = requestTarget.find('?');
	httpRequest.requestPath = requestTarget.substr(0, queryPos);
	httpRequest.queryString = queryPos == std::string::npos ? "" : requestTarget.substr(queryPos + 1);
	if (httpRequest.requestPath.length() > 1
		&& httpRequest.requestPath.back() != '/'
		&& isDirectory(virtualHost._documentRoot + httpRequest.requestPath))
	{
		httpRequest.requestPath += "/";
	}
	parseFileType(httpRequest.requestPath, httpResponse.mimeType);

	httpRequest.serverName = extractSubstring(input, "Host:", ":", strlen("Host: "));
	std::string temp = extractSubstring(input, httpRequest.serverName, "\r\n",
		strlen(httpRequest.serverName.c_str()) + 1);
	try
	{
		httpRequest.port = static_cast<uint16_t>(std::stoi(temp));
	}
	catch (const std::exception&)
	{
		httpRequest.port = virtualHost._listenPort;
	}

	temp = extractSubstring(input, "Content-Length:", "\r\n", strlen("Content-Length: "));
	try
	{
		httpRequest.contentLength = static_cast<uint64_t>(std::stoull(temp));
	}
	catch (const std::exception&)
	{
		httpRequest.contentLength = 0;
	}

	if (httpRequest.httpMethod != "GET" && httpRequest.httpMethod != "POST" && httpRequest.httpMethod != "DELETE")
		httpResponse.statusCode = 501;
	if (httpRequest.httpProtocol != "HTTP/1.1" && httpRequest.httpProtocol != "HTTP/1.0" && httpRequest.httpProtocol != "HTTP/0.9")
		httpResponse.statusCode = 505;
	if (httpRequest.httpProtocol == "HTTP/0.9" && httpRequest.httpMethod != "GET")
		httpResponse.statusCode = 400;
	const size_t bodyStart = input.find(HEADER_DELIMITER);
	const bool hasBody = bodyStart != std::string::npos && bodyStart + strlen(HEADER_DELIMITER) < input.length();
	if (httpResponse.statusCode == 200 && (httpRequest.contentLength > 0 || input.find("boundary=") != std::string::npos || hasBody))
		parseBody(httpRequest, httpResponse, input);
	if (httpRequest.requestBody.empty() == false)
		httpRequest.contentLength = httpRequest.requestBody.length();
}

bool	parseChunkedBody(const std::string& rawRequest, size_t bodyStart, std::string& decodedBody, size_t& consumedBytes, bool& malformedBody)
{
	size_t cursor = bodyStart;
	decodedBody.clear();
	consumedBytes = 0;
	malformedBody = false;
	while (true)
	{
		const size_t lineEnd = rawRequest.find("\r\n", cursor);
		if (lineEnd == std::string::npos)
			return false;
		std::string sizeLine = rawRequest.substr(cursor, lineEnd - cursor);
		const size_t extPos = sizeLine.find(';');
		if (extPos != std::string::npos)
			sizeLine = sizeLine.substr(0, extPos);
		size_t chunkSize = 0;
		try
		{
			chunkSize = std::stoul(sizeLine, nullptr, 16);
		}
		catch (const std::exception&)
		{
			malformedBody = true;
			return false;
		}
		cursor = lineEnd + 2;
		if (chunkSize == 0)
		{
			if (rawRequest.length() < cursor + 2)
				return false;
			if (rawRequest.compare(cursor, 2, "\r\n") != 0)
			{
				malformedBody = true;
				return false;
			}
			consumedBytes = cursor + 2 - bodyStart;
			return true;
		}
		if (rawRequest.length() < cursor + chunkSize + 2)
			return false;
		decodedBody.append(rawRequest, cursor, chunkSize);
		cursor += chunkSize;
		if (rawRequest.compare(cursor, 2, "\r\n") != 0)
		{
			malformedBody = true;
			return false;
		}
		cursor += 2;
	}
}
