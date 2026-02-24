#include "webserv.hpp"
#include <cerrno>

namespace runtime
{
bool isRunning = true;
}

void sig_handle(int signal)
{
	(void)signal;
	runtime::isRunning = false;
}

void	WebServerCore::startEventLoop()
{
	std::cout << "\nRunning servers:\n";
	for (auto host : _virtualHosts)
		std::cout << "  " << host->_hostName << " -> http://" << host->_bindAddress << ":" << host->_listenPort << "/\n";
	std::cout << "\n";

	signal(SIGINT, sig_handle);

	while (runtime::isRunning)
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
				auto pipeIter = _cgiPipeToClient.find(_eventBuffer[i].data.fd);
				if (pipeIter != _cgiPipeToClient.end())
				{
					handleCgiPipeEvent(_eventBuffer[i].data.fd, _eventBuffer[i].events);
					continue;
				}
				auto	it = _sessionMap.find(_eventBuffer[i].data.fd);
				if (it == _sessionMap.end())
					continue;
				const uint32_t events = _eventBuffer[i].events;

				if (events & (EPOLLERR | EPOLLHUP))
				{
					cleanupSession(it->first);
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
			ClientSession& session = it->second;
			bool timedOut = now - session.lastActivityAt > session.virtualHost._connectionTimeoutMs;
			if (session.cgiActive)
			{
				timedOut = false;
				if (now - session.cgiStartTime > session.virtualHost._cgiTimeoutMs)
				{
					if (session.cgiPid > 0)
					{
						kill(session.cgiPid, SIGKILL);
						waitpid(session.cgiPid, nullptr, WNOHANG);
						session.cgiPid = -1;
					}
					session.httpResponse.statusCode = 504;
					getErrorPage(session.virtualHost, session.httpResponse);
					session.responseBuffer = buildSerializedResponse(session.httpRequest, session.httpResponse);
					session.responseReady = true;
					session.cgiActive = false;
					closeCgiPipe(session.cgiInputPipeFd);
					closeCgiPipe(session.cgiOutputPipeFd);
				}
				else
				{
					if (session.cgiPid > 0)
						waitpid(session.cgiPid, nullptr, WNOHANG);
				}
			}
			if (timedOut)
			{
				const int fd = it->first;
				++it;
				cleanupSession(fd);
			}
			else
				++it;
		}
	}
	std::cout << "\nstopped running\n";
}
