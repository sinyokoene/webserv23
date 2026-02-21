#include "webserv.hpp"
#include <cerrno>

bool isRunning = true;

namespace
{
std::string	buildHttpResponse(const HttpResponse& httpResponse)
{
	std::ostringstream out;

	out << "HTTP/1.1 " << getErrorCode(httpResponse.statusCode) << "\r\n"
		<< "Content-Type: " << getFullFileType(httpResponse.mimeType) << "\r\n"
		<< httpResponse.extraHeaders
		<< "Content-Length: " << httpResponse.contentLength << "\r\n"
		<< "Connection: close\r\n"
		<< "\r\n"
		<< httpResponse.responseBody;
	return out.str();
}

void	applyRedirect(const RouteConfig& routeConfig, HttpResponse& httpResponse)
{
	httpResponse.statusCode = routeConfig.redirectCode;
	httpResponse.extraHeaders = "Location: " + routeConfig.redirectTarget + "\r\n";
	layoutPage(httpResponse, "Redirect",
		"<h1>" + getErrorCode(routeConfig.redirectCode) + "</h1><p>Go to <a href=\"" +
		routeConfig.redirectTarget + "\">" + routeConfig.redirectTarget + "</a></p>");
	httpResponse.contentLength = httpResponse.responseBody.length();
}

void	dispatchRequest(VirtualHost& virtualHost, HttpRequest& httpRequest, HttpResponse& httpResponse)
{
	auto routeEntry = resolveRoute(virtualHost, httpRequest.requestPath);
	if (routeEntry != virtualHost._routeTable.end() && routeEntry->second.hasRedirect)
		return applyRedirect(routeEntry->second, httpResponse);
	if (httpRequest.requestPath.find("/cgi-bin") == 0)
		return handleCGI(virtualHost, httpRequest, httpResponse);
	if (httpRequest.httpMethod == "POST")
		return handlePost(virtualHost, httpRequest, httpResponse);
	if (httpRequest.httpMethod == "GET")
		return handleGet(virtualHost, httpRequest, httpResponse);
	if (httpRequest.httpMethod == "DELETE")
		return handleDelete(virtualHost, httpRequest, httpResponse);
}

} // namespace

void sig_handle(int signal)
{
	(void)signal;
	isRunning = false;
}

VirtualHost *WebServerCore::locateVirtualHost(int eventIdx)
{
	for (auto it : _virtualHosts)
	{
		if (_eventBuffer[eventIdx].data.fd == it->_socketFd)
			return it;
	}
	return nullptr;
}

bool	WebServerCore::establishConnection(VirtualHost &virtualHost)
{
	const int clientFd = accept(virtualHost._socketFd, nullptr, nullptr);
	if (clientFd == -1)
	{
		std::cerr << "accept failed miserably\n";
		return false;
	}
	setFdFlag(clientFd, O_NONBLOCK);

	_epollEvent.events = EPOLLIN | EPOLLOUT;
	_epollEvent.data.fd = clientFd;

	if (epoll_ctl(_epollFd, EPOLL_CTL_ADD, clientFd, &_epollEvent) == -1)
	{
		std::cerr << "epoll_ctl failed\n";
		close(clientFd);
		return false;
	}

	_sessionMap.insert(std::pair<int, ClientSession>(clientFd,
		ClientSession{virtualHost, HttpRequest(), HttpResponse(), "", "", 0, false, false, -1, 0, getMilliseconds()}));
	// lastActivityAt is initialised to now so idle clients are timed out even if they never send a byte.
	return true;
}

uint16_t WebServerCore::receiveRequestData(ClientSession& session, int clientFd)
{
	const char*		endOfHeader = "\r\n\r\n";
	char			buffer[1024]{};
	ssize_t			readBytes = read(clientFd, buffer, 1024);

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
			return 0;
		session.headerParsed = true;
		session.headerEndPos = headerPos + strlen(endOfHeader);
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
	}

	size_t bodyRead = session.rawRequest.length() - session.headerEndPos;
	if (bodyRead < static_cast<size_t>(session.expectedBodySize))
		return 0;
	if (static_cast<uint64_t>(session.expectedBodySize) > session.virtualHost._maxBodySize)
		return 413;
	return 200;
}

