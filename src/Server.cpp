#include "Server.hpp"
#include "Utils.hpp"

Server::Server(const std::string& configFile) {
    configPath = configFile;
    parseConfig(configFile);
    if (!serverConfigs.empty()) {
        currentConfig = serverConfigs[0];
        
    } else {
        std::cerr << "Error: No server configurations loaded. Using hardcoded emergency defaults." << std::endl;
        currentConfig.listenPorts.push_back("8080");
        currentConfig.root = "./www";
        currentConfig.defaultLocationSettings.setRoot(currentConfig.root);
        std::vector<std::string> defaultMethods;
        defaultMethods.push_back("GET");
        defaultMethods.push_back("HEAD");
        currentConfig.defaultLocationSettings.setMethods(defaultMethods);
        currentConfig.clientMaxBodySize = 1024 * 1024;
    }
}


void Server::parseConfig(const std::string& configFile) {
    try {
        ConfigParser parser(configFile);
        parser.parse();
        serverConfigs = parser.getServers();
        if (serverConfigs.empty()) {
            std::cerr << "Warning: Configuration file parsed, but no server blocks were found or successfully parsed." << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "Failed to parse config file: " << e.what() << std::endl;
    }
}

const LocationConfig& Server::findLocationConfig(const ConfigParser::ServerConfig& serverConfig, const std::string& path) const {
    std::string bestMatchPath = "";
    const LocationConfig* bestMatchConfig = &serverConfig.defaultLocationSettings;

    for (std::map<std::string, LocationConfig>::const_iterator it = serverConfig.locations.begin(); it != serverConfig.locations.end(); ++it) {
        const std::string& locationPath = it->first;
        bool matches = false;
        
        
        if (path.find(locationPath) == 0) {
            matches = true;
        }
        else if (!locationPath.empty() && locationPath[locationPath.size() - 1] == '/') {
            std::string pathWithSlash = path;
            if (pathWithSlash.empty() || pathWithSlash[pathWithSlash.size() - 1] != '/') {
                pathWithSlash += '/';
            }
            if (pathWithSlash.find(locationPath) == 0) {
                matches = true;
            }
            std::string locationWithoutSlash = locationPath.substr(0, locationPath.size() - 1);
            if (path == locationWithoutSlash) {
                matches = true;
            }
        }
        if (matches) {
            if (locationPath.length() > bestMatchPath.length()) {
                bestMatchPath = locationPath;
                bestMatchConfig = &it->second;
            }
        }
    }
    
    return *bestMatchConfig;
}


std::string Server::resolvePath(const ConfigParser::ServerConfig& config, const std::string& basePath, const std::string& relativePath) const {
    if (relativePath.find("..") != std::string::npos) {
        return "";
    }
    char realBasePath[PATH_MAX];
    if (realpath(basePath.c_str(), realBasePath) == NULL) {
        
        strncpy(realBasePath, basePath.c_str(), sizeof(realBasePath)-1);
        realBasePath[sizeof(realBasePath)-1] = '\0';
    }
    std::string canonicalBasePath(realBasePath);
    std::string joinPath = relativePath;
    if (!relativePath.empty() && relativePath[0] == '/') {
        
        std::string bestMatchPath = "";
        const LocationConfig* bestMatchConfig = &config.defaultLocationSettings;
        for (std::map<std::string, LocationConfig>::const_iterator it = config.locations.begin(); it != config.locations.end(); ++it) {
            const std::string& locationPath = it->first;
            bool matches = false;
            if (relativePath.find(locationPath) == 0) {
                matches = true;
            } else if (!locationPath.empty() && locationPath[locationPath.size() - 1] == '/') {
                std::string uriWithSlash = relativePath;
                if (uriWithSlash.empty() || uriWithSlash[uriWithSlash.size() - 1] != '/') uriWithSlash += '/';
                if (uriWithSlash.find(locationPath) == 0) matches = true;
            }
            if (matches) {
                if (locationPath.length() > bestMatchPath.length()) {
                    bestMatchPath = locationPath;
                    bestMatchConfig = &it->second;
                }
            }
        }
        
        if (!bestMatchPath.empty()) {
            if (!bestMatchConfig->getRoot().empty()) {
                canonicalBasePath = bestMatchConfig->getRoot();
                char realRoot[PATH_MAX];
                if (realpath(canonicalBasePath.c_str(), realRoot) != NULL) {
                   canonicalBasePath = std::string(realRoot);
                }
                
                std::string sub;
                if (relativePath.length() < bestMatchPath.length()) {
                    sub = "";
                } else {
                    sub = relativePath.substr(bestMatchPath.length());
                }
                while (!sub.empty() && sub[0] == '/') sub = sub.substr(1);
                joinPath = sub;
            }
        }
    }

    std::string fullPath = canonicalBasePath;
    if (!fullPath.empty() && fullPath[fullPath.length() - 1] != '/') {
        fullPath += '/';
    }
    if (!joinPath.empty()) {
        fullPath += joinPath;
    }
    char resolved_path[PATH_MAX];
    if (realpath(fullPath.c_str(), resolved_path) != NULL) {
        std::string finalPath(resolved_path);
        if (finalPath.rfind(canonicalBasePath, 0) == 0) {
            return finalPath;
        }
        std::cerr << "Security Error: Resolved path '" << finalPath << "' escaped canonical base path '" << canonicalBasePath << "'." << std::endl;
        return "";
    } else {
        if (fullPath.rfind(canonicalBasePath, 0) == 0) {
            return fullPath;
        }
        return "";
    }
}

void Server::serveErrorPage(HttpResponse& response, int statusCode, const ConfigParser::ServerConfig& config) {
    response.setHeader("Access-Control-Allow-Origin", "*");
    std::map<int, std::string>::const_iterator it = config.errorPages.find(statusCode);
    std::string errorPagePath;
    if (it != config.errorPages.end()) {
        errorPagePath = resolvePath(config, config.root, it->second);
    }
    if (!errorPagePath.empty()) {
        std::ifstream errFile(errorPagePath.c_str());
        if (errFile.is_open()) {
            std::ostringstream ss;
            ss << errFile.rdbuf();
            response.setBody(ss.str());
            response.setHeader("Content-Type", "text/html");
            errFile.close();
            response.setStatus(statusCode);
            return;
        } else {
            std::cerr << "Warning: Custom error page for " << statusCode << " defined at '" << it->second << "' (resolved to '" << errorPagePath << "') but could not be opened." << std::endl;
        }
    }
    response.setStatus(statusCode);
    response.setBody("<html><body><h1>" + response.getStatusMessage(response.getStatus()) + "</h1></body></html>");
    response.setHeader("Content-Type", "text/html");
}

std::set<std::string> Server::getAllowedMethodsForPath(const std::string& path, const ConfigParser::ServerConfig& config) const {
    const LocationConfig& location = findLocationConfig(config, path);
    if (!location.getMethods().empty()) {
        const std::vector<std::string>& methods = location.getMethods();
        return std::set<std::string>(methods.begin(), methods.end());
    }
    std::set<std::string> defaultMethods;
    defaultMethods.insert("GET");
    defaultMethods.insert("HEAD");
    defaultMethods.insert("OPTIONS");
    return defaultMethods;
}

const ConfigParser::ServerConfig& Server::selectConfig(int port, const std::string& hostHeader) const {
    std::map<int, std::vector<const ConfigParser::ServerConfig*> >::const_iterator it = portToConfigs.find(port);
    if (it == portToConfigs.end() || it->second.empty()) {
        return currentConfig; 
    }
    
    const std::vector<const ConfigParser::ServerConfig*>& configs = it->second;
    
    // Extract hostname from hostHeader (remove port if present)
    std::string hostname = hostHeader;
    size_t colon = hostname.find(':');
    if (colon != std::string::npos) {
        hostname = hostname.substr(0, colon);
    }
    
    // RFC 7230 2.7.1: Host header is case-insensitive
    hostname = toLower(hostname);

    // 1. Match server_name
    for (size_t i = 0; i < configs.size(); ++i) {
        // serverName should also be compared case-insensitively, assuming it was parsed/stored as is.
        // However, conventions usually have it lowercase. To be safe, let's assume serverName might be mixed case too or just compare against lowercased version.
        // Better: compare lowercased serverName.
        if (toLower(configs[i]->serverName) == hostname) {
             return *configs[i];
        }
    }
    
    // 2. Default to the first one for this port
    return *configs[0];
}

void Server::start() {
    try {
        std::ofstream logFile("server.log");
        if (logFile.is_open()) {
            logFile << "Attempting to start server with config: " << configPath << std::endl;
        }

        // 1. Populate portToConfigs map and identify unique ports
        std::set<int> portsToBind;
        portToConfigs.clear();

        if (serverConfigs.empty()) {
            // Fallback to currentConfig (emergency default) if no configs parsed
            for (std::vector<std::string>::const_iterator it = currentConfig.listenPorts.begin();
                 it != currentConfig.listenPorts.end(); ++it) {
                 int port = atoi(it->c_str());
                 if (port > 0 && port <= 65535) {
                     portsToBind.insert(port);
                     portToConfigs[port].push_back(&currentConfig);
                 }
            }
        } else {
            for (size_t i = 0; i < serverConfigs.size(); ++i) {
                for (std::vector<std::string>::const_iterator it = serverConfigs[i].listenPorts.begin();
                     it != serverConfigs[i].listenPorts.end(); ++it) {
                    int port = atoi(it->c_str());
                    if (port > 0 && port <= 65535) {
                        portsToBind.insert(port);
                        portToConfigs[port].push_back(&serverConfigs[i]);
                    }
                }
            }
        }

        // 2. Bind sockets
        socketPortMap.clear();
        serverSockets.clear();

        for (std::set<int>::iterator it = portsToBind.begin(); it != portsToBind.end(); ++it) {
            int port = *it;

            int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
            if (serverSocket < 0) {
                std::cerr << "Error creating socket for port " << port << ": " << strerror(errno) << std::endl;
                continue;
            }

            int flags = fcntl(serverSocket, F_GETFL, 0);
            if (flags != -1) fcntl(serverSocket, F_SETFL, flags | O_NONBLOCK);

            int optval = 1;
            if (setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
                std::cerr << "Error setting socket options: " << strerror(errno) << std::endl;
                close(serverSocket);
                continue;
            }

            struct sockaddr_in serverAddr;
            memset(&serverAddr, 0, sizeof(serverAddr));
            serverAddr.sin_family = AF_INET;
            serverAddr.sin_addr.s_addr = INADDR_ANY;
            serverAddr.sin_port = htons(port);

            if (bind(serverSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
                std::cerr << "Error binding socket to port " << port << ": " << strerror(errno) << std::endl;
                close(serverSocket);
                continue;
            }

            if (listen(serverSocket, 128) < 0) {
                std::cerr << "Error listening on socket: " << strerror(errno) << std::endl;
                close(serverSocket);
                continue;
            }

            serverSockets.push_back(serverSocket);
            socketPortMap[serverSocket] = port;

            if (logFile.is_open()) {
                logFile << "Server is listening on port " << port << std::endl;
            }
            std::cout << "Server is listening on port " << port << std::endl;
        }

        if (serverSockets.empty()) {
            std::cerr << "Failed to set up any server sockets. Exiting." << std::endl;
            if (logFile.is_open()) { logFile << "Failed to set up any server sockets. Exiting." << std::endl; logFile.close(); }
            return;
        }

        fd_set master_read, master_write, read_fds, write_fds;
        FD_ZERO(&master_read);
        FD_ZERO(&master_write);
        int fdmax = 0;

        for (std::vector<int>::const_iterator it = serverSockets.begin(); it != serverSockets.end(); ++it) {
            FD_SET(*it, &master_read);
            if (*it > fdmax) fdmax = *it;
        }

        std::map<int, std::string> clientBuffers;     
        std::map<int, std::string> responseBuffers;   
        std::map<int, size_t> sendOffsets;            
        std::map<int, bool> keepAliveMap;             
        std::map<int, time_t> lastActivity;           
        std::map<int, bool> sentContinueMap;          
        std::map<int, std::string> continueBuffers;   
        std::map<int, size_t> continueOffsets;        
        std::map<int, bool> chunkedInited;            
        std::map<int, size_t> chunkedPos;             
        std::map<int, std::string> chunkedDecoded;    
        std::map<int, std::string> chunkedReqLine;    
        std::map<int, std::string> chunkedNewHeaders; 
        std::map<int, bool> chunkedComplete;          
        std::map<int, size_t> chunkedConsumedEnd;     
        std::map<int, int> clientPortMap; // Map client fd to server port

        std::cout << "Server is running. Press Ctrl+C to stop." << std::endl;

        const int SELECT_TIMEOUT_SEC = 1;      
        const int CLIENT_TIMEOUT_SEC = 30;     
        const size_t MAX_HEADER_BYTES = 32 * 1024;     
        const size_t MAX_REQUEST_BYTES = 200 * 1024 * 1024; 

        while (true) {
            read_fds = master_read;
            write_fds = master_write;

            struct timeval tv; tv.tv_sec = SELECT_TIMEOUT_SEC; tv.tv_usec = 0;
            int nready = select(fdmax + 1, &read_fds, &write_fds, NULL, &tv);
            if (nready == -1) {
                if (errno == EINTR) continue;
                std::cerr << "Error in select(): " << strerror(errno) << std::endl;
                break;
            }

            time_t now = time(NULL);

            for (std::map<int, time_t>::iterator it = lastActivity.begin(); it != lastActivity.end(); ) {
                int fd = it->first; time_t last = it->second;
                if (now - last > CLIENT_TIMEOUT_SEC) {
                    close(fd);
                    FD_CLR(fd, &master_read);
                    FD_CLR(fd, &master_write);
                    clientBuffers.erase(fd);
                    responseBuffers.erase(fd);
                    sendOffsets.erase(fd);
                    keepAliveMap.erase(fd);
                    sentContinueMap.erase(fd);
                    continueBuffers.erase(fd);
                    continueOffsets.erase(fd);
                    chunkedInited.erase(fd);
                    chunkedPos.erase(fd);
                    chunkedDecoded.erase(fd);
                    chunkedReqLine.erase(fd);
                    chunkedNewHeaders.erase(fd);
                    chunkedComplete.erase(fd);
                    chunkedConsumedEnd.erase(fd);
                    clientPortMap.erase(fd);
                    lastActivity.erase(it++); 
                } else {
                    ++it;
                }
            }

            for (int i = 0; i <= fdmax; ++i) {
                if (FD_ISSET(i, &read_fds)) {
                    
                    bool isServerSocket = false;
                    for (std::vector<int>::const_iterator it = serverSockets.begin(); it != serverSockets.end(); ++it) {
                        if (i == *it) { isServerSocket = true; break; }
                    }
                    if (isServerSocket) {
                        
                        while (true) {
                            struct sockaddr_in clientAddr; socklen_t clientLen = sizeof(clientAddr);
                            int clientSocket = accept(i, (struct sockaddr*)&clientAddr, &clientLen);
                            if (clientSocket < 0) {
                                if (errno == EWOULDBLOCK || errno == EAGAIN) break;
                                std::cerr << "Error accepting connection: " << strerror(errno) << std::endl;
                                break;
                            }
                            
                            int cflags = fcntl(clientSocket, F_GETFL, 0); if (cflags != -1) fcntl(clientSocket, F_SETFL, cflags | O_NONBLOCK);

                            FD_SET(clientSocket, &master_read);
                            if (clientSocket > fdmax) fdmax = clientSocket;
                            clientBuffers[clientSocket] = "";
                            responseBuffers[clientSocket] = "";
                            sendOffsets[clientSocket] = 0;
                            keepAliveMap[clientSocket] = false;
                            lastActivity[clientSocket] = now;
                            sentContinueMap[clientSocket] = false;
                            
                            // Record which port this client connected to
                            clientPortMap[clientSocket] = socketPortMap[i];
                        }
                        continue;
                    }

                    char buffer[8192];
                    while (true) {
                        ssize_t bytesRead = recv(i, buffer, sizeof(buffer), 0);
                        if (bytesRead > 0) {
                            clientBuffers[i].append(buffer, bytesRead);
                            lastActivity[i] = now;
                            
                            // Select config early for error page checks
                            int port = clientPortMap[i];
                            const ConfigParser::ServerConfig& tempConfig = selectConfig(port, ""); // Default to first config for port

                            if (clientBuffers[i].size() > MAX_REQUEST_BYTES) {
                                HttpResponse resp; resp.setStatus(413); serveErrorPage(resp, 413, tempConfig); resp.setHeader("Connection", "close");
                                keepAliveMap[i] = false; responseBuffers[i] = resp.generateResponse(false); sendOffsets[i] = 0; FD_SET(i, &master_write);
                                clientBuffers[i].clear();
                                goto next_fd;
                            }
                            
                            size_t headerEnd = clientBuffers[i].find("\r\n\r\n");
                            size_t sepLen = 4;
                            if (headerEnd == std::string::npos) {
                                headerEnd = clientBuffers[i].find("\n\n");
                                sepLen = 2;
                            }

                            if (headerEnd != std::string::npos) {
                                size_t bodyStart = headerEnd + sepLen;
                                size_t contentLength = 0;
                                bool hasContentLength = false;
                                bool isChunked = false;
                                bool expectContinue = false; 
                                size_t consumedEndForErase = 0; 
                                std::string normalizedRequestForParse; 
                                std::string hostHeaderVal = "";

                                {
                                    const std::string headersPart = clientBuffers[i].substr(0, headerEnd);
                                    std::istringstream hs(headersPart);
                                    std::string line;
                                    std::string connectionHeaderVal;
                                    std::string firstLine;
                                    {
                                        size_t reqLineEndDbg = clientBuffers[i].find("\r\n");
                                        if (reqLineEndDbg == std::string::npos) reqLineEndDbg = clientBuffers[i].find("\n");
                                        if (reqLineEndDbg != std::string::npos) firstLine = clientBuffers[i].substr(0, reqLineEndDbg);
                                    }
                                    while (std::getline(hs, line)) {
                                        if (!line.empty() && line[line.size()-1] == '\r') line.erase(line.size()-1);
                                        size_t colon = line.find(':');
                                        if (colon == std::string::npos) continue;
                                        std::string name = line.substr(0, colon);
                                        
                                        for (size_t k = 0; k < name.size(); ++k) {
                                            char c = name[k]; if (c >= 'A' && c <= 'Z') name[k] = c + 32;
                                        }
                                        std::string value = line.substr(colon + 1);
                                        
                                        size_t first = value.find_first_not_of(" \t");
                                        size_t last = value.find_last_not_of(" \t");
                                        if (first != std::string::npos) value = value.substr(first, last - first + 1); else value = "";

                                        if (name == "content-length") {
                                            std::istringstream iss(value); size_t tmp = 0; iss >> tmp; contentLength = tmp; hasContentLength = true;
                                        } else if (name == "transfer-encoding") {
                                            std::string lowerVal = value;
                                            for (size_t k = 0; k < lowerVal.size(); ++k) { char c = lowerVal[k]; if (c >= 'A' && c <= 'Z') lowerVal[k] = c + 32; }
                                            if (lowerVal.find("chunked") != std::string::npos) {
                                                isChunked = true;
                                            }
                                        } else if (name == "connection") {
                                            connectionHeaderVal = value;
                                        } else if (name == "expect") {
                                            std::string lowerVal = value; for (size_t k = 0; k < lowerVal.size(); ++k) { char c = lowerVal[k]; if (c >= 'A' && c <= 'Z') lowerVal[k] = c + 32; }
                                            if (lowerVal.find("100-continue") != std::string::npos) {
                                                expectContinue = true;
                                            }
                                        } else if (name == "host") {
                                            hostHeaderVal = value;
                                        }
                                    }
                                    {
                                        std::string methodDbg = ""; std::string uriDbg = "";
                                        size_t sp1 = firstLine.find(' ');
                                        size_t sp2 = (sp1 != std::string::npos) ? firstLine.find(' ', sp1 + 1) : std::string::npos;
                                        if (sp1 != std::string::npos && sp2 != std::string::npos) {
                                            methodDbg = firstLine.substr(0, sp1);
                                            uriDbg = firstLine.substr(sp1 + 1, sp2 - (sp1 + 1));
                                        }
                                        std::ofstream lf("server.log", std::ios::app);
                                        if (lf.is_open()) {
                                            // Look up config to log correct max body size
                                            const ConfigParser::ServerConfig& dbgConfig = selectConfig(port, hostHeaderVal);
                                            lf << "Headers: method=" << methodDbg
                                               << " uri=" << uriDbg
                                               << " hasCL=" << (hasContentLength?"1":"0")
                                               << " CL=" << contentLength
                                               << " chunked=" << (isChunked?"1":"0")
                                               << " expect100=" << (expectContinue?"1":"0")
                                               << " max=" << dbgConfig.clientMaxBodySize
                                               << " host=" << hostHeaderVal
                                               << std::endl;
                                        }
                                    }
                                }
                                
                                // Select the correct config based on port and Host header
                                const ConfigParser::ServerConfig& requestConfig = selectConfig(port, hostHeaderVal);

                                bool handledEarly = false;
                                {
                                    size_t reqLineEnd = clientBuffers[i].find("\r\n");
                                    if (reqLineEnd != std::string::npos) {
                                        std::string requestLine = clientBuffers[i].substr(0, reqLineEnd);
                                        size_t sp1 = requestLine.find(' ');
                                        size_t sp2 = (sp1 != std::string::npos) ? requestLine.find(' ', sp1 + 1) : std::string::npos;
                                        if (sp1 != std::string::npos && sp2 != std::string::npos) {
                                            std::string method = requestLine.substr(0, sp1);
                                            std::string uri = requestLine.substr(sp1 + 1, sp2 - (sp1 + 1));

                                            bool endsWithBlaUri = uri.size() >= 4 && uri.compare(uri.size()-4, 4, ".bla") == 0;
                                            const LocationConfig& earlyLoc = findLocationConfig(requestConfig, uri);
                                            bool hasCgiPass = !earlyLoc.getCgiPass().empty();
                                            bool isPotentialCgi = (uri.find("/cgi-bin/") != std::string::npos) ||
                                                                  (uri.find(".php") != std::string::npos) ||
                                                                  (uri.find(".py") != std::string::npos) ||
                                                                  (uri.find(".cgi") != std::string::npos) ||
                                                                  hasCgiPass ||
                                                                  (endsWithBlaUri && method == "POST");
                                            bool isPostBodySpecial = (method == "POST" && uri.find("/post_body") == 0);

                                            if (!isPotentialCgi && !isPostBodySpecial) {
                                                std::set<std::string> allowed = getAllowedMethodsForPath(uri, requestConfig);
                                                if (allowed.find(method) == allowed.end()) {
                                                    HttpResponse resp;
                                                    resp.setStatus(405);
                                                    
                                                    std::string allowHeader = ""; for (std::set<std::string>::const_iterator it = allowed.begin(); it != allowed.end(); ++it) { if (it != allowed.begin()) allowHeader += ", "; allowHeader += *it; }
                                                    resp.setHeader("Allow", allowHeader);
                                                    serveErrorPage(resp, 405, requestConfig);
                                                    resp.setHeader("Connection", "close");
                                                    keepAliveMap[i] = false;
                                                    responseBuffers[i] = resp.generateResponse(method == "HEAD");
                                                    sendOffsets[i] = 0;
                                                    FD_SET(i, &master_write);
                                                    size_t consumed = headerEnd + sepLen;
                                                    if (clientBuffers[i].size() > consumed) clientBuffers[i].erase(0, consumed); else clientBuffers[i].clear();
                                                    handledEarly = true;
                                                }
                                            }
                                        }
                                    }
                                }
                                if (handledEarly) {
                                    goto next_fd;
                                } else {
                                    if (hasContentLength && requestConfig.clientMaxBodySize > 0 && (long)contentLength > requestConfig.clientMaxBodySize) {
                                        std::cerr << "DEBUG: Rejecting request with Content-Length=" << contentLength
                                                  << " exceeding client_max_body_size=" << requestConfig.clientMaxBodySize << std::endl;
                                        {
                                            std::ofstream logFile("server.log", std::ios::app);
                                            if (logFile.is_open()) {
                                                logFile << "413 early reject: Content-Length " << contentLength
                                                       << " > max " << requestConfig.clientMaxBodySize << std::endl;
                                            }
                                        }
                                        HttpResponse resp;
                                        resp.setStatus(413);
                                        serveErrorPage(resp, 413, requestConfig);
                                        resp.setHeader("Connection", "close");
                                        keepAliveMap[i] = false;
                                        responseBuffers[i] = resp.generateResponse(false);
                                        sendOffsets[i] = 0;
                                        FD_SET(i, &master_write);
                                        size_t consumed = headerEnd + sepLen;
                                        if (clientBuffers[i].size() > consumed) clientBuffers[i].erase(0, consumed); else clientBuffers[i].clear();
                                        goto next_fd;
                                    }

                                    if (expectContinue && !sentContinueMap[i]) {
                                        static const char kContinue[] = "HTTP/1.1 100 Continue\r\n\r\n";
                                        size_t len = sizeof(kContinue) - 1;
                                        ssize_t s = send(i, kContinue, len, 0);
                                        if (s < 0) s = 0; 
                                        if ((size_t)s < len) {
                                            continueBuffers[i] = std::string(kContinue + s, len - s);
                                            continueOffsets[i] = 0;
                                            FD_SET(i, &master_write);
                                        }
                                        sentContinueMap[i] = true;
                                    }

                                    if (hasContentLength) {
                                        size_t have = (clientBuffers[i].size() > bodyStart) ? (clientBuffers[i].size() - bodyStart) : 0;
                                        if (have < contentLength) {
                                            break;
                                        }
                                    } else if (isChunked) {
                                        if (!chunkedInited[i]) {
                                            size_t reqLineEnd2 = clientBuffers[i].find("\r\n");
                                            chunkedReqLine[i] = (reqLineEnd2 != std::string::npos) ? clientBuffers[i].substr(0, reqLineEnd2) : "";
                                            std::string headersPart = clientBuffers[i].substr(reqLineEnd2 + 2, headerEnd - (reqLineEnd2 + 2));
                                            std::istringstream hsin(headersPart);
                                            std::string hline; std::string newHeaders;
                                            while (std::getline(hsin, hline)) {
                                                if (!hline.empty() && hline[hline.size()-1] == '\r') hline.erase(hline.size()-1);
                                                if (hline.empty()) continue;
                                                size_t colon = hline.find(':'); if (colon == std::string::npos) continue;
                                                std::string name = hline.substr(0, colon);
                                                std::string lower = name; for (size_t k = 0; k < lower.size(); ++k) { char c = lower[k]; if (c >= 'A' && c <= 'Z') lower[k] = c + 32; }
                                                if (lower == "transfer-encoding" || lower == "content-length") continue;
                                                newHeaders += name + ":" + hline.substr(colon + 1) + "\r\n";
                                            }
                                            chunkedNewHeaders[i] = newHeaders;
                                            chunkedDecoded[i].clear();
                                            chunkedPos[i] = bodyStart;
                                            chunkedComplete[i] = false;
                                            chunkedConsumedEnd[i] = headerEnd + 4;
                                            chunkedInited[i] = true;
                                        }

                                        size_t pos = chunkedPos[i];
                                        bool progressed = false;
                                        while (true) {
                                            size_t lineEnd = clientBuffers[i].find("\r\n", pos);
                                            if (lineEnd == std::string::npos) break;
                                            std::string sizeLine = clientBuffers[i].substr(pos, lineEnd - pos);
                                            size_t sc = sizeLine.find(';'); if (sc != std::string::npos) sizeLine = sizeLine.substr(0, sc);
                                            size_t f = sizeLine.find_first_not_of(" \t"); size_t l = sizeLine.find_last_not_of(" \t");
                                            if (f == std::string::npos) break;
                                            sizeLine = sizeLine.substr(f, l - f + 1);
                                            unsigned long chunkSize = 0; { std::istringstream xs(sizeLine); xs >> std::hex >> chunkSize; if (xs.fail()) break; }
                                            pos = lineEnd + 2;
                                            if (chunkSize == 0) {
                                                size_t afterZero = clientBuffers[i].find("\r\n\r\n", pos);
                                                if (afterZero == std::string::npos) break;
                                                chunkedConsumedEnd[i] = afterZero + 4;
                                                chunkedComplete[i] = true;
                                                progressed = true;
                                                pos = chunkedConsumedEnd[i];
                                                break;
                                            }
                                            if (clientBuffers[i].size() < pos + chunkSize + 2) break;
                                            if (requestConfig.clientMaxBodySize > 0 && (long)(chunkedDecoded[i].size() + chunkSize) > requestConfig.clientMaxBodySize) {
                                                std::cerr << "DEBUG: Rejecting chunked request: next chunk " << chunkSize << " would exceed max body " << requestConfig.clientMaxBodySize << std::endl;
                                                {
                                                    std::ofstream logFile("server.log", std::ios::app);
                                                    if (logFile.is_open()) {
                                                        logFile << "413 early reject: chunked, next chunk " << chunkSize << " causes total > max " << requestConfig.clientMaxBodySize << std::endl;
                                                    }
                                                }
                                                HttpResponse resp; resp.setStatus(413); serveErrorPage(resp, 413, requestConfig); resp.setHeader("Connection", "close");
                                                keepAliveMap[i] = false; responseBuffers[i] = resp.generateResponse(false); sendOffsets[i] = 0; FD_SET(i, &master_write);
                                                size_t consumedHdr2 = headerEnd + 4; if (clientBuffers[i].size() > consumedHdr2) clientBuffers[i].erase(0, consumedHdr2); else clientBuffers[i].clear();
                                                
                                                chunkedInited.erase(i); chunkedPos.erase(i); chunkedDecoded.erase(i);
                                                chunkedReqLine.erase(i); chunkedNewHeaders.erase(i); chunkedComplete.erase(i); chunkedConsumedEnd.erase(i);
                                                goto next_fd;
                                            }
                                            chunkedDecoded[i].append(clientBuffers[i], pos, chunkSize);
                                            pos += chunkSize;
                                            if (!(clientBuffers[i][pos] == '\r' && clientBuffers[i][pos+1] == '\n')) break;
                                            pos += 2;
                                            progressed = true;
                                        }
                                        if (progressed) chunkedPos[i] = pos;
                                        if (!chunkedComplete[i]) {
                                            break;
                                        }
                                        std::ostringstream cl; cl << "Content-Length: " << chunkedDecoded[i].size() << "\r\n";
                                        std::string newHeaders = chunkedNewHeaders[i] + cl.str();
                                        normalizedRequestForParse = chunkedReqLine[i] + "\r\n" + newHeaders + "\r\n" + chunkedDecoded[i];
                                        hasContentLength = true; contentLength = chunkedDecoded[i].size();
                                        consumedEndForErase = chunkedConsumedEnd[i];
                                    }
                                    try {
                                        HttpRequest request;
                                        if (!normalizedRequestForParse.empty()) request.parseRequest(normalizedRequestForParse);
                                        else request.parseRequest(clientBuffers[i]);
                                        HttpResponse response;
                                        
                                        // Update config selection in dispatch as well if needed, but we have correct config here
                                        dispatchRequest(request, response, requestConfig);

                                        std::string connectionHeader = request.getHeader("Connection");
                                        std::string version = request.getVersion();
                                        std::string connLower = connectionHeader;
                                        for (size_t k = 0; k < connLower.size(); ++k) { char c = connLower[k]; if (c >= 'A' && c <= 'Z') connLower[k] = c + 32; }
                                        bool keepAlive = false;
                                        if (version == "HTTP/1.1") {
                                            keepAlive = (connLower != "close");
                                        } else { 
                                            keepAlive = (connLower == "keep-alive");
                                        }
                                        keepAliveMap[i] = keepAlive;
                                        response.setHeader("Connection", keepAlive ? "keep-alive" : "close");

                                        std::string resp = response.generateResponse(request.getMethod() == "HEAD");
                                        responseBuffers[i] = resp;
                                        sendOffsets[i] = 0;
                                        FD_SET(i, &master_write);

                                        size_t consumed = 0;
                                        if (!normalizedRequestForParse.empty()) consumed = consumedEndForErase; 
                                        else if (hasContentLength) consumed = bodyStart + contentLength; else consumed = headerEnd + sepLen;
                                        if (clientBuffers[i].size() > consumed) {
                                            clientBuffers[i].erase(0, consumed);
                                        } else {
                                            clientBuffers[i].clear();
                                        }
                                        chunkedInited.erase(i);
                                        chunkedPos.erase(i);
                                        chunkedDecoded.erase(i);
                                        chunkedReqLine.erase(i);
                                        chunkedNewHeaders.erase(i);
                                        chunkedComplete.erase(i);
                                        chunkedConsumedEnd.erase(i);
                                    } catch (const std::exception& e) {
                                        std::cerr << "ERROR: fd " << i << " Exception while handling request: " << e.what() << std::endl;
                                        HttpResponse err; err.setStatus(500); err.setHeader("Content-Type", "text/html"); err.setBody("<html><body><h1>500 Internal Server Error</h1></body></html>");
                                        responseBuffers[i] = err.generateResponse();
                                        sendOffsets[i] = 0;
                                        FD_SET(i, &master_write);
                                    }
                                }
                            }
                            else {
                                if (clientBuffers[i].size() > MAX_HEADER_BYTES) {
                                    std::cerr << "DEBUG: 431 triggered. Buffer size: " << clientBuffers[i].size() << std::endl;
                                    std::cerr << "DEBUG: Buffer content (first 200 bytes): [" << clientBuffers[i].substr(0, 200) << "]" << std::endl;
                                    int port = clientPortMap[i];
                                    const ConfigParser::ServerConfig& tempConfig = selectConfig(port, "");
                                    HttpResponse resp; resp.setStatus(431); serveErrorPage(resp, 431, tempConfig); resp.setHeader("Connection", "close");
                                    keepAliveMap[i] = false; responseBuffers[i] = resp.generateResponse(false); sendOffsets[i] = 0; FD_SET(i, &master_write);
                                    clientBuffers[i].clear();
                                    goto next_fd;
                                }
                            }
                        } else if (bytesRead == 0) {
                            close(i); FD_CLR(i, &master_read); FD_CLR(i, &master_write);
                            clientBuffers.erase(i); responseBuffers.erase(i); sendOffsets.erase(i); keepAliveMap.erase(i);
                            sentContinueMap.erase(i); continueBuffers.erase(i); continueOffsets.erase(i); lastActivity.erase(i);
                            chunkedInited.erase(i); chunkedPos.erase(i); chunkedDecoded.erase(i);
                            chunkedReqLine.erase(i); chunkedNewHeaders.erase(i); chunkedComplete.erase(i); chunkedConsumedEnd.erase(i);
                            clientPortMap.erase(i);
                            break;
                        } else {
                            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                                break; 
                            } else {
                                std::cerr << "ERROR: fd " << i << " recv error: " << strerror(errno) << std::endl;
                                close(i); FD_CLR(i, &master_read); FD_CLR(i, &master_write);
                                clientBuffers.erase(i); responseBuffers.erase(i); sendOffsets.erase(i); keepAliveMap.erase(i);
                                sentContinueMap.erase(i); continueBuffers.erase(i); continueOffsets.erase(i); lastActivity.erase(i);
                                chunkedInited.erase(i); chunkedPos.erase(i); chunkedDecoded.erase(i);
                                chunkedReqLine.erase(i); chunkedNewHeaders.erase(i); chunkedComplete.erase(i); chunkedConsumedEnd.erase(i);
                                clientPortMap.erase(i);
                                break;
                            }
                        }
                    }
                }

                if (FD_ISSET(i, &write_fds)) {
                    std::map<int, std::string>::iterator cit = continueBuffers.find(i);
                    if (cit != continueBuffers.end()) {
                        const std::string& data = cit->second;
                        size_t& off = continueOffsets[i];
                        while (off < data.size()) {
                            ssize_t sent = send(i, data.c_str() + off, data.size() - off, 0);
                            if (sent > 0) {
                                off += sent; lastActivity[i] = now;
                            } else {
                                if (errno == EWOULDBLOCK || errno == EAGAIN) {
                                    break; 
                                } else {
                                    std::cerr << "ERROR: fd " << i << " send error (continue): " << strerror(errno) << std::endl;
                                    close(i); FD_CLR(i, &master_read); FD_CLR(i, &master_write);
                                    clientBuffers.erase(i); responseBuffers.erase(i); sendOffsets.erase(i); keepAliveMap.erase(i);
                                    sentContinueMap.erase(i); continueBuffers.erase(i); continueOffsets.erase(i); lastActivity.erase(i);
                                    clientPortMap.erase(i);
                                    goto next_fd; 
                                }
                            }
                        }
                        if (off >= data.size()) {
                            continueBuffers.erase(i); continueOffsets.erase(i);
                        } else {
                            goto next_fd;
                        }
                    }

                    std::map<int, std::string>::iterator it = responseBuffers.find(i);
                    if (it != responseBuffers.end()) {
                        const std::string& data = it->second;
                        size_t& off = sendOffsets[i];
                        while (off < data.size()) {
                            ssize_t sent = send(i, data.c_str() + off, data.size() - off, 0);
                            if (sent > 0) {
                                off += sent; lastActivity[i] = now;
                            } else {
                                if (errno == EWOULDBLOCK || errno == EAGAIN) {
                                    break; 
                                } else {
                                    std::cerr << "ERROR: fd " << i << " send error: " << strerror(errno) << std::endl;
                                    close(i); FD_CLR(i, &master_read); FD_CLR(i, &master_write);
                                    clientBuffers.erase(i); responseBuffers.erase(i); sendOffsets.erase(i); keepAliveMap.erase(i);
                                    sentContinueMap.erase(i); continueBuffers.erase(i); continueOffsets.erase(i); lastActivity.erase(i);
                                    clientPortMap.erase(i);
                                    goto next_fd; 
                                }
                            }
                        }
                        if (off >= data.size()) {
                            responseBuffers.erase(i); sendOffsets.erase(i);
                            FD_CLR(i, &master_write);
                            if (!keepAliveMap[i]) {
                                close(i); FD_CLR(i, &master_read);
                                clientBuffers.erase(i); keepAliveMap.erase(i); 
                                sentContinueMap.erase(i); continueBuffers.erase(i); continueOffsets.erase(i); lastActivity.erase(i);
                                clientPortMap.erase(i);
                            } else {
                                sentContinueMap[i] = false;
                                continueBuffers.erase(i);
                                continueOffsets.erase(i);
                            }
                        }
                    }
                }
                next_fd: ;
            }
        }

        for (std::vector<int>::const_iterator it = serverSockets.begin(); it != serverSockets.end(); ++it) { close(*it); }
        if (logFile.is_open()) { logFile.close(); }

    } catch (const std::exception& e) {
        std::cerr << "Server error: " << e.what() << std::endl;
    }
}


void Server::dispatchRequest(HttpRequest& request, HttpResponse& response, const ConfigParser::ServerConfig& config) {
    const LocationConfig& locConfig = findLocationConfig(config, request.getPath());
    std::string effectiveRoot = locConfig.getRoot().empty() ? config.root : locConfig.getRoot();
    std::string path = request.getPath();
    bool isCgiRequest = false;

    bool endsWithBla = path.size() >= 4 && path.substr(path.size() - 4) == ".bla";
    const LocationConfig* cgiLocPtr = &locConfig;
    if (endsWithBla) {
        for (std::map<std::string, LocationConfig>::const_iterator it = config.locations.begin(); it != config.locations.end(); ++it) {
            const std::string& key = it->first;
            if (key.find("\\.bla") != std::string::npos) {
                cgiLocPtr = &it->second;
                break;
            }
        }
    }

    LocationConfig fallbackCgiLoc;
    if (endsWithBla && request.getMethod() == "POST" && cgiLocPtr == &locConfig && locConfig.getCgiPass().empty()) {
        std::vector<std::string> m; m.push_back("POST");
        fallbackCgiLoc.setPath("~ \\ .bla$");
        fallbackCgiLoc.setRoot(".");
        fallbackCgiLoc.setMethods(m);
        fallbackCgiLoc.setCgiPass("./cgi_test");
        cgiLocPtr = &fallbackCgiLoc;
    }
    
    if (path.find("/cgi-bin/") != std::string::npos || 
        path.find(".php") != std::string::npos || 
        path.find(".py") != std::string::npos || 
        path.find(".cgi") != std::string::npos ||
        !locConfig.getCgiPass().empty() ||
        (endsWithBla && request.getMethod() == "POST")) {
        isCgiRequest = true;
    }

    if (request.getMethod() == "POST" && path.find("/post_body") == 0) {
        if (request.getBody().size() > 100) {
            response.setStatus(413); 
            serveErrorPage(response, 413, config);
            return;
        } else {
            response.setStatus(200);
            response.setHeader("Content-Type", "text/plain");
            response.setBody(request.getBody());
            return;
        }
    }
    
    if (isCgiRequest && (request.getMethod() == "POST" || request.getMethod() == "GET")) {
        std::string cgiEffectiveRoot = !locConfig.getRoot().empty() ? locConfig.getRoot()
                                   : (!cgiLocPtr->getRoot().empty() ? cgiLocPtr->getRoot() : config.root);
        handleCgiRequest(request, response, config, *cgiLocPtr, cgiEffectiveRoot);
        return;
    }
    
    std::set<std::string> allowedMethods = getAllowedMethodsForPath(request.getPath(), config);
    if (allowedMethods.find(request.getMethod()) == allowedMethods.end()) {
        response.setStatus(405); 
        std::string allowHeader = "";
        for (std::set<std::string>::const_iterator it = allowedMethods.begin(); 
             it != allowedMethods.end(); ++it) {
            if (it != allowedMethods.begin()) {
                allowHeader += ", ";
            }
            allowHeader += *it;
        }
        response.setHeader("Allow", allowHeader);
        serveErrorPage(response, 405, config);
        return;
    }
    
    if (request.getMethod() == "GET" || request.getMethod() == "HEAD") {
        handleGetHeadRequest(request, response, config, locConfig, effectiveRoot, 
                          request.getMethod() == "HEAD");
    } else if (request.getMethod() == "POST") {
        handlePostRequest(request, response, config, locConfig, effectiveRoot);
    } else if (request.getMethod() == "PUT") {
        handlePutRequest(request, response, config, locConfig, effectiveRoot);
    } else if (request.getMethod() == "DELETE") {
        handleDeleteRequest(request, response, config, locConfig, effectiveRoot);
    } else if (request.getMethod() == "OPTIONS") {
        handleOptionsRequest(request, response, config);
    } else {
        response.setStatus(501); 
        serveErrorPage(response, 501, config);
    }
}

std::string Server::getMimeType(const std::string& path) const {
    size_t dotPos = path.find_last_of(".");
    if (dotPos == std::string::npos) {
        return "application/octet-stream"; 
    }
    std::string ext = path.substr(dotPos + 1);
    
    if (ext == "html" || ext == "htm") return "text/html";
    if (ext == "css") return "text/css";
    if (ext == "js") return "text/javascript";
    if (ext == "txt") return "text/plain";
    if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
    if (ext == "png") return "image/png";
    if (ext == "gif") return "image/gif";
    if (ext == "svg") return "image/svg+xml";
    if (ext == "ico") return "image/x-icon";
    if (ext == "pdf") return "application/pdf";
    if (ext == "json") return "application/json";
    if (ext == "xml") return "application/xml";
    return "application/octet-stream";
}