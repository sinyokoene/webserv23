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
