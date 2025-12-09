#include "Server.hpp"
#include "Utils.hpp"

#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

// Helper to convert HttpRequest to environment variables
static std::vector<char*> createCgiEnv(HttpRequest& request,
                                       const ConfigParser::ServerConfig& config,
                                       const LocationConfig& locConfig,
                                       const std::string& scriptPath) {
    std::map<std::string, std::string> envMap;

    // Server and request specific variables
    envMap["GATEWAY_INTERFACE"] = "CGI/1.1";
    envMap["SERVER_SOFTWARE"] = "WebServ/1.0";
    envMap["SERVER_NAME"] = config.serverName.empty() ? "localhost" : config.serverName;
    envMap["SERVER_PROTOCOL"] = request.getVersion();
    envMap["SERVER_PORT"] = config.listenPorts.empty() ? "80" : config.listenPorts[0];
    envMap["REQUEST_METHOD"] = request.getMethod();
    envMap["SCRIPT_NAME"] = request.getPath();
    envMap["SCRIPT_FILENAME"] = scriptPath;
    envMap["PATH_INFO"] = request.getPath();
    envMap["PATH_TRANSLATED"] = scriptPath;
    envMap["REQUEST_URI"] = request.getPath();
    envMap["QUERY_STRING"] = request.getQueryString();
    envMap["REMOTE_ADDR"] = "127.0.0.1";
    envMap["REMOTE_HOST"] = "localhost";

    // HTTP Headers
    const std::map<std::string, std::string>& headers = request.getHeaders();
    for (std::map<std::string, std::string>::const_iterator it = headers.begin(); it != headers.end(); ++it) {
        std::string envHeaderName = "HTTP_";
        std::string headerKey = it->first;
        for (size_t i = 0; i < headerKey.length(); ++i) {
            if (headerKey[i] == '-') {
                envHeaderName += '_';
            } else {
                envHeaderName += toupper(headerKey[i]);
            }
        }
        envMap[envHeaderName] = it->second;
    }

    if (request.getMethod() == "POST") {
        envMap["CONTENT_TYPE"] = request.getHeader("Content-Type");
        std::ostringstream oss;
        oss << request.getBody().length();
        envMap["CONTENT_LENGTH"] = oss.str();
    }
    
    if (!locConfig.getCgiPass().empty()) {
        envMap["CGI_PASS_DIRECTIVE"] = locConfig.getCgiPass();
    }

    std::vector<char*> cgiEnv;
    for (std::map<std::string, std::string>::const_iterator it = envMap.begin(); it != envMap.end(); ++it) {
        std::string envEntry = it->first + "=" + it->second;
        cgiEnv.push_back(strdup(envEntry.c_str()));
    }
    cgiEnv.push_back(NULL);

    return cgiEnv;
}

// Helper to free environment variables
static void freeCgiEnv(std::vector<char*>& cgiEnv) {
    for (size_t i = 0; i < cgiEnv.size(); ++i) {
        if (cgiEnv[i]) {
            free(cgiEnv[i]);
        }
    }
}

