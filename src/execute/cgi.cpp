#include "webserv.hpp"

void	free2D(const char* const* envArray)
{
	for (uint32_t i = 0; envArray && envArray[i] != nullptr; i++)
	{
		delete[] envArray[i];
	}
	delete[] envArray;
}

const char**	initEnv(const HttpRequest& httpRequest)
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
		"CONTENT_LENGTH=" + std::to_string(httpRequest.contentLength),
		"SCRIPT_NAME=" + httpRequest.requestPath,
		"SCRIPT_FILENAME=" + httpRequest.requestPath,
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

bool	initFork(int inputPipe[2], int outputPipe[2], pid_t& processId)
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

void	execute(VirtualHost& virtualHost, HttpRequest& httpRequest, int inputPipe[2], int outputPipe[2])
{
	close(inputPipe[1]);
	close(outputPipe[0]);

	std::string		scriptPath    = virtualHost._documentRoot + httpRequest.requestPath;
	const char**	envVars     = initEnv(httpRequest);
	const char*		argv[2] = {scriptPath.c_str(), nullptr};

	if (envVars == nullptr)
	{
		exit(EXIT_FAILURE);
	}
	if (dup2(inputPipe[0], STDIN_FILENO) < 0 || dup2(outputPipe[1], STDOUT_FILENO) < 0)
	{
		exit(EXIT_FAILURE);
	}
	execve(argv[0], (char* const*)argv, (char* const*)envVars);
	free2D(envVars);
	exit(EXIT_FAILURE);
}

void	handleCGI(VirtualHost& virtualHost, HttpRequest& httpRequest, HttpResponse& httpResponse)
{
	int		inputPipe[2], outputPipe[2];
	pid_t	processId;
	
	if (isDirectory(virtualHost._documentRoot + httpRequest.requestPath))
	{
		httpResponse.statusCode = 403;
		return;
	}
	if (initFork(inputPipe, outputPipe, processId) == false)
	{
		httpResponse.statusCode = 500;
		return;
	}
	if (!processId)
	{
		execute(virtualHost, httpRequest, inputPipe, outputPipe);
	}

	close(inputPipe[0]);
	close(outputPipe[1]);
	if (httpRequest.contentLength > 0)
	{
		ssize_t	totalWritten = 0;
		while (totalWritten < static_cast<ssize_t>(httpRequest.contentLength))
		{
		ssize_t bytes = write(inputPipe[1], httpRequest.requestBody.c_str() + totalWritten,
			httpRequest.contentLength - totalWritten);
		if (bytes < 0)
		{
			close(inputPipe[1]);
				close(outputPipe[0]);
				httpResponse.statusCode = 500;
				logger::addMsg("couldn't write to pipe");
				return;
			}
			if (bytes == 0)
				break;
			totalWritten += bytes;
		}
		if (totalWritten < static_cast<ssize_t>(httpRequest.contentLength))
		{
			close(inputPipe[1]);
			close(outputPipe[0]);
			httpResponse.statusCode = 500;
			logger::addMsg("short write to CGI input pipe");
			return;
		}
	}

	// check child
	close(inputPipe[1]);
	int	exitStatus;
	checkTimeoutExpired(virtualHost._connectionTimeoutMs);
	while (waitpid(processId, &exitStatus, WNOHANG) == 0)
	{
		if(checkTimeoutExpired() == true)
		{
			kill(processId, SIGKILL);
			httpResponse.statusCode = 500;
			logger::addMsg("script timed out");
			close(outputPipe[0]);
			return;
		}
	}
	if (WEXITSTATUS(exitStatus) != 0)
	{
		httpResponse.statusCode = 500;
		logger::addMsg("failed to run script, exit status: " + std::to_string(WEXITSTATUS(exitStatus)));
		close(outputPipe[0]);
		return;
	}

	// The child process has already exited (confirmed by waitpid above).
	// All output is already present in the kernel pipe buffer — reads will
	// not block. This pipe fd is local IPC, not a network socket.
	char		buffer[1025]{};
	ssize_t		bytesRead;
	std::string	outputContent;
	do
	{
		bytesRead = read(outputPipe[0], buffer, sizeof(buffer) - 1);
		if (bytesRead > 0)
			outputContent.append(buffer, bytesRead);
		std::memset(buffer, '\0', sizeof(buffer));
	}
	while(bytesRead > 0);
	close(outputPipe[0]);
	if (bytesRead < 0)
	{
		httpResponse.statusCode = 500;
		logger::addMsg("failed to read script output");
		return;
	}
	layoutPage(httpResponse, httpRequest.requestPath, outputContent);
	httpResponse.contentLength = httpResponse.responseBody.length();
}
