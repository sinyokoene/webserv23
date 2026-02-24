#include "webserv.hpp"

namespace
{
void	free2D(const char* const* envArray)
{
	for (uint32_t i = 0; envArray != nullptr && envArray[i] != nullptr; i++)
	{
		delete[] envArray[i];
	}
	delete[] envArray;
}

std::string	extractScriptDir(const std::string& scriptPath)
{
	const size_t slashPos = scriptPath.find_last_of('/');
	if (slashPos == std::string::npos)
		return ".";
	if (slashPos == 0)
		return "/";
	return scriptPath.substr(0, slashPos);
}

const char**	initEnv(const HttpRequest& httpRequest, const std::string& scriptPath)
{
	std::vector<std::string> envVars = 
	{
		"REDIRECT_STATUS=200",
		"GATEWAY_INTERFACE=CGI/1.1",
		"PATH=/usr/bin:/bin",
		"REQUEST_METHOD=" + httpRequest.httpMethod,
		"SERVER_PROTOCOL=" + httpRequest.httpProtocol,
		"SERVER_NAME=" + httpRequest.serverName,
		"SERVER_PORT=" + std::to_string(httpRequest.port),
		"CONTENT_TYPE=" + httpRequest.contentType,
		"CONTENT_LENGTH=" + std::to_string(httpRequest.requestBody.length()),
		"QUERY_STRING=" + httpRequest.queryString,
		"SCRIPT_NAME=" + httpRequest.requestPath,
		"SCRIPT_FILENAME=" + scriptPath,
		"PATH_INFO=" + httpRequest.requestPath,
		"TZ=Europe/Amsterdam"
	};

	char**	envArray{};
	try
	{
		envArray = new char*[envVars.size() + 1]{};
		for (uint32_t i = 0; i < envVars.size(); i++)
		{
			envArray[i] = new char[envVars[i].length() + 1];
			std::strcpy(envArray[i], envVars[i].c_str());
		}
	}
	catch(const std::exception& e)
	{
		free2D(envArray);
		std::cerr << "new failed when making env";
		return nullptr;
	}
	
	return (const char**)envArray;
}

bool	createPipesAndFork(int inputPipe[2], int outputPipe[2], pid_t& processId)
{
	if (pipe(inputPipe) < 0)
	{
		logger::addMsg("pipe error");
		return false;
	}
	if (pipe(outputPipe) < 0)
	{
		close(inputPipe[0]);
		close(inputPipe[1]);
		logger::addMsg("pipe error");
		return false;
	}
	processId = fork();
	if (processId == -1)
	{
		close(inputPipe[0]);
		close(inputPipe[1]);
		close(outputPipe[0]);
		close(outputPipe[1]);
		logger::addMsg("fork error");
		return false;
	}
	return true;
}

void	executeChildProcess(VirtualHost& virtualHost, HttpRequest& httpRequest, int inputPipe[2], int outputPipe[2])
{
	close(inputPipe[1]);
	close(outputPipe[0]);

	const std::string scriptPath = std::filesystem::absolute(
		virtualHost._documentRoot + httpRequest.requestPath).string();
	const std::string	scriptDir  = extractScriptDir(scriptPath);
	const char**	envVars     = initEnv(httpRequest, scriptPath);
	const char*		argv[2] = {scriptPath.c_str(), nullptr};

	if (envVars == nullptr)
	{
		exit(EXIT_FAILURE);
	}
	if (chdir(scriptDir.c_str()) < 0)
		exit(EXIT_FAILURE);
	if (dup2(inputPipe[0], STDIN_FILENO) < 0 || dup2(outputPipe[1], STDOUT_FILENO) < 0)
	{
		exit(EXIT_FAILURE);
	}
	execve(argv[0], (char* const*)argv, (char* const*)envVars);
	free2D(envVars);
	exit(EXIT_FAILURE);
}
} // namespace

