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

bool	parseChunkedBody(const std::string& rawBody, std::string& decodedBody, size_t& consumedBytes, bool& malformedBody)
{
	size_t cursor = 0;
	decodedBody.clear();
	consumedBytes = 0;
	malformedBody = false;
	while (true)
	{
		const size_t lineEnd = rawBody.find("\r\n", cursor);
		if (lineEnd == std::string::npos)
			return false;
		std::string sizeLine = rawBody.substr(cursor, lineEnd - cursor);
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
			if (rawBody.length() < cursor + 2)
				return false;
			if (rawBody.compare(cursor, 2, "\r\n") != 0)
			{
				malformedBody = true;
				return false;
			}
			consumedBytes = cursor + 2;
			return true;
		}
		if (rawBody.length() < cursor + chunkSize + 2)
			return false;
		decodedBody.append(rawBody, cursor, chunkSize);
		cursor += chunkSize;
		if (rawBody.compare(cursor, 2, "\r\n") != 0)
		{
			malformedBody = true;
			return false;
		}
		cursor += 2;
	}
}

} // namespace

bool	WebServerCore::beginCgiForSession(ClientSession& session, int clientFd, HttpResponse& httpResponse)
{
	if (isDirectory(session.virtualHost._documentRoot + session.httpRequest.requestPath))
	{
		httpResponse.statusCode = 403;
		return false;
	}

	pid_t pid = -1;
	int inputWriteFd = -1;
	int outputReadFd = -1;
	if (startCGIProcess(session.virtualHost, session.httpRequest, pid, inputWriteFd, outputReadFd) == false)
	{
		httpResponse.statusCode = 500;
		return false;
	}

	setFdFlag(inputWriteFd, O_NONBLOCK);
	setFdFlag(outputReadFd, O_NONBLOCK);

	struct epoll_event inputEvent{};
	inputEvent.events = EPOLLOUT;
	inputEvent.data.fd = inputWriteFd;
	if (epoll_ctl(_epollFd, EPOLL_CTL_ADD, inputWriteFd, &inputEvent) == -1)
	{
		close(inputWriteFd);
		close(outputReadFd);
		kill(pid, SIGKILL);
		waitpid(pid, nullptr, WNOHANG);
		httpResponse.statusCode = 500;
		return false;
	}
	struct epoll_event outputEvent{};
	outputEvent.events = EPOLLIN;
	outputEvent.data.fd = outputReadFd;
	if (epoll_ctl(_epollFd, EPOLL_CTL_ADD, outputReadFd, &outputEvent) == -1)
	{
		epoll_ctl(_epollFd, EPOLL_CTL_DEL, inputWriteFd, nullptr);
		close(inputWriteFd);
		close(outputReadFd);
		kill(pid, SIGKILL);
		waitpid(pid, nullptr, WNOHANG);
		httpResponse.statusCode = 500;
		return false;
	}

	session.cgiActive = true;
	session.cgiPid = pid;
	session.cgiInputPipeFd = inputWriteFd;
	session.cgiOutputPipeFd = outputReadFd;
	session.cgiWriteOffset = 0;
	session.cgiOutputBuffer.clear();
	session.cgiStartTime = getMilliseconds();
	_cgiPipeToClient[inputWriteFd] = clientFd;
	_cgiPipeToClient[outputReadFd] = clientFd;
	if (session.httpRequest.requestBody.empty())
	{
		epoll_ctl(_epollFd, EPOLL_CTL_DEL, inputWriteFd, nullptr);
		close(inputWriteFd);
		_cgiPipeToClient.erase(inputWriteFd);
		session.cgiInputPipeFd = -1;
	}
	return true;
}

