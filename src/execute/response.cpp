#include "webserv.hpp"

std::string	buildSerializedResponse(const HttpRequest& httpRequest, const HttpResponse& httpResponse)
{
	if (httpRequest.httpProtocol == "HTTP/0.9")
		return httpResponse.responseBody;

	std::ostringstream out;
	const std::string protocol = httpRequest.httpProtocol == "HTTP/1.0" ? "HTTP/1.0" : "HTTP/1.1";
	out << protocol << " " << getErrorCode(httpResponse.statusCode) << "\r\n";
	if (httpResponse.mimeType.empty() == false)
		out << "Content-Type: " << getFullFileType(httpResponse.mimeType) << "\r\n";
	out << httpResponse.extraHeaders;
	out << "Content-Length: " << httpResponse.contentLength << "\r\n";
	out << "Connection: close\r\n";
	out << "\r\n";
	out << httpResponse.responseBody;
	return out.str();
}

void	WebServerCore::dispatchResponse(std::map<int, ClientSession>::iterator sessionIter)
{
	if (sessionIter == _sessionMap.end())
		return;
	const int clientFd = sessionIter->first;
	ClientSession& session = sessionIter->second;
	if (session.responseReady == false)
		return;

	const size_t remaining = session.responseBuffer.length() - session.sentBytes;
	if (remaining == 0)
	{
		cleanupSession(clientFd);
		return;
	}

	const ssize_t sent = send(clientFd, session.responseBuffer.c_str() + session.sentBytes, remaining, 0);
	if (sent <= 0)
	{
		logger::addMsg("send");
		cleanupSession(clientFd);
		return;
	}
	session.sentBytes += static_cast<size_t>(sent);
	session.lastActivityAt = getMilliseconds();
	if (session.sentBytes >= session.responseBuffer.length())
		cleanupSession(clientFd);
}