// Start CGI request (non-blocking, returns true on success)
bool Server::startCgiRequest(int clientFd, HttpRequest& request,
                              const ConfigParser::ServerConfig& config,
                              const LocationConfig& locConfig,
                              const std::string& effectiveRoot,
                              bool isHead) {
    std::string cgiPassValue = locConfig.getCgiPass();

    std::cerr << "DEBUG[CGI]: Starting CGI for client " << clientFd << " method='" << request.getMethod() 
              << "' path='" << request.getPath() << "' bodyLen=" << request.getBody().size() << std::endl;

    // Map the requested URI to a filesystem path
    std::string mappedScriptPath = resolvePath(config, effectiveRoot, request.getPath());
    std::string scriptFilename = mappedScriptPath;
    std::string execPath = cgiPassValue.empty() ? mappedScriptPath : cgiPassValue;

    // Check executable availability
    if (execPath.empty() || access(execPath.c_str(), X_OK) != 0) {
        std::cerr << "CGI executable not found or not executable: " << execPath << std::endl;
        return false;
    }

    int pipe_in[2];
    int pipe_out[2];

    if (pipe(pipe_in) == -1 || pipe(pipe_out) == -1) {
        std::cerr << "Pipe failed: " << strerror(errno) << std::endl;
        return false;
    }

    pid_t pid = fork();
    if (pid == -1) {
        std::cerr << "Fork failed: " << strerror(errno) << std::endl;
        close(pipe_in[0]); close(pipe_in[1]);
        close(pipe_out[0]); close(pipe_out[1]);
        return false;
    }

    if (pid == 0) { // Child process
        close(pipe_in[1]);
        close(pipe_out[0]);

        if (dup2(pipe_in[0], STDIN_FILENO) == -1) {
            std::cerr << "dup2 stdin failed: " << strerror(errno) << std::endl;
            exit(EXIT_FAILURE);
        }
        close(pipe_in[0]);

        if (dup2(pipe_out[1], STDOUT_FILENO) == -1) {
            std::cerr << "dup2 stdout failed: " << strerror(errno) << std::endl;
            exit(EXIT_FAILURE);
        }
        close(pipe_out[1]);
        
        std::vector<char*> cgiEnv = createCgiEnv(request, config, locConfig, scriptFilename);
        
        char* argv[3];
        if (!cgiPassValue.empty()) {
            argv[0] = strdup(execPath.c_str());
            argv[1] = NULL;
            argv[2] = NULL;
        } else {
            argv[0] = strdup(execPath.c_str());
            argv[1] = NULL;
            argv[2] = NULL;
        }

        execve(argv[0], argv, &cgiEnv[0]);
        
        std::cerr << "Execve failed for " << argv[0] << ": " << strerror(errno) << std::endl;
        freeCgiEnv(cgiEnv);
        free(argv[0]);
        if (argv[1]) free(argv[1]);
        exit(EXIT_FAILURE);

    } else { // Parent process
        close(pipe_in[0]);
        close(pipe_out[1]);

        // Set pipes to non-blocking
        int flags_in = fcntl(pipe_in[1], F_GETFL, 0);
        int flags_out = fcntl(pipe_out[0], F_GETFL, 0);
        fcntl(pipe_in[1], F_SETFL, flags_in | O_NONBLOCK);
        fcntl(pipe_out[0], F_SETFL, flags_out | O_NONBLOCK);

        // Create CGI state
        CgiState& cgi = cgiStates[clientFd];
        cgi.pid = pid;
        cgi.pipe_in = pipe_in[1];
        cgi.pipe_out = pipe_out[0];
        cgi.bodyToWrite = request.getBody();
        cgi.bodyWritten = 0;
        cgi.cgiOutput.clear();
        cgi.writeComplete = (request.getMethod() != "POST" || cgi.bodyToWrite.empty());
        cgi.readComplete = false;
        cgi.startTime = time(NULL);
        cgi.lastIO = time(NULL);
        cgi.request = request;
        cgi.config = &config;
        cgi.locConfig = locConfig;
        cgi.effectiveRoot = effectiveRoot;
        cgi.isHead = isHead;

        std::cerr << "DEBUG[CGI]: Started pid=" << pid << " for client " << clientFd << std::endl;
        return true;
                }
    return false;
            }
            
// Handle writing to CGI stdin
void Server::handleCgiWrite(int clientFd, CgiState& cgi) {
    if (cgi.writeComplete) return;

    ssize_t written = write(cgi.pipe_in, cgi.bodyToWrite.c_str() + cgi.bodyWritten, 
                           cgi.bodyToWrite.length() - cgi.bodyWritten);
    if (written > 0) {
        cgi.bodyWritten += written;
        cgi.lastIO = time(NULL);
        
        if (cgi.bodyWritten >= cgi.bodyToWrite.length()) {
            close(cgi.pipe_in);
            cgi.pipe_in = -1;
            cgi.writeComplete = true;
            std::cerr << "DEBUG[CGI]: Client " << clientFd << " stdin closed after " << cgi.bodyWritten << " bytes" << std::endl;
        }
    } else {
        return; // Not ready; try again when selectable
    }
            }
            