uint16_t WebServerCore::receiveRequestData(ClientSession& session, int clientFd)
{
	const char*		endOfHeader = "\r\n\r\n";
	char			buffer[1024]{};
	ssize_t			readBytes = read(clientFd, buffer, sizeof(buffer));

	if (readBytes < 0)
		return 500;
	if (readBytes == 0)
		return 500;
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
		std::string contentLength = extractSubstring(session.rawRequest, "Content-Length:", "\n", strlen("Content-Length: "));
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
		if (parseChunkedBody(session.rawRequest.substr(session.headerEndPos), decodedBody, consumedBytes, malformedChunkedBody) == false)
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
	if (httpResponse.statusCode == 500)
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

void	WebServerCore::handleCgiPipeEvent(int pipeFd, uint32_t events)
{
	auto pipeIt = _cgiPipeToClient.find(pipeFd);
	if (pipeIt == _cgiPipeToClient.end())
		return;
	const int clientFd = pipeIt->second;
	auto sessionIt = _sessionMap.find(clientFd);
	if (sessionIt == _sessionMap.end())
		return;
	ClientSession& session = sessionIt->second;
	if (session.cgiActive == false)
		return;

	if (events & EPOLLERR)
	{
		if (session.cgiInputPipeFd != -1)
		{
			epoll_ctl(_epollFd, EPOLL_CTL_DEL, session.cgiInputPipeFd, nullptr);
			close(session.cgiInputPipeFd);
			_cgiPipeToClient.erase(session.cgiInputPipeFd);
			session.cgiInputPipeFd = -1;
		}
		if (session.cgiOutputPipeFd != -1)
		{
			epoll_ctl(_epollFd, EPOLL_CTL_DEL, session.cgiOutputPipeFd, nullptr);
			close(session.cgiOutputPipeFd);
			_cgiPipeToClient.erase(session.cgiOutputPipeFd);
			session.cgiOutputPipeFd = -1;
		}
		if (session.cgiPid > 0)
		{
			kill(session.cgiPid, SIGKILL);
			waitpid(session.cgiPid, nullptr, WNOHANG);
			session.cgiPid = -1;
		}
		session.httpResponse.statusCode = 500;
		getErrorPage(session.virtualHost, session.httpResponse);
		session.responseBuffer = buildSerializedResponse(session.httpRequest, session.httpResponse);
		session.sentBytes = 0;
		session.responseReady = true;
		session.cgiActive = false;
		return;
	}

	if (pipeFd == session.cgiInputPipeFd && (events & (EPOLLOUT | EPOLLHUP)))
	{
		const std::string& body = session.httpRequest.requestBody;
		const size_t remaining = body.length() - session.cgiWriteOffset;
		if (remaining == 0)
		{
			epoll_ctl(_epollFd, EPOLL_CTL_DEL, session.cgiInputPipeFd, nullptr);
			close(session.cgiInputPipeFd);
			_cgiPipeToClient.erase(session.cgiInputPipeFd);
			session.cgiInputPipeFd = -1;
		}
		else
		{
			ssize_t written = write(session.cgiInputPipeFd, body.c_str() + session.cgiWriteOffset, remaining);
			if (written <= 0)
			{
				cleanupSession(clientFd);
				return;
			}
			session.cgiWriteOffset += static_cast<size_t>(written);
			if (session.cgiWriteOffset >= body.length())
			{
				epoll_ctl(_epollFd, EPOLL_CTL_DEL, session.cgiInputPipeFd, nullptr);
				close(session.cgiInputPipeFd);
				_cgiPipeToClient.erase(session.cgiInputPipeFd);
				session.cgiInputPipeFd = -1;
			}
		}
	}

	if (pipeFd == session.cgiOutputPipeFd && (events & (EPOLLIN | EPOLLHUP)))
	{
		char buffer[1024]{};
		ssize_t bytesRead = read(session.cgiOutputPipeFd, buffer, sizeof(buffer));
		if (bytesRead > 0)
		{
			session.cgiOutputBuffer.append(buffer, bytesRead);
			session.lastActivityAt = getMilliseconds();
		}
		else if (bytesRead == 0)
		{
			epoll_ctl(_epollFd, EPOLL_CTL_DEL, session.cgiOutputPipeFd, nullptr);
			close(session.cgiOutputPipeFd);
			_cgiPipeToClient.erase(session.cgiOutputPipeFd);
			session.cgiOutputPipeFd = -1;

			int status = 0;
			const pid_t waitResult = session.cgiPid > 0 ? waitpid(session.cgiPid, &status, WNOHANG) : -1;
			if (waitResult > 0)
				session.cgiPid = -1;
			session.cgiActive = false;
			if (waitResult > 0 && (WIFEXITED(status) == false || WEXITSTATUS(status) != 0))
			{
				session.httpResponse.statusCode = 500;
				getErrorPage(session.virtualHost, session.httpResponse);
			}
			else if (parseCGIResponse(session.cgiOutputBuffer, session.httpResponse) == false)
			{
				session.httpResponse.statusCode = 500;
				getErrorPage(session.virtualHost, session.httpResponse);
			}
			logger::write(session.virtualHost, session.httpRequest, session.httpResponse, clientFd);
			session.responseBuffer = buildSerializedResponse(session.httpRequest, session.httpResponse);
			session.sentBytes = 0;
			session.responseReady = true;
		}
		else
		{
			// Pipe can wake with HUP while no bytes are available yet.
			if ((events & EPOLLHUP) == 0)
				cleanupSession(clientFd);
		}
	}
}
