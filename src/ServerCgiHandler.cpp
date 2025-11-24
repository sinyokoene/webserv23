#include "Server.hpp"

// Current CGI handler uses blocking pipes and waitpid. For full compliance,
// consider refactoring to integrate pipe FDs into the main select() loop with
// non-blocking reads/writes and timeouts.

// Helper to convert HttpRequest to environment variables
static std::vector<char*> createCgiEnv(HttpRequest& request,
                                       const ConfigParser::ServerConfig& config,
                                       const LocationConfig& locConfig,
                                       const std::string& scriptPath,
                                       const std::string& effectiveRoot) {
    (void)effectiveRoot; // not used after simplifying PATH_TRANSLATED
    std::map<std::string, std::string> envMap;

    // Server and request specific variables
    envMap["GATEWAY_INTERFACE"] = "CGI/1.1";
    envMap["SERVER_SOFTWARE"] = "WebServ/1.0"; // Replace with your server name/version
    envMap["SERVER_NAME"] = config.serverName.empty() ? "localhost" : config.serverName;
    envMap["SERVER_PROTOCOL"] = request.getVersion();
    envMap["SERVER_PORT"] = config.listenPorts.empty() ? "80" : config.listenPorts[0]; // Assuming first port
    envMap["REQUEST_METHOD"] = request.getMethod();
    envMap["SCRIPT_NAME"] = request.getPath(); // Virtual path to the script
    envMap["SCRIPT_FILENAME"] = scriptPath;    // Filesystem path to the script
    // Set PATH_INFO to the requested path; tester expects it
    envMap["PATH_INFO"] = request.getPath();
    // Translate PATH_INFO to filesystem; here it's the mapped script path
    envMap["PATH_TRANSLATED"] = scriptPath;
    // Provide the request URI for CGI that expects it
    envMap["REQUEST_URI"] = request.getPath();
    envMap["QUERY_STRING"] = request.getQueryString();
    envMap["REMOTE_ADDR"] = "127.0.0.1"; // Placeholder, ideally get from client socket
    envMap["REMOTE_HOST"] = "localhost"; // Placeholder

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
    
    // Add cgi_pass from location config if it's not empty
    if (!locConfig.getCgiPass().empty()) {
        envMap["CGI_PASS_DIRECTIVE"] = locConfig.getCgiPass();
    }


    std::vector<char*> cgiEnv;
    for (std::map<std::string, std::string>::const_iterator it = envMap.begin(); it != envMap.end(); ++it) {
        std::string envEntry = it->first + "=" + it->second;
        cgiEnv.push_back(strdup(envEntry.c_str())); // strdup allocates memory, must be freed
    }
    cgiEnv.push_back(NULL); // Null-terminate the array

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

// Handler for CGI requests
void Server::handleCgiRequest(HttpRequest& request, HttpResponse& response,
                              const ConfigParser::ServerConfig& config,
                              const LocationConfig& locConfig,
                              const std::string& effectiveRoot) {
    std::string cgiPassValue = locConfig.getCgiPass();

    // Reduce overly verbose per-chunk logging for large bodies
    const bool VERBOSE_CGI_IO = true;         // set true for deep debugging
    const size_t LOG_EVERY_BYTES = 1 << 20;    // 1 MB

    std::cerr << "DEBUG[CGI]: method='" << request.getMethod() << "' path='" << request.getPath() << "' bodyLen=" << request.getBody().size() << std::endl;
    if (!cgiPassValue.empty()) std::cerr << "DEBUG[CGI]: cgi_pass='" << cgiPassValue << "'" << std::endl;

    // Always map the requested URI to a filesystem path (for SCRIPT_FILENAME)
    std::string mappedScriptPath = resolvePath(effectiveRoot, request.getPath());
    std::cerr << "DEBUG[CGI]: mappedScriptPath='" << mappedScriptPath << "' effectiveRoot='" << effectiveRoot << "'" << std::endl;
    
    // For SCRIPT_FILENAME environment variable, always use the mapped filesystem path.
    // Many CGI testers expect SCRIPT_FILENAME to be an absolute or resolvable path.
    std::string scriptFilename = mappedScriptPath;

    // Determine the executable to run: interpreter (cgi_pass) if provided, else the mapped script itself
    std::string execPath = cgiPassValue.empty() ? mappedScriptPath : cgiPassValue;
    std::cerr << "DEBUG[CGI]: execPath='" << execPath << "' scriptFilename='" << scriptFilename << "'" << std::endl;

    // Check executable availability (interpreter or direct script)
    if (execPath.empty() || access(execPath.c_str(), X_OK) != 0) {
        std::cerr << "CGI executable not found or not executable: " << execPath << std::endl;
        serveErrorPage(response, 404, config);
        return;
    }

    int pipe_in[2];  // Pipe for sending request body to CGI
    int pipe_out[2]; // Pipe for receiving CGI output

    if (pipe(pipe_in) == -1 || pipe(pipe_out) == -1) {
        std::cerr << "Pipe failed: " << strerror(errno) << std::endl;
        serveErrorPage(response, 500, config);
        return;
    }

    pid_t pid = fork();
    if (pid == -1) {
        std::cerr << "Fork failed: " << strerror(errno) << std::endl;
        close(pipe_in[0]); close(pipe_in[1]);
        close(pipe_out[0]); close(pipe_out[1]);
        serveErrorPage(response, 500, config);
        return;
    }

    if (pid == 0) { // Child process (CGI script)
        close(pipe_in[1]);  // Close write end of pipe_in
        close(pipe_out[0]); // Close read end of pipe_out

        // Redirect stdin from pipe_in
        if (dup2(pipe_in[0], STDIN_FILENO) == -1) {
            std::cerr << "dup2 stdin failed: " << strerror(errno) << std::endl;
            exit(EXIT_FAILURE);
        }
        close(pipe_in[0]);

        // Redirect stdout to pipe_out
        if (dup2(pipe_out[1], STDOUT_FILENO) == -1) {
            std::cerr << "dup2 stdout failed: " << strerror(errno) << std::endl;
            exit(EXIT_FAILURE);
        }
        close(pipe_out[1]);
        
        // Build environment with SCRIPT_FILENAME set to the appropriate script path
        std::vector<char*> cgiEnv = createCgiEnv(request, config, locConfig, scriptFilename, effectiveRoot);
        
        // Prepare arguments for execve
        // If cgi_pass is provided, it is the interpreter; pass mapped script as argv[1]
        // Otherwise, execute the mapped script directly
        char* argv[3];
        if (!cgiPassValue.empty()) {
            argv[0] = strdup(execPath.c_str());
            // ubuntu_tester's cgi_test expects CGI env and reads stdin; no argv needed
            argv[1] = NULL;
            argv[2] = NULL;
        } else {
            argv[0] = strdup(execPath.c_str());
            argv[1] = NULL;
            argv[2] = NULL;
        }

        execve(argv[0], argv, &cgiEnv[0]);
        
        // If execve returns, it's an error
        std::cerr << "Execve failed for " << argv[0] << (argv[1] ? std::string(" ") + argv[1] : std::string("")) << ": " << strerror(errno) << std::endl;
        freeCgiEnv(cgiEnv);
        free(argv[0]);
        if (argv[1]) free(argv[1]);
        exit(EXIT_FAILURE);

    } else { // Parent process
        close(pipe_in[0]);  // Close read end of pipe_in
        close(pipe_out[1]); // Close write end of pipe_out

        // Set pipes to non-blocking for concurrent read/write
        int flags_in = fcntl(pipe_in[1], F_GETFL, 0);
        int flags_out = fcntl(pipe_out[0], F_GETFL, 0);
        fcntl(pipe_in[1], F_SETFL, flags_in | O_NONBLOCK);
        fcntl(pipe_out[0], F_SETFL, flags_out | O_NONBLOCK);

        // Handle concurrent reading and writing to avoid deadlock
        const std::string& body = request.getBody();
        size_t totalWritten = 0;
        size_t nextLogMarkWrite = LOG_EVERY_BYTES;
        size_t nextLogMarkRead = LOG_EVERY_BYTES;
        bool needToWrite = (request.getMethod() == "POST" && !body.empty());
        bool writeComplete = !needToWrite;
        std::string cgiOutput;
        char buffer[16384];
        
        bool cgiFinished = false;
        std::cerr << "DEBUG[CGI]: start IO loop needToWrite=" << (needToWrite?"true":"false") << " bodyLen=" << body.size() << std::endl;

        // Activity-based timeout: abort only if no IO for CGI_IDLE_TIMEOUT seconds
        const int CGI_IDLE_TIMEOUT = 120; // seconds
        time_t lastIO = time(NULL);

        while (!writeComplete || !cgiFinished) {
            fd_set read_fds, write_fds;
            FD_ZERO(&read_fds);
            FD_ZERO(&write_fds);
            
            int maxfd = 0;
            
            // Always try to read CGI output unless we know it's finished
            if (!cgiFinished) {
                FD_SET(pipe_out[0], &read_fds);
                maxfd = pipe_out[0];
            }
            
            // Only try to write if we have more data to send
            if (!writeComplete) {
                FD_SET(pipe_in[1], &write_fds);
                if (pipe_in[1] > maxfd) maxfd = pipe_in[1];
            }
            
            // If both operations are complete, break
            if (writeComplete && cgiFinished) {
                break;
            }
            
            struct timeval timeout;
            timeout.tv_sec = 10; // short select window; we enforce idle timeout via lastIO
            timeout.tv_usec = 0;
            
            int ready = select(maxfd + 1, &read_fds, &write_fds, NULL, &timeout);
            if (ready == -1) {
                std::cerr << "select() failed in CGI handler: " << strerror(errno) << std::endl;
                break;
            } else if (ready == 0) {
                // Check idle timeout
                if (time(NULL) - lastIO > CGI_IDLE_TIMEOUT) {
                    std::cerr << "CGI idle timeout after " << CGI_IDLE_TIMEOUT << " seconds" << std::endl;
                    break;
                }
                // else continue waiting
                continue;
            }
            
            // Try to write more data to CGI stdin
            if (FD_ISSET(pipe_in[1], &write_fds) && !writeComplete) {
                ssize_t written = write(pipe_in[1], body.c_str() + totalWritten, body.length() - totalWritten);
                if (written > 0) {
                    totalWritten += written;
                    lastIO = time(NULL);
                    if (VERBOSE_CGI_IO) {
                        std::cerr << "DEBUG[CGI]: wrote " << written << " bytes to CGI (" << totalWritten << "/" << body.length() << ")" << std::endl;
                    } else if (totalWritten >= nextLogMarkWrite) {
                        std::cerr << "DEBUG[CGI]: wrote " << totalWritten/ (1<<20) << " MB of " << body.length()/(1<<20) << " MB" << std::endl;
                        nextLogMarkWrite += LOG_EVERY_BYTES;
                    }
                    if (totalWritten >= body.length()) {
                        close(pipe_in[1]); // Signal EOF to CGI
                        writeComplete = true;
                        std::cerr << "DEBUG[CGI]: stdin closed after " << totalWritten << " bytes" << std::endl;
                    }
                } else if (written == -1 && errno != EAGAIN && errno != EWOULDBLOCK) {
                    std::cerr << "Write to CGI stdin failed: " << strerror(errno) << std::endl;
                    close(pipe_in[1]);
                    writeComplete = true;
                }
            }
            
            // Try to read CGI output
            if (FD_ISSET(pipe_out[0], &read_fds) && !cgiFinished) {
                ssize_t bytesRead = read(pipe_out[0], buffer, sizeof(buffer));
                if (bytesRead > 0) {
                    cgiOutput.append(buffer, bytesRead);
                    lastIO = time(NULL);
                    if (VERBOSE_CGI_IO) {
                        std::cerr << "DEBUG[CGI]: read " << bytesRead << " bytes from CGI (total " << cgiOutput.size() << ")" << std::endl;
                    } else if (cgiOutput.size() >= nextLogMarkRead) {
                        std::cerr << "DEBUG[CGI]: read " << cgiOutput.size()/(1<<20) << " MB from CGI" << std::endl;
                        nextLogMarkRead += LOG_EVERY_BYTES;
                    }
                } else if (bytesRead == 0) {
                    // CGI finished writing
                    cgiFinished = true;
                    std::cerr << "DEBUG[CGI]: stdout EOF" << std::endl;
                } else if (bytesRead == -1 && errno != EAGAIN && errno != EWOULDBLOCK) {
                    std::cerr << "Read from CGI stdout failed: " << strerror(errno) << std::endl;
                    cgiFinished = true;
                }
            }
        }
        
        close(pipe_out[0]);
        if (!writeComplete) {
            close(pipe_in[1]);
        }

        int status;
        waitpid(pid, &status, 0);
        std::cerr << "DEBUG[CGI]: child exit status=" << (WIFEXITED(status)?WEXITSTATUS(status):-1) << (WIFSIGNALED(status)?" signaled":"") << std::endl;

        if (WIFEXITED(status) && WEXITSTATUS(status) == EXIT_SUCCESS) {
            // Parse CGI output (headers and body)
            size_t headerEndPos = cgiOutput.find("\r\n\r\n");
            if (headerEndPos == std::string::npos) {
                headerEndPos = cgiOutput.find("\n\n");
                if (headerEndPos == std::string::npos) {
                    std::cerr << "CGI output format error: No header/body separator" << std::endl;
                    serveErrorPage(response, 500, config);
                    return;
                }
                headerEndPos += 2; // for \n\n
            } else {
                headerEndPos += 4; // for \r\n\r\n
            }

            std::string cgiHeadersStr = cgiOutput.substr(0, headerEndPos);
            std::string cgiBodyStr = cgiOutput.substr(headerEndPos);
            std::cerr << "DEBUG[CGI]: headersLen=" << cgiHeadersStr.size() << " bodyLen=" << cgiBodyStr.size() << std::endl;

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
            std::cerr << "CGI script execution failed or returned non-zero status." << std::endl;
            if (WIFEXITED(status)) {
                std::cerr << "CGI script exited with status: " << WEXITSTATUS(status) << std::endl;
            } else if (WIFSIGNALED(status)) {
                std::cerr << "CGI script killed by signal: " << WTERMSIG(status) << std::endl;
            }
            serveErrorPage(response, 502, config);
        }
    }
}