// Handle reading from CGI stdout
void Server::handleCgiRead(int clientFd, CgiState& cgi) {
    if (cgi.readComplete) return;

    char buffer[16384];
    ssize_t bytesRead = read(cgi.pipe_out, buffer, sizeof(buffer));
    if (bytesRead > 0) {
        cgi.cgiOutput.append(buffer, bytesRead);
        cgi.lastIO = time(NULL);
    } else if (bytesRead == 0) {
        // CGI finished writing
        close(cgi.pipe_out);
        cgi.pipe_out = -1;
        cgi.readComplete = true;
        std::cerr << "DEBUG[CGI]: Client " << clientFd << " stdout EOF, output=" << cgi.cgiOutput.size() << " bytes" << std::endl;
    } else if (bytesRead < 0) {
        return; // No data ready; try again later
    }
}

// Finalize CGI request and generate response
void Server::finalizeCgiRequest(int clientFd, CgiState& cgi, int status, std::string& responseBuffer) {
    // Close any remaining pipes
    if (cgi.pipe_in != -1) {
        close(cgi.pipe_in);
        cgi.pipe_in = -1;
    }
    if (cgi.pipe_out != -1) {
        close(cgi.pipe_out);
        cgi.pipe_out = -1;
    }

    std::cerr << "DEBUG[CGI]: Finalizing client " << clientFd << " WIFEXITED=" << WIFEXITED(status) 
              << " WEXITSTATUS=" << (WIFEXITED(status) ? WEXITSTATUS(status) : -1) 
              << " output_size=" << cgi.cgiOutput.size() << std::endl;

    HttpResponse response;

        if (WIFEXITED(status) && WEXITSTATUS(status) == EXIT_SUCCESS) {
        // Parse CGI output
        size_t headerEndPos = cgi.cgiOutput.find("\r\n\r\n");
        if (headerEndPos == std::string::npos) {
            headerEndPos = cgi.cgiOutput.find("\n\n");
            if (headerEndPos == std::string::npos) {
                std::cerr << "CGI output format error for client " << clientFd << std::endl;
                serveErrorPage(response, 500, *cgi.config);
                responseBuffer = response.generateResponse(cgi.isHead);
                    return;
                }
            headerEndPos += 2;
            } else {
            headerEndPos += 4;
            }

        std::string cgiHeadersStr = cgi.cgiOutput.substr(0, headerEndPos);
        std::string cgiBodyStr = cgi.cgiOutput.substr(headerEndPos);

            response.setStatus(200);

            std::istringstream headerStream(cgiHeadersStr);
            std::string headerLine;
            bool contentTypeSet = false;
            while (std::getline(headerStream, headerLine)) {
                if (headerLine.empty() || headerLine == "\r") continue;
                if (!headerLine.empty() && headerLine[headerLine.length() - 1] == '\r') {
                    headerLine.erase(headerLine.length() - 1);
                }

                size_t colonPos = headerLine.find(':');
                if (colonPos != std::string::npos) {
                    std::string headerName = headerLine.substr(0, colonPos);
                    std::string headerValue = headerLine.substr(colonPos + 1);
                    size_t first = headerValue.find_first_not_of(" \t");
                    if (std::string::npos == first) headerValue = ""; else {
                        size_t last = headerValue.find_last_not_of(" \t");
                        headerValue = headerValue.substr(first, (last - first + 1));
                    }
                    if (headerName == "Status") {
                        std::istringstream statusVal(headerValue);
                        int statusCode; statusVal >> statusCode;
                        response.setStatus(statusCode);
                    } else {
                        response.setHeader(headerName, headerValue);
                        if (headerName == "Content-Type") contentTypeSet = true;
                    }
                }
            }
            if (!contentTypeSet) response.setHeader("Content-Type", "text/html");
            response.setBody(cgiBodyStr);

        } else {
        std::cerr << "CGI script execution failed for client " << clientFd << std::endl;
        if (WIFSIGNALED(status)) {
            std::cerr << "CGI killed by signal: " << WTERMSIG(status) << std::endl;
            }
        serveErrorPage(response, 502, *cgi.config);
        }

    responseBuffer = response.generateResponse(cgi.isHead);
}