void	WebServerCore::processIncomingRequest(std::map<int, ClientSession>::iterator sessionIter)
{
	int				clientFd = sessionIter->first;
	ClientSession&	session = sessionIter->second;
	if (session.responseReady)
		return;
	VirtualHost&	virtualHost = sessionIter->second.virtualHost;
	HttpRequest&	httpRequest = sessionIter->second.httpRequest;
	HttpResponse&	httpResponse = sessionIter->second.httpResponse;
	httpResponse.statusCode = receiveRequestData(sessionIter->second, clientFd);
	if (httpResponse.statusCode == 0)
		return;
	if (httpResponse.statusCode == 500)
	{
		epoll_ctl(_epollFd, EPOLL_CTL_DEL, clientFd, NULL);
		close(clientFd);
		_sessionMap.erase(clientFd);
		return;
	}

	if (httpResponse.statusCode == 200)
		parseRequest(session.rawRequest, virtualHost, httpRequest, httpResponse);
	if (httpResponse.statusCode == 200)
		dispatchRequest(virtualHost, httpRequest, httpResponse);
	if (httpResponse.statusCode != 200 && (httpResponse.statusCode < 300 || httpResponse.statusCode >= 400))
		getErrorPage(virtualHost, httpResponse);

	logger::write(virtualHost, httpRequest, httpResponse, clientFd);
	session.responseBuffer = buildHttpResponse(session.httpResponse);
	session.sentBytes = 0;
	// Refresh activity when response becomes ready, otherwise timeout cleanup
	// can close the socket before EPOLLOUT sends the error page.
	session.lastActivityAt = getMilliseconds();
	session.responseReady = true;
}

void	WebServerCore::dispatchResponse(std::map<int, ClientSession>::iterator sessionIter)
{
	if (sessionIter == _sessionMap.end())
		return;

	const int	clientFd = sessionIter->first;
	ClientSession& session = sessionIter->second;
	if (session.responseReady == false)
		return;

	const size_t remaining = session.responseBuffer.length() - session.sentBytes;
	if (remaining == 0)
		return;
	ssize_t sent = send(clientFd, session.responseBuffer.c_str() + session.sentBytes, remaining, 0);
	if (sent < 0)
	{
		logger::addMsg("send");
		if (epoll_ctl(_epollFd, EPOLL_CTL_DEL, clientFd, NULL) == -1)
			logger::addMsg((std::string)"epoll_ctl fd: " + std::to_string(clientFd));
		if (close(clientFd) == -1)
			logger::addMsg("close");
		_sessionMap.erase(clientFd);
		return;
	}
	if (sent == 0)
	{
		if (epoll_ctl(_epollFd, EPOLL_CTL_DEL, clientFd, NULL) == -1)
			logger::addMsg((std::string)"epoll_ctl fd: " + std::to_string(clientFd));
		if (close(clientFd) == -1)
			logger::addMsg("close");
		_sessionMap.erase(clientFd);
		return;
	}
	session.sentBytes += static_cast<size_t>(sent);
	session.lastActivityAt = getMilliseconds();
	if (session.sentBytes < session.responseBuffer.length())
		return;

	if (epoll_ctl(_epollFd, EPOLL_CTL_DEL, clientFd, NULL) == -1)
		logger::addMsg((std::string)"epoll_ctl fd: " + std::to_string(clientFd));
	if (close(clientFd) == -1)
		logger::addMsg("close");
	_sessionMap.erase(clientFd);
}

void	WebServerCore::startEventLoop()
{
	std::cout << "\nRunning servers:\n";
	for (auto host : _virtualHosts)
		std::cout << "  " << host->_hostName << " -> http://" << host->_bindAddress << ":" << host->_listenPort << "/\n";
	std::cout << "\n";

	signal(SIGINT, sig_handle);

	while (isRunning)
	{
		int evCount = epoll_wait(_epollFd, _eventBuffer, 10, 1000);
		if (evCount == -1)
		{
			if (errno == EINTR)
				continue;
			logger::addMsg("epoll_wait");
			continue;
		}

		for (int i = 0; i < evCount; i++)
		{
			VirtualHost *virtualHost = locateVirtualHost(i);

			if (virtualHost != nullptr)
			{
				if (establishConnection(*virtualHost) == false)
					continue;
			}
			else
			{
				auto	it = _sessionMap.find(_eventBuffer[i].data.fd);
				if (it == _sessionMap.end())
					continue;
				const uint32_t events = _eventBuffer[i].events;

				if (events & (EPOLLERR | EPOLLHUP))
				{
					if (epoll_ctl(_epollFd, EPOLL_CTL_DEL, it->first, NULL) == -1)
						logger::addMsg((std::string)"epoll_ctl fd: " + std::to_string(it->first));
					if (close(it->first) == -1)
						logger::addMsg("close");
					_sessionMap.erase(it);
					continue;
				}

				// Handle at most one I/O operation per client per epoll dispatch.
				if ((events & EPOLLOUT) && it->second.responseReady)
					dispatchResponse(it);
				else if (events & EPOLLIN)
					processIncomingRequest(it);
			}
		}

		const int64_t now = getMilliseconds();
		for (auto it = _sessionMap.begin(); it != _sessionMap.end(); )
		{
			if (now - it->second.lastActivityAt > it->second.virtualHost._connectionTimeoutMs)
			{
				epoll_ctl(_epollFd, EPOLL_CTL_DEL, it->first, NULL);
				close(it->first);
				it = _sessionMap.erase(it);
			}
			else
				++it;
		}
	}
	std::cout << "\nstopped running\n";
}
