#include "webserv.hpp"

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
		logger::addMsg("accept");
		return false;
	}
	setFdFlag(clientFd, O_NONBLOCK);

	_epollEvent.events = EPOLLIN | EPOLLOUT;
	_epollEvent.data.fd = clientFd;
	if (epoll_ctl(_epollFd, EPOLL_CTL_ADD, clientFd, &_epollEvent) == -1)
	{
		logger::addMsg("epoll_ctl add client");
		close(clientFd);
		return false;
	}

	ClientSession session{virtualHost};
	session.lastActivityAt = getMilliseconds();
	_sessionMap.insert(std::make_pair(clientFd, session));
	return true;
}

void	WebServerCore::cleanupSession(int clientFd)
{
	auto sessionIt = _sessionMap.find(clientFd);
	if (sessionIt != _sessionMap.end())
	{
		ClientSession& session = sessionIt->second;
		if (session.cgiPid > 0)
		{
			kill(session.cgiPid, SIGKILL);
			waitpid(session.cgiPid, nullptr, WNOHANG);
		}
		closeCgiPipe(session.cgiInputPipeFd);
		closeCgiPipe(session.cgiOutputPipeFd);
		_sessionMap.erase(sessionIt);
	}
	if (epoll_ctl(_epollFd, EPOLL_CTL_DEL, clientFd, nullptr) == -1)
		logger::addMsg((std::string)"epoll_ctl fd: " + std::to_string(clientFd));
	if (close(clientFd) == -1)
		logger::addMsg("close");
}