bool	startCGIProcess(VirtualHost& virtualHost, HttpRequest& httpRequest, pid_t& processId, int& inputWriteFd, int& outputReadFd)
{
	int		inputPipe[2], outputPipe[2];
	
	if (isDirectory(virtualHost._documentRoot + httpRequest.requestPath))
	{
		return false;
	}
	if (createPipesAndFork(inputPipe, outputPipe, processId) == false)
	{
		return false;
	}
	if (!processId)
	{
		executeChildProcess(virtualHost, httpRequest, inputPipe, outputPipe);
	}
	close(inputPipe[0]);
	close(outputPipe[1]);
	inputWriteFd = inputPipe[1];
	outputReadFd = outputPipe[0];
	return true;
}

bool	parseCGIResponse(const std::string& cgiOutput, HttpResponse& httpResponse)
{
	const std::string separator = "\r\n\r\n";
	const size_t headerEnd = cgiOutput.find(separator);
	if (headerEnd == std::string::npos)
	{
		httpResponse.responseBody = cgiOutput;
		httpResponse.contentLength = httpResponse.responseBody.length();
		httpResponse.mimeType = ".html";
		return true;
	}

	const std::string headers = cgiOutput.substr(0, headerEnd);
	httpResponse.responseBody = cgiOutput.substr(headerEnd + separator.length());
	httpResponse.contentLength = httpResponse.responseBody.length();
	httpResponse.extraHeaders.clear();

	std::istringstream headerStream(headers);
	std::string line;
	while (std::getline(headerStream, line))
	{
		if (line.empty())
			continue;
		if (line.back() == '\r')
			line.erase(line.length() - 1);
		if (line.find("Status:") == 0)
		{
			std::string codePart = extractSubstring(line, "Status:", " ", strlen("Status: "));
			if (codePart.empty() == false)
			{
				try
				{
					httpResponse.statusCode = static_cast<uint16_t>(std::stoi(codePart));
				}
				catch (const std::exception&)
				{
					httpResponse.statusCode = 500;
					return false;
				}
			}
			continue;
		}
		if (line.find("Content-Type:") == 0)
		{
			std::string typeValue = line.substr(strlen("Content-Type:"));
			while (typeValue.empty() == false && (typeValue[0] == ' ' || typeValue[0] == '\t'))
				typeValue.erase(0, 1);
			if (typeValue.find("text/html") != std::string::npos)
				httpResponse.mimeType = ".html";
			else if (typeValue.find("application/json") != std::string::npos)
				httpResponse.mimeType = ".json";
			else if (typeValue.find("text/plain") != std::string::npos)
				httpResponse.mimeType = ".txt";
			continue;
		}
		httpResponse.extraHeaders += line + "\r\n";
	}
	if (httpResponse.mimeType.empty())
		httpResponse.mimeType = ".html";
	return true;
}

void	WebServerCore::closeCgiPipe(int& pipeFd)
{
	if (pipeFd == -1)
		return;
	epoll_ctl(_epollFd, EPOLL_CTL_DEL, pipeFd, nullptr);
	close(pipeFd);
	_cgiPipeToClient.erase(pipeFd);
	pipeFd = -1;
}

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
		closeCgiPipe(session.cgiInputPipeFd);
	return true;
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
		closeCgiPipe(session.cgiInputPipeFd);
		closeCgiPipe(session.cgiOutputPipeFd);
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
			closeCgiPipe(session.cgiInputPipeFd);
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
				closeCgiPipe(session.cgiInputPipeFd);
		}
	}

	if (pipeFd == session.cgiOutputPipeFd && (events & (EPOLLIN | EPOLLHUP)))
	{
		char buffer[8192]{};
		ssize_t bytesRead = read(session.cgiOutputPipeFd, buffer, sizeof(buffer));
		if (bytesRead > 0)
		{
			session.cgiOutputBuffer.append(buffer, bytesRead);
			session.lastActivityAt = getMilliseconds();
		}
		else if (bytesRead == 0)
		{
			closeCgiPipe(session.cgiOutputPipeFd);

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
