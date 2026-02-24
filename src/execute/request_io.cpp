#include "webserv.hpp"
#include <cctype>

namespace
{
void	applyRedirect(const RouteConfig& routeConfig, HttpResponse& httpResponse)
{
	httpResponse.statusCode = routeConfig.redirectCode;
	httpResponse.extraHeaders = "Location: " + routeConfig.redirectTarget + "\r\n";
	layoutPage(httpResponse, "Redirect",
		"<h1>" + getErrorCode(routeConfig.redirectCode) + "</h1><p>Go to <a href=\"" +
		routeConfig.redirectTarget + "\">" + routeConfig.redirectTarget + "</a></p>");
	httpResponse.contentLength = httpResponse.responseBody.length();
}

std::string	toLower(std::string in)
{
	for (size_t i = 0; i < in.length(); i++)
		in[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(in[i])));
	return in;
}

} // namespace

uint16_t WebServerCore::receiveRequestData(ClientSession& session, int clientFd)
{
	const char*		endOfHeader = "\r\n\r\n";
	char			buffer[8192]{};
	ssize_t			readBytes = read(clientFd, buffer, sizeof(buffer));

	if (readBytes < 0)
		return 500;
	if (readBytes == 0)
		return 499;
	session.rawRequest.append(buffer, readBytes);
	session.lastActivityAt = getMilliseconds();

	if (session.headerParsed == false)
	{
		const size_t headerPos = session.rawRequest.find(endOfHeader);
		if (headerPos == session.rawRequest.npos)
		{
			// Support HTTP/0.9 simple requests: "GET /path\r\n" (no headers/body).
			const size_t lineEndPos = session.rawRequest.find("\r\n");
			if (lineEndPos != std::string::npos)
			{
				const std::string requestLine = session.rawRequest.substr(0, lineEndPos);
				if (requestLine.find("HTTP/") == std::string::npos && requestLine.find("GET ") == 0)
				{
					session.headerParsed = true;
					session.headerEndPos = lineEndPos + 2;
					session.expectedBodySize = 0;
					session.chunkedTransfer = false;
					return 200;
				}
			}
			if (session.rawRequest.length() > defaults::MAX_HEADER_SIZE)
				return 431;
			return 0;
		}
		session.headerParsed = true;
		session.headerEndPos = headerPos + strlen(endOfHeader);
		const std::string headerPart = toLower(session.rawRequest.substr(0, session.headerEndPos));
		session.chunkedTransfer = headerPart.find("transfer-encoding: chunked") != std::string::npos;
		std::string contentLength = extractSubstring(headerPart, "content-length:", "\n", strlen("content-length: "));
		if (contentLength.empty())
		{
			session.expectedBodySize = 0;
		}
		else
		{
			try
			{
				if (contentLength.length() > 18)
					throw std::out_of_range("too large");
				session.expectedBodySize = std::stoll(contentLength);
				if (session.expectedBodySize < 0)
					throw std::out_of_range("is negative");
			}
			catch (const std::exception& e)
			{
				logger::addMsg((std::string)"invalid content length: " + e.what());
				return 400;
			}
		}
		if (session.chunkedTransfer == false
			&& static_cast<uint64_t>(session.expectedBodySize) > session.virtualHost._maxBodySize)
		{
			return 413;
		}
	}

	size_t bodyRead = session.rawRequest.length() - session.headerEndPos;
	if (session.chunkedTransfer)
	{
		std::string decodedBody;
		size_t consumedBytes = 0;
		bool malformedChunkedBody = false;
		if (parseChunkedBody(session.rawRequest, session.headerEndPos, decodedBody, consumedBytes, malformedChunkedBody) == false)
		{
			if (malformedChunkedBody)
				return 400;
			return 0;
		}
		if (decodedBody.length() > session.virtualHost._maxBodySize)
			return 413;
		std::string normalizedHeaders = session.rawRequest.substr(0, session.headerEndPos - 4);
		normalizedHeaders += "\r\nContent-Length: " + std::to_string(decodedBody.length()) + "\r\n\r\n";
		session.rawRequest = normalizedHeaders + decodedBody;
		session.headerEndPos = normalizedHeaders.length();
		session.expectedBodySize = decodedBody.length();
		return 200;
	}
	if (bodyRead < static_cast<size_t>(session.expectedBodySize))
		return 0;
	return 200;
}

void	WebServerCore::processIncomingRequest(std::map<int, ClientSession>::iterator sessionIter)
{
	const int clientFd = sessionIter->first;
	ClientSession& session = sessionIter->second;
	if (session.responseReady || session.cgiActive)
		return;

	HttpRequest& httpRequest = session.httpRequest;
	HttpResponse& httpResponse = session.httpResponse;
	httpResponse.statusCode = receiveRequestData(session, clientFd);
	if (httpResponse.statusCode == 0)
		return;
	if (httpResponse.statusCode >= 499)
	{
		cleanupSession(clientFd);
		return;
	}

	if (httpResponse.statusCode == 200)
		parseRequest(session.rawRequest, session.virtualHost, httpRequest, httpResponse);
	if (httpResponse.statusCode == 200)
	{
		auto routeEntry = resolveRoute(session.virtualHost, session.httpRequest.requestPath);
		if (routeEntry != session.virtualHost._routeTable.end() && routeEntry->second.hasRedirect)
		{
			applyRedirect(routeEntry->second, httpResponse);
		}
		else if (session.httpRequest.requestPath.find("/cgi-bin") == 0)
		{
			beginCgiForSession(session, clientFd, httpResponse);
		}
		else if (session.httpRequest.httpMethod == "POST")
		{
			handlePost(session.virtualHost, session.httpRequest, httpResponse);
		}
		else if (session.httpRequest.httpMethod == "GET")
		{
			handleGet(session.virtualHost, session.httpRequest, httpResponse);
		}
		else if (session.httpRequest.httpMethod == "DELETE")
		{
			handleDelete(session.virtualHost, session.httpRequest, httpResponse);
		}
	}
	if (session.cgiActive)
		return;
	if (httpResponse.statusCode != 200 && (httpResponse.statusCode < 300 || httpResponse.statusCode >= 400))
		getErrorPage(session.virtualHost, httpResponse);

	logger::write(session.virtualHost, httpRequest, httpResponse, clientFd);
	session.responseBuffer = buildSerializedResponse(httpRequest, httpResponse);
	session.sentBytes = 0;
	session.lastActivityAt = getMilliseconds();
	session.responseReady = true;
}
