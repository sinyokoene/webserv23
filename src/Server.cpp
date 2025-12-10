#include "Server.hpp"
#include "Utils.hpp"

// Event loop tuning knobs
static const int SELECT_TIMEOUT_SEC = 1;
static const int CLIENT_TIMEOUT_SEC = 30;
static const int CGI_TIMEOUT_SEC = 120;
static const size_t MAX_HEADER_BYTES = 32 * 1024;
static const size_t MAX_REQUEST_BYTES = 200 * 1024 * 1024;
static const size_t FILE_CHUNK_BYTES = 16 * 1024;

// ---- internal helpers ----------------------------------------------------
static std::map<std::string, std::string> parseHeaderMap(const std::string& headerBlock) {
    std::map<std::string, std::string> headers;
    std::istringstream hs(headerBlock);
    std::string line;
    while (std::getline(hs, line)) {
        if (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
        size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string name = toLower(line.substr(0, colon));
        std::string value = line.substr(colon + 1);
        size_t first = value.find_first_not_of(" \t");
        size_t last = value.find_last_not_of(" \t");
        if (first != std::string::npos) value = value.substr(first, last - first + 1); else value = "";
        headers[name] = value;
    }
    return headers;
}

static bool needsWrite(const ClientState& st) {
    if (st.outOffset < st.outBuffer.size()) return true;
    if (st.fileStream.active) {
        if (!st.fileStream.pendingChunk.empty()) return true;
        if (st.fileStream.offset < st.fileStream.size) return true;
    }
    return false;
}

static void clearFileStream(FileStreamState& fs) {
    if (fs.fd != -1) close(fs.fd);
    fs.fd = -1;
    fs.offset = 0;
    fs.size = 0;
    fs.active = false;
    fs.isHead = false;
    fs.pendingChunk.clear();
}

static bool decodeChunkedBody(const std::string& data, size_t startPos, size_t& consumed, std::string& out) {
    size_t pos = startPos;
    out.clear();
    while (true) {
        size_t lineEnd = data.find("\r\n", pos);
        if (lineEnd == std::string::npos) return false;
        std::string sizeLine = data.substr(pos, lineEnd - pos);
        size_t sc = sizeLine.find(';');
        if (sc != std::string::npos) sizeLine = sizeLine.substr(0, sc);
        size_t f = sizeLine.find_first_not_of(" \t");
        size_t l = sizeLine.find_last_not_of(" \t");
        if (f == std::string::npos) return false;
        sizeLine = sizeLine.substr(f, l - f + 1);
        unsigned long chunkSize = 0;
        std::istringstream xs(sizeLine);
        xs >> std::hex >> chunkSize;
        if (xs.fail()) return false;
        pos = lineEnd + 2;
        if (chunkSize == 0) {
            size_t trailerEnd = data.find("\r\n", pos);
            if (trailerEnd == std::string::npos) return false;
            consumed = trailerEnd + 2;
            return true;
        }
        if (data.size() < pos + chunkSize + 2) return false;
        out.append(data, pos, chunkSize);
        pos += chunkSize;
        if (!(data[pos] == '\r' && data[pos + 1] == '\n')) return false;
        pos += 2;
    }
}

static void cleanupCgi(int fd, std::map<int, CgiState>& cgiStates);
static void closeClientFd(int fd, fd_set& mr, fd_set& mw, std::map<int, ClientState>& clients, std::map<int, CgiState>& cgiStates);

void Server::buildPortMapping(std::set<int>& portsToBind) {
    portsToBind.clear();
    portToConfigs.clear();

    if (serverConfigs.empty()) {
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
}

bool Server::bindListeningSockets(const std::set<int>& portsToBind) {
    socketPortMap.clear();
    serverSockets.clear();

    for (std::set<int>::const_iterator it = portsToBind.begin(); it != portsToBind.end(); ++it) {
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
        std::cout << "Server is listening on port " << port << std::endl;
    }

    if (serverSockets.empty()) {
        std::cerr << "Failed to set up any server sockets. Exiting." << std::endl;
        return false;
    }
    return true;
}

void Server::initMasterFdSets(fd_set& master_read, fd_set& master_write, int& fdmax) const {
    FD_ZERO(&master_read);
    FD_ZERO(&master_write);
    fdmax = 0;

    for (std::vector<int>::const_iterator it = serverSockets.begin(); it != serverSockets.end(); ++it) {
        FD_SET(*it, &master_read);
        if (*it > fdmax) fdmax = *it;
    }
}

void Server::buildFdSets(const fd_set& master_read, const fd_set& master_write,
                         fd_set& read_fds, fd_set& write_fds, int fdmax, int& loopFdMax) {
    read_fds = master_read;
    write_fds = master_write;
    loopFdMax = fdmax;

    for (std::map<int, CgiState>::iterator cit = cgiStates.begin(); cit != cgiStates.end(); ++cit) {
        if (cit->second.pipe_out != -1 && !cit->second.readComplete) {
            FD_SET(cit->second.pipe_out, &read_fds);
            if (cit->second.pipe_out > loopFdMax) loopFdMax = cit->second.pipe_out;
        }
        if (cit->second.pipe_in != -1 && !cit->second.writeComplete) {
            FD_SET(cit->second.pipe_in, &write_fds);
            if (cit->second.pipe_in > loopFdMax) loopFdMax = cit->second.pipe_in;
        }
    }
}

void Server::handleClientTimeouts(std::map<int, ClientState>& clients,
                                  fd_set& master_read, fd_set& master_write, time_t now) {
    for (std::map<int, ClientState>::iterator it = clients.begin(); it != clients.end(); ) {
        if (now - it->second.lastActivity > CLIENT_TIMEOUT_SEC) {
            closeClientFd(it->first, master_read, master_write, clients, cgiStates);
            it = clients.begin();
        } else {
            ++it;
        }
    }
}

void Server::handleCgiTimeouts(std::map<int, ClientState>& clients,
                               fd_set& master_write, int& fdmax, time_t now) {
    for (std::map<int, CgiState>::iterator cit = cgiStates.begin(); cit != cgiStates.end(); ) {
        if (now - cit->second.lastIO > CGI_TIMEOUT_SEC) {
            HttpResponse response;
            serveErrorPage(response, 504, *cit->second.config);
            int clientFd = cit->first;
            if (clients.find(clientFd) != clients.end()) {
                clients[clientFd].outBuffer = response.generateResponse(cit->second.isHead);
                clients[clientFd].outOffset = 0;
                FD_SET(clientFd, &master_write);
                if (clientFd > fdmax) fdmax = clientFd;
            }
            cleanupCgi(clientFd, cgiStates);
            cit = cgiStates.begin();
        } else {
            ++cit;
        }
    }
}

void Server::acceptConnections(fd_set& master_read, int& fdmax,
                               std::map<int, ClientState>& clients, time_t now) {
    for (std::vector<int>::const_iterator it = serverSockets.begin(); it != serverSockets.end(); ++it) {
        if (FD_ISSET(*it, &master_read)) {
            while (true) {
                struct sockaddr_in clientAddr;
                socklen_t clientLen = sizeof(clientAddr);
                int clientSocket = accept(*it, (struct sockaddr*)&clientAddr, &clientLen);
                if (clientSocket < 0) {
                    // Non-blocking accept has no more queued connections; log once and exit the loop
                    std::cerr << "Error accepting connection: " << strerror(errno) << std::endl;
                    break;
                }
                int cflags = fcntl(clientSocket, F_GETFL, 0);
                if (cflags != -1) fcntl(clientSocket, F_SETFL, cflags | O_NONBLOCK);
                FD_SET(clientSocket, &master_read);
                if (clientSocket > fdmax) fdmax = clientSocket;
                ClientState cs;
                cs.lastActivity = now;
                cs.port = socketPortMap[*it];
                clients[clientSocket] = cs;
            }
        }
    }
}

void Server::processCgiIo(fd_set& read_fds, fd_set& write_fds,
                          fd_set& master_write, int& fdmax,
                          std::map<int, ClientState>& clients) {
    for (std::map<int, CgiState>::iterator cit = cgiStates.begin(); cit != cgiStates.end(); ) {
        CgiState& cgi = cit->second;
        int clientFd = cit->first;
        if (cgi.pipe_in != -1 && FD_ISSET(cgi.pipe_in, &write_fds)) {
            handleCgiWrite(clientFd, cgi);
        }
        if (cgi.pipe_out != -1 && FD_ISSET(cgi.pipe_out, &read_fds)) {
            handleCgiRead(clientFd, cgi);
        }
        if (cgi.readComplete) {
            int status;
            pid_t result = waitpid(cgi.pid, &status, WNOHANG);
            if (result != 0) {
                std::string response;
                finalizeCgiRequest(clientFd, cgi, status, response);
                if (clients.find(clientFd) != clients.end()) {
                    clients[clientFd].outBuffer = response;
                    clients[clientFd].outOffset = 0;
                    clients[clientFd].keepAlive = false;
                    FD_SET(clientFd, &master_write);
                    if (clientFd > fdmax) fdmax = clientFd;
                }
                std::map<int, CgiState>::iterator next = cit;
                ++next;
                cleanupCgi(clientFd, cgiStates);
                cit = next;
                continue;
            }
        }
        ++cit;
    }
}

void Server::processClientReads(fd_set& read_fds, fd_set& master_read, fd_set& master_write,
                                int& fdmax, std::map<int, ClientState>& clients,
                                time_t now) {
    for (std::map<int, ClientState>::iterator it = clients.begin(); it != clients.end(); ) {
        int fd = it->first;
        ClientState& state = it->second;
        bool closed = false;

        if (FD_ISSET(fd, &read_fds)) {
            char buffer[8192];
            while (true) {
                ssize_t bytesRead = recv(fd, buffer, sizeof(buffer), 0);
                if (bytesRead > 0) {
                    state.inBuffer.append(buffer, bytesRead);
                    state.lastActivity = now;
                    if (state.inBuffer.size() > MAX_REQUEST_BYTES) {
                        HttpResponse resp;
                        resp.setStatus(413);
                        serveErrorPage(resp, 413, selectConfig(state.port, ""));
                        state.keepAlive = false;
                        state.outBuffer = resp.generateResponse(false);
                        state.outOffset = 0;
                        FD_SET(fd, &master_write);
                        break;
                    }
                } else if (bytesRead == 0) {
                    std::map<int, ClientState>::iterator next = it;
                    ++next;
                    closeClientFd(fd, master_read, master_write, clients, cgiStates);
                    it = next;
                    closed = true;
                    break;
                } else {
                    // On non-blocking sockets, a negative read here simply defers to the next select cycle
                    break;
                }
            }
        }

        if (closed) continue;

        bool parsed = true;
        while (parsed) {
            parsed = false;
            size_t headerEnd = state.inBuffer.find("\r\n\r\n");
            size_t sepLen = 4;
            if (headerEnd == std::string::npos) {
                headerEnd = state.inBuffer.find("\n\n");
                sepLen = 2;
            }
            if (headerEnd == std::string::npos) {
                if (state.inBuffer.size() > MAX_HEADER_BYTES) {
                    HttpResponse resp;
                    resp.setStatus(431);
                    serveErrorPage(resp, 431, selectConfig(state.port, ""));
                    state.keepAlive = false;
                    state.outBuffer = resp.generateResponse(false);
                    state.outOffset = 0;
                    FD_SET(fd, &master_write);
                }
                break;
            }

            size_t bodyStart = headerEnd + sepLen;
            std::string headerBlock = state.inBuffer.substr(0, headerEnd);
            std::map<std::string, std::string> headers = parseHeaderMap(headerBlock);
            std::string hostHeader = headers["host"];
            bool hasContentLength = headers.find("content-length") != headers.end();
            size_t contentLength = 0;
            if (hasContentLength) {
                std::istringstream iss(headers["content-length"]);
                iss >> contentLength;
            }
            bool isChunked = headers.find("transfer-encoding") != headers.end() &&
                             headers["transfer-encoding"].find("chunked") != std::string::npos;
            state.expectContinue = headers.find("expect") != headers.end() &&
                                   headers["expect"].find("100-continue") != std::string::npos;

            const ConfigParser::ServerConfig& cfg = selectConfig(state.port, hostHeader);

            if (state.expectContinue && !state.sentContinue) {
                static const char kContinue[] = "HTTP/1.1 100 Continue\r\n\r\n";
                state.outBuffer += std::string(kContinue);
                state.outOffset = 0;
                state.sentContinue = true;
                FD_SET(fd, &master_write);
            }

            std::string normalizedRequest;
            size_t consumed = 0;
            if (isChunked) {
                size_t consumedEnd = 0;
                if (!decodeChunkedBody(state.inBuffer, bodyStart, consumedEnd, state.chunkDecoded)) {
                    break;
                }
                std::string reqLine = state.inBuffer.substr(0, state.inBuffer.find("\r\n"));
                std::string headersOnly = state.inBuffer.substr(state.inBuffer.find("\r\n") + 2, headerEnd - (state.inBuffer.find("\r\n") + 2));
                std::istringstream hsin(headersOnly);
                std::string line;
                std::string rebuiltHeaders;
                while (std::getline(hsin, line)) {
                    if (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
                    size_t colon = line.find(':');
                    if (colon == std::string::npos) continue;
                    std::string lower = toLower(line.substr(0, colon));
                    if (lower == "transfer-encoding" || lower == "content-length") continue;
                    rebuiltHeaders += line + "\r\n";
                }
                std::ostringstream cl;
                cl << "Content-Length: " << state.chunkDecoded.size() << "\r\n";
                rebuiltHeaders += cl.str();
                normalizedRequest = reqLine + "\r\n" + rebuiltHeaders + "\r\n" + state.chunkDecoded;
                consumed = consumedEnd;
                hasContentLength = true;
                contentLength = state.chunkDecoded.size();
            } else if (hasContentLength) {
                size_t have = state.inBuffer.size() > bodyStart ? state.inBuffer.size() - bodyStart : 0;
                if (have < contentLength) break;
                consumed = bodyStart + contentLength;
                normalizedRequest = state.inBuffer.substr(0, consumed);
            } else {
                consumed = bodyStart;
                normalizedRequest = state.inBuffer.substr(0, consumed);
            }

            try {
                HttpRequest req;
                req.parseRequest(normalizedRequest);
                HttpResponse resp;
                bool responseReady = false;
                dispatchRequest(fd, req, resp, cfg, responseReady, state);
                if (responseReady) {
                    std::string connHeader = req.getHeader("Connection");
                    std::string version = req.getVersion();
                    std::string connLower = toLower(connHeader);
                    bool keep = false;
                    if (version == "HTTP/1.1") keep = (connLower != "close");
                    else keep = (connLower == "keep-alive");
                    state.keepAlive = keep;
                    resp.setHeader("Connection", keep ? "keep-alive" : "close");
                    state.outBuffer += resp.generateResponse(req.getMethod() == "HEAD");
                    state.outOffset = 0;
                    FD_SET(fd, &master_write);
                    if (fd > fdmax) fdmax = fd;
                }
            } catch (const std::exception& e) {
                HttpResponse err;
                err.setStatus(400);
                serveErrorPage(err, 400, cfg);
                state.keepAlive = false;
                state.outBuffer += err.generateResponse(false);
                state.outOffset = 0;
                FD_SET(fd, &master_write);
            }

            if (consumed >= state.inBuffer.size()) state.inBuffer.clear();
            else state.inBuffer.erase(0, consumed);
            state.expectContinue = false;
            state.sentContinue = false;
            state.chunkDecoded.clear();
            parsed = true;
        }

        ++it;
    }
}

void Server::processClientWrites(fd_set& write_fds, fd_set& master_read, fd_set& master_write,
                                 std::map<int, ClientState>& clients, time_t now) {
    for (std::map<int, ClientState>::iterator it = clients.begin(); it != clients.end(); ) {
        int fd = it->first;
        ClientState& st = it->second;
        bool closed = false;

        if (FD_ISSET(fd, &write_fds)) {
            while (st.outOffset < st.outBuffer.size()) {
                ssize_t sent = send(fd, st.outBuffer.c_str() + st.outOffset, st.outBuffer.size() - st.outOffset, 0);
                if (sent > 0) {
                    st.outOffset += sent;
                    st.lastActivity = now;
                } else {
                    break;
                }
            }
            if (closed) continue;

            if (st.outOffset >= st.outBuffer.size()) {
                st.outBuffer.clear();
                st.outOffset = 0;
            }

            if (st.fileStream.active && st.outBuffer.empty()) {
                if (st.fileStream.pendingChunk.empty() && st.fileStream.offset < st.fileStream.size) {
                    char fbuf[FILE_CHUNK_BYTES];
                    ssize_t r = read(st.fileStream.fd, fbuf, FILE_CHUNK_BYTES);
                    if (r > 0) {
                        st.fileStream.pendingChunk.assign(fbuf, r);
                        st.fileStream.offset += r;
                    } else if (r == 0) {
                        clearFileStream(st.fileStream);
                    } else {
                        std::map<int, ClientState>::iterator next = it;
                        ++next;
                        closeClientFd(fd, master_read, master_write, clients, cgiStates);
                        it = next;
                        closed = true;
                    }
                }
                if (closed) continue;

                while (!st.fileStream.pendingChunk.empty()) {
                    ssize_t sent = send(fd, st.fileStream.pendingChunk.c_str(), st.fileStream.pendingChunk.size(), 0);
                    if (sent > 0) {
                        st.fileStream.pendingChunk.erase(0, sent);
                        st.lastActivity = now;
                    } else {
                        break;
                    }
                }
                if (st.fileStream.pendingChunk.empty() && st.fileStream.offset >= st.fileStream.size) {
                    clearFileStream(st.fileStream);
                }
            }

            if (!needsWrite(st)) {
                FD_CLR(fd, &master_write);
                if (!st.keepAlive) {
                    std::map<int, ClientState>::iterator next = it;
                    ++next;
                    closeClientFd(fd, master_read, master_write, clients, cgiStates);
                    it = next;
                    closed = true;
                }
            }
        }

        if (closed) continue;

        ++it;
    }
}

static void cleanupCgi(int fd, std::map<int, CgiState>& cgiStates) {
    std::map<int, CgiState>::iterator cgit = cgiStates.find(fd);
    if (cgit != cgiStates.end()) {
        if (cgit->second.pipe_in != -1) close(cgit->second.pipe_in);
        if (cgit->second.pipe_out != -1) close(cgit->second.pipe_out);
        kill(cgit->second.pid, SIGKILL);
        waitpid(cgit->second.pid, NULL, WNOHANG);
        cgiStates.erase(cgit);
    }
}

static void closeClientFd(int fd, fd_set& mr, fd_set& mw, std::map<int, ClientState>& clients, std::map<int, CgiState>& cgiStates) {
    cleanupCgi(fd, cgiStates);
    std::map<int, ClientState>::iterator it = clients.find(fd);
    if (it != clients.end()) {
        clearFileStream(it->second.fileStream);
        clients.erase(it);
    }
    close(fd);
    FD_CLR(fd, &mr);
    FD_CLR(fd, &mw);
}

// ---- end helpers ---------------------------------------------------------

Server::Server(const std::string& configFile) {
    configPath = configFile;
    parseConfig(configFile);
    if (serverConfigs.empty()) {
        throw std::runtime_error("No server configurations loaded.");
    }
    currentConfig = serverConfigs[0];
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

std::pair<std::string, const LocationConfig*> Server::matchLocation(const ConfigParser::ServerConfig& serverConfig, const std::string& path) const {
    std::string bestMatchPath;
    const LocationConfig* bestMatchConfig = &serverConfig.defaultLocationSettings;

    for (std::map<std::string, LocationConfig>::const_iterator it = serverConfig.locations.begin(); it != serverConfig.locations.end(); ++it) {
        const std::string& locationPath = it->first;
        bool matches = false;

        if (path.find(locationPath) == 0) {
            matches = true;
        } else if (!locationPath.empty() && locationPath[locationPath.size() - 1] == '/') {
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
        if (matches && locationPath.length() > bestMatchPath.length()) {
            bestMatchPath = locationPath;
            bestMatchConfig = &it->second;
        }
    }

    return std::make_pair(bestMatchPath, bestMatchConfig);
}

const LocationConfig& Server::findLocationConfig(const ConfigParser::ServerConfig& serverConfig, const std::string& path) const {
    return *(matchLocation(serverConfig, path).second);
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
        std::pair<std::string, const LocationConfig*> match = matchLocation(config, relativePath);
        const std::string& bestMatchPath = match.first;
        const LocationConfig* bestMatchConfig = match.second;
        
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

        // If the matched location path is an exact file (no trailing slash) and there is
        // no remaining subpath, use the request path (without the leading slash) so we
        // resolve to the file instead of the directory root.
        if (joinPath.empty() &&
            !bestMatchPath.empty() &&
            bestMatchPath == relativePath &&
            bestMatchPath[bestMatchPath.size() - 1] != '/') {
            joinPath = relativePath.substr(1);
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
        if (toLower(configs[i]->serverName) == hostname) {
             return *configs[i];
        }
    }
    
    // 2. Default to the first one for this port
    return *configs[0];
}

void Server::start() {
    std::map<int, ClientState> clients;

    try {
        std::set<int> portsToBind;
        buildPortMapping(portsToBind);

        if (!bindListeningSockets(portsToBind)) return;

        fd_set master_read, master_write;
        int fdmax = 0;
        initMasterFdSets(master_read, master_write, fdmax);

        std::cout << "Server is running. Press Ctrl+C to stop." << std::endl;

        while (true) {
            fd_set read_fds;
            fd_set write_fds;
            int loopFdMax = 0;
            buildFdSets(master_read, master_write, read_fds, write_fds, fdmax, loopFdMax);

            struct timeval tv;
            tv.tv_sec = SELECT_TIMEOUT_SEC;
            tv.tv_usec = 0;
            int nready = select(loopFdMax + 1, &read_fds, &write_fds, NULL, &tv);
            if (nready == -1) {
                std::cerr << "Error in select()" << std::endl;
                break;
            }

            time_t now = time(NULL);

            handleClientTimeouts(clients, master_read, master_write, now);
            handleCgiTimeouts(clients, master_write, fdmax, now);
            acceptConnections(master_read, fdmax, clients, now);
            processCgiIo(read_fds, write_fds, master_write, fdmax, clients);
            processClientReads(read_fds, master_read, master_write, fdmax, clients, now);
            processClientWrites(write_fds, master_read, master_write, clients, now);
        }

        for (std::vector<int>::const_iterator it = serverSockets.begin(); it != serverSockets.end(); ++it) { close(*it); }

    } catch (const std::exception& e) {
        std::cerr << "Server error: " << e.what() << std::endl;
    }
}


void Server::dispatchRequest(int clientFd, HttpRequest& request, HttpResponse& response, 
                              const ConfigParser::ServerConfig& config, bool& responseReady, ClientState& state) {
    responseReady = true; // Default: response is ready unless CGI
    clearFileStream(state.fileStream);
    
    const LocationConfig& locConfig = findLocationConfig(config, request.getPath());
    std::string effectiveRoot = locConfig.getRoot().empty() ? config.root : locConfig.getRoot();
    std::string path = request.getPath();
    bool isCgiRequest =
        path.find("/cgi-bin/") != std::string::npos ||
        path.find(".php") != std::string::npos ||
        path.find(".py")  != std::string::npos ||
        path.find(".cgi") != std::string::npos ||
        !locConfig.getCgiPass().empty();

    // Handle CGI requests
    if (isCgiRequest && (request.getMethod() == "POST" || request.getMethod() == "GET" || request.getMethod() == "HEAD")) {
        std::string cgiEffectiveRoot = !locConfig.getRoot().empty() ? locConfig.getRoot() : config.root;
        bool isHead = (request.getMethod() == "HEAD");
        bool cgiStarted = startCgiRequest(clientFd, request, config, locConfig, cgiEffectiveRoot, isHead);
        if (cgiStarted) {
            responseReady = false; // Response will be generated later when CGI completes
            return;
        } else {
            // CGI failed to start
            response.setStatus(500);
            serveErrorPage(response, 500, config);
            return;
        }
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
                          request.getMethod() == "HEAD", state.fileStream);
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